#pragma once

// muse::io ships only a Qt implementation of IFileSystem
// (global/io/internal/filesystem.cpp, QDir/QDirListing). A Qt-free build has
// to supply the interface itself — this is the minimum the engine needs
// (exists + readFile), on std::filesystem/fstream. Everything else reports
// NotSupported. Qrc-style ":/..." resource paths are remapped onto a real
// directory (resources/ in this repo), the same pattern as the fork's
// web/webfilesystem.h.

#include <filesystem>
#include <fstream>

#include "global/io/ifilesystem.h"

namespace sve {
class EngineFileSystem : public muse::io::IFileSystem
{
public:
    EngineFileSystem() = default;

    explicit EngineFileSystem(const std::string& resourceRoot)
        : m_resourceRoot(resourceRoot) {}

    muse::Ret exists(const muse::io::path_t& path) const override
    {
        std::error_code ec;
        bool ok = std::filesystem::exists(resolve(path), ec);
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
        std::ifstream f(resolve(filePath), std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            return muse::make_ret(muse::Ret::Code::UnknownError, "failed open " + filePath.toStdString());
        }
        std::streamsize size = f.tellg();
        f.seekg(0, std::ios::beg);
        data.resize(static_cast<size_t>(size));
        if (!f.read(reinterpret_cast<char*>(data.data()), size)) {
            return muse::make_ret(muse::Ret::Code::UnknownError, "failed read " + filePath.toStdString());
        }
        return muse::make_ret(muse::Ret::Code::Ok);
    }

    muse::RetVal<uint64_t> fileSize(const muse::io::path_t& path) const override
    {
        muse::RetVal<uint64_t> rv;
        std::error_code ec;
        uint64_t size = std::filesystem::file_size(resolve(path), ec);
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

    std::string resolve(const muse::io::path_t& path) const
    {
        const std::string& s = path.toStdString();
        if (!m_resourceRoot.empty() && s.rfind(":/", 0) == 0) {
            return m_resourceRoot + s.substr(1);
        }
        return s;
    }

    std::string m_resourceRoot;
};
}
