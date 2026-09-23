#pragma once

// muse::io ships only a Qt implementation of IFileSystem
// (global/io/internal/filesystem.cpp, QDir/QDirListing). A Qt-free build has
// to supply the interface itself — this is the minimum the engine needs
// (exists + readFile), on std::filesystem/fstream. Everything else reports
// NotSupported. Qrc-style ":/..." resource paths are remapped onto a real
// directory (resources/ in this repo), the same pattern as the fork's
// web/webfilesystem.h.
//
// Reads are confined to the roots the caller opens: the resource directory,
// and whatever allowRead() adds (the score, a font handed to addFont). See
// resolve() for why that is not optional.

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "global/io/ifilesystem.h"

#include "log.h"

namespace sve {
class EngineFileSystem : public muse::io::IFileSystem
{
public:
    explicit EngineFileSystem(const std::string& resourceRoot)
        : m_resourceRoot(normalize(resourceRoot))
    {
        if (!m_resourceRoot.empty()) {
            m_allowed.push_back(m_resourceRoot);
        }
    }

    //! Permit reads of `path` — that file, or everything under it when it is a
    //! directory. Called for the score being converted and for fonts the
    //! caller registers; nothing else in this build opens a root.
    void allowRead(const muse::io::path_t& path)
    {
        std::string p = normalize(path.toStdString());
        if (p.empty()) {
            return;
        }
        for (const std::string& root : m_allowed) {
            if (p == root) {
                return;
            }
        }
        m_allowed.push_back(std::move(p));
    }

    //! Also answers for the directories an opened root sits in. Upstream reads
    //! a .mscx in Dir mode: MscReader::DirReader takes the score's directory
    //! as its root and asks whether it exists before it reads the one file it
    //! wants. Opening that directory for reads would hand a score everything
    //! beside it; answering exists() for it tells nothing - the ancestors of a
    //! file that was opened for reading exist by definition.
    muse::Ret exists(const muse::io::path_t& path) const override
    {
        std::string real = resolve(path);
        if (real.empty()) {
            real = ancestorOfRoot(path);
        }
        if (real.empty()) {
            return muse::make_ret(muse::Ret::Code::UnknownError);
        }
        std::error_code ec;
        bool ok = std::filesystem::exists(real, ec);
        return muse::make_ret(ok ? muse::Ret::Code::Ok : muse::Ret::Code::UnknownError);
    }

    muse::RetVal<muse::ByteArray> readFile(const muse::io::path_t& filePath) const override
    {
        muse::RetVal<muse::ByteArray> rv;
        rv.ret = readFile(filePath, rv.val);
        return rv;
    }

    muse::Ret readFile(const muse::io::path_t& filePath, muse::ByteArray& data) const override
    {
        const std::string real = resolve(filePath);
        if (real.empty()) {
            return muse::make_ret(muse::Ret::Code::UnknownError, "refused read " + filePath.toStdString());
        }
        // Regular files only. A directory gets past is_open() on glibc, and
        // what the seek to its end answers depends on the file system: -1 on
        // some, and on ext4 and overlayfs a huge positive offset, which the
        // size check below cannot tell from a real size. resize() then threw
        // std::length_error in the middle of a load - and a score reaches
        // this with a chordDescriptionFile of "..", which names a directory
        // inside the resource root.
        std::error_code ec;
        if (!std::filesystem::is_regular_file(real, ec)) {
            return muse::make_ret(muse::Ret::Code::UnknownError, "not a file " + filePath.toStdString());
        }
        std::ifstream f(real, std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            return muse::make_ret(muse::Ret::Code::UnknownError, "failed open " + filePath.toStdString());
        }
        // tellg() answers -1 when the seek failed.
        const std::streamoff size = f.tellg();
        if (size < 0) {
            return muse::make_ret(muse::Ret::Code::UnknownError, "failed size " + filePath.toStdString());
        }
        f.seekg(0, std::ios::beg);
        data.resize(static_cast<size_t>(size));
        if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), size)) {
            return muse::make_ret(muse::Ret::Code::UnknownError, "failed read " + filePath.toStdString());
        }
        return muse::make_ret(muse::Ret::Code::Ok);
    }

    muse::RetVal<uint64_t> fileSize(const muse::io::path_t& path) const override
    {
        muse::RetVal<uint64_t> rv;
        const std::string real = resolve(path);
        if (real.empty()) {
            rv.ret = muse::make_ret(muse::Ret::Code::UnknownError);
            rv.val = 0;
            return rv;
        }
        std::error_code ec;
        uint64_t size = std::filesystem::file_size(real, ec);
        rv.ret = muse::make_ret(ec ? muse::Ret::Code::UnknownError : muse::Ret::Code::Ok);
        rv.val = ec ? 0 : size;
        return rv;
    }

    // Unused by the conversion path:
    muse::Ret remove(const muse::io::path_t&, bool) override { return notSupported(); }
    muse::Ret clear(const muse::io::path_t&) override { return notSupported(); }
    muse::Ret copy(const muse::io::path_t&, const muse::io::path_t&, bool) override { return notSupported(); }
    muse::Ret move(const muse::io::path_t&, const muse::io::path_t&, bool) override { return notSupported(); }
    muse::Ret makePath(const muse::io::path_t&) const override { return notSupported(); }
    muse::Ret makeLink(const muse::io::path_t&, const muse::io::path_t&) const override { return notSupported(); }
    muse::io::EntryType entryType(const muse::io::path_t&) const override { return muse::io::EntryType::Undefined; }
    muse::RetVal<muse::io::paths_t> scanFiles(const muse::io::path_t&, const std::vector<std::string>&,
                                              muse::io::ScanMode) const override
    {
        return muse::RetVal<muse::io::paths_t>(notSupported());
    }

    void setAttribute(const muse::io::path_t&, Attribute) const override {}
    bool setPermissionsAllowedForAll(const muse::io::path_t&) const override { return false; }
    muse::Ret writeFile(const muse::io::path_t&, const muse::ByteArray&) override { return notSupported(); }
    muse::RetVal<muse::io::StreamId> openStream(const muse::io::path_t&, muse::io::OpenMode) override
    {
        return muse::RetVal<muse::io::StreamId>(notSupported());
    }

    muse::Ret writeToStream(muse::io::StreamId, const muse::ByteArray&, uint64_t) override { return notSupported(); }
    muse::Ret closeStream(muse::io::StreamId) override { return notSupported(); }
    muse::io::path_t canonicalFilePath(const muse::io::path_t& p) const override { return p; }
    muse::io::path_t absolutePath(const muse::io::path_t& p) const override { return p; }
    muse::io::path_t absoluteFilePath(const muse::io::path_t& p) const override { return p; }
    muse::DateTime birthTime(const muse::io::path_t&) const override { return muse::DateTime(); }
    muse::DateTime lastModified(const muse::io::path_t&) const override { return muse::DateTime(); }
    muse::Ret isWritable(const muse::io::path_t&) const override { return notSupported(); }

private:
    static muse::Ret notSupported() { return muse::make_ret(muse::Ret::Code::NotSupported); }

    //! An engine path mapped onto a real one, or "" when the engine may not
    //! read it.
    //!
    //! Two jobs. The qrc remap, ":/x" -> <resourceRoot>/x, is what this class
    //! was written for. The confinement is the other one, and it is not
    //! belt-and-braces: `chordDescriptionFile` is a STYLE value, so it comes
    //! out of the .mscx, and upstream's ChordList::read() pastes a relative
    //! one behind ":/engraving/styles/" without normalizing it
    //! (dom/chordlist.cpp). Plain concatenation let "../../../../etc/passwd"
    //! out of the resource tree and into an XML parser. Normalizing alone
    //! would not close it either — the same style value written as an
    //! absolute path never reaches the qrc branch at all — so every read is
    //! tested against the roots the caller opened, and the engine gets no
    //! others. The wasm build was always confined by MEMFS; this is what the
    //! native CLI and the sidecar were missing.
    //!
    //! Lexical normalization, not weakly_canonical: a score cannot plant a
    //! symlink on the host, resolving them would cost a stat per read, and
    //! MEMFS has none to follow anyway.
    std::string resolve(const muse::io::path_t& path) const
    {
        const std::string s = path.toStdString();
        if (s.empty()) {
            return std::string();
        }

        std::string mapped = s;
        if (!m_resourceRoot.empty() && s.rfind(":/", 0) == 0) {
            mapped = m_resourceRoot + s.substr(1);
        }

        const std::string full = normalize(mapped);
        if (full.empty()) {
            return std::string();
        }
        for (const std::string& root : m_allowed) {
            if (isWithin(full, root)) {
                return full;
            }
        }
        LOGW() << "refusing a read outside the opened roots: " << s;
        return std::string();
    }

    //! `path`, normalized, when an opened root sits under it; otherwise "".
    //! For exists() only - see there.
    std::string ancestorOfRoot(const muse::io::path_t& path) const
    {
        const std::string full = normalize(path.toStdString());
        if (full.empty()) {
            return std::string();
        }
        for (const std::string& root : m_allowed) {
            if (isWithin(root, full)) {
                return full;
            }
        }
        return std::string();
    }

    //! Absolute and free of "." / "..", with no trailing separator. Empty when
    //! the path cannot be made absolute at all.
    static std::string normalize(const std::string& p)
    {
        if (p.empty()) {
            return std::string();
        }
        std::error_code ec;
        const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(p), ec);
        if (ec) {
            return std::string();
        }
        std::string s = abs.lexically_normal().generic_string();
        while (s.size() > 1 && s.back() == '/') {
            s.pop_back();
        }
        return s;
    }

    //! `path` is `root` itself or sits under it. The separator test is what
    //! keeps "/srv/resources-backup" from counting as inside "/srv/resources".
    static bool isWithin(const std::string& path, const std::string& root)
    {
        if (path == root) {
            return true;
        }
        if (path.size() <= root.size() || path.compare(0, root.size(), root) != 0) {
            return false;
        }
        return path[root.size()] == '/' || (root.size() == 1 && root[0] == '/');
    }

    std::string m_resourceRoot;
    std::vector<std::string> m_allowed;
};
}
