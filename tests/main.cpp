// Unit tests for the pure functions of this engine — the ones the corpus gate
// cannot reach.
//
// The corpus gate is an end-to-end fingerprint comparison over 570 scores, so
// it covers exactly what those scores happen to contain. None of them carries
// a picture (see .github/workflows/build.yml), which left the header probe —
// the one loop in this repository that walks attacker-shaped bytes — tested by
// a single `grep -q` for a data URI. Everything below is what that gate is
// structurally unable to check: malformed input, formats no corpus score uses,
// the exact output of the three serialization primitives, and the read
// confinement, which no corpus score was ever written to probe.
//
// No test framework: this build vendors or pins every dependency it has, and
// a few dozen assertions do not need gtest to be linked, downloaded or
// version-watched. Failures print the file, the line and both values, which is
// what a framework would have given us here anyway.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "global/types/bytearray.h"

#include "image/imageformat.h"
#include "platform/cryptographichash.h"
#include "platform/enginefilesystem.h"
#include "svg/svgprimitives.h"

using muse::ByteArray;

static int g_failures = 0;
static int g_checks = 0;
static const char* g_group = "";

static void group(const char* name)
{
    g_group = name;
    std::printf("\n  %s\n", name);
}

static void fail(const char* what, int line, const std::string& got, const std::string& want)
{
    ++g_failures;
    std::printf("    FAIL  %s (line %d)\n          got  %s\n          want %s\n",
                what, line, got.c_str(), want.c_str());
}

#define CHECK_EQ(what, got, want)                                       \
    do {                                                                \
        ++g_checks;                                                     \
        auto _g = (got);                                                \
        auto _w = (want);                                               \
        if (!(_g == _w)) {                                              \
            fail(what, __LINE__, describe(_g), describe(_w));           \
        }                                                               \
    } while (0)

static std::string describe(const std::string& s) { return "\"" + s + "\""; }
static std::string describe(const char* s) { return describe(std::string(s)); }
static std::string describe(int v) { return std::to_string(v); }
static std::string describe(bool v) { return v ? "true" : "false"; }

static ByteArray bytes(std::initializer_list<int> vs)
{
    std::vector<uint8_t> v;
    for (int b : vs) {
        v.push_back(static_cast<uint8_t>(b));
    }
    return ByteArray(v.data(), v.size());
}

static ByteArray bytes(const std::string& s)
{
    return ByteArray(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

static std::string hex(const ByteArray& b)
{
    static const char* D = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < b.size(); ++i) {
        out += D[b.constData()[i] >> 4];
        out += D[b.constData()[i] & 0xF];
    }
    return out;
}

// ---------------------------------------------------------------------------
// imageformat.cpp — the header probe
// ---------------------------------------------------------------------------

//! A minimal but real PNG header: signature, then an IHDR chunk carrying the
//! size as two big-endian u32.
static ByteArray png(uint32_t w, uint32_t h)
{
    std::vector<uint8_t> v = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
                               0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    for (int s : { 24, 16, 8, 0 }) {
        v.push_back(static_cast<uint8_t>(w >> s));
    }
    for (int s : { 24, 16, 8, 0 }) {
        v.push_back(static_cast<uint8_t>(h >> s));
    }
    return ByteArray(v.data(), v.size());
}

//! SOI, an APP0 segment of `app0Len` bytes, then an SOF0 holding the geometry.
//! Height precedes width in a JPEG frame header, which is the detail worth a
//! test of its own.
static ByteArray jpeg(uint16_t w, uint16_t h, uint8_t sofMarker = 0xC0, int app0Len = 16)
{
    std::vector<uint8_t> v = { 0xFF, 0xD8 };
    v.push_back(0xFF);
    v.push_back(0xE0);
    v.push_back(static_cast<uint8_t>(app0Len >> 8));
    v.push_back(static_cast<uint8_t>(app0Len & 0xFF));
    for (int i = 0; i < app0Len - 2; ++i) {
        v.push_back(0);
    }
    v.push_back(0xFF);
    v.push_back(sofMarker);
    v.push_back(0);
    v.push_back(11);           // segment length
    v.push_back(8);            // sample precision
    v.push_back(static_cast<uint8_t>(h >> 8));
    v.push_back(static_cast<uint8_t>(h & 0xFF));
    v.push_back(static_cast<uint8_t>(w >> 8));
    v.push_back(static_cast<uint8_t>(w & 0xFF));
    v.push_back(1);            // component count
    v.push_back(1);
    v.push_back(0x11);
    v.push_back(0);
    return ByteArray(v.data(), v.size());
}

static ByteArray gif(const char* sig, uint16_t w, uint16_t h)
{
    std::vector<uint8_t> v(sig, sig + 6);
    v.push_back(static_cast<uint8_t>(w & 0xFF));
    v.push_back(static_cast<uint8_t>(w >> 8));
    v.push_back(static_cast<uint8_t>(h & 0xFF));
    v.push_back(static_cast<uint8_t>(h >> 8));
    v.push_back(0);
    return ByteArray(v.data(), v.size());
}

//! BITMAPINFOHEADER (headerSize 40) unless `headerSize` says otherwise; the
//! v12 BITMAPCOREHEADER stores the size as two u16 instead of two i32.
static ByteArray bmp(int32_t w, int32_t h, uint32_t headerSize = 40)
{
    std::vector<uint8_t> v = { 'B', 'M' };
    v.resize(14, 0);
    auto put32 = [&v](uint32_t x) {
        for (int s : { 0, 8, 16, 24 }) {
            v.push_back(static_cast<uint8_t>(x >> s));
        }
    };
    auto put16 = [&v](uint16_t x) {
        v.push_back(static_cast<uint8_t>(x & 0xFF));
        v.push_back(static_cast<uint8_t>(x >> 8));
    };
    put32(headerSize);
    if (headerSize == 12) {
        put16(static_cast<uint16_t>(w));
        put16(static_cast<uint16_t>(h));
        v.resize(26, 0);
    } else {
        put32(static_cast<uint32_t>(w));
        put32(static_cast<uint32_t>(h));
        v.resize(54, 0);
    }
    return ByteArray(v.data(), v.size());
}

static void testImageFormat()
{
    group("imageformat: the formats a browser can show");

    sve::ImageFormat f = sve::probeImage(png(640, 480));
    CHECK_EQ("png media type", std::string(f.mediaType ? f.mediaType : ""), std::string("image/png"));
    CHECK_EQ("png width", f.width, 640);
    CHECK_EQ("png height", f.height, 480);

    f = sve::probeImage(jpeg(1024, 768));
    CHECK_EQ("jpeg media type", std::string(f.mediaType ? f.mediaType : ""), std::string("image/jpeg"));
    // Height sits before width in a SOFn. Swapping them is the classic bug
    // here, and it would make every embedded JPEG render at the wrong aspect.
    CHECK_EQ("jpeg width", f.width, 1024);
    CHECK_EQ("jpeg height", f.height, 768);

    f = sve::probeImage(gif("GIF87a", 12, 34));
    CHECK_EQ("gif87a media type", std::string(f.mediaType ? f.mediaType : ""), std::string("image/gif"));
    CHECK_EQ("gif87a width", f.width, 12);
    CHECK_EQ("gif87a height", f.height, 34);

    f = sve::probeImage(gif("GIF89a", 300, 200));
    CHECK_EQ("gif89a width", f.width, 300);
    CHECK_EQ("gif89a height", f.height, 200);

    f = sve::probeImage(bmp(50, 60));
    CHECK_EQ("bmp media type", std::string(f.mediaType ? f.mediaType : ""), std::string("image/bmp"));
    CHECK_EQ("bmp width", f.width, 50);
    CHECK_EQ("bmp height", f.height, 60);

    // A negative height means the rows are stored top-down. The picture is as
    // tall either way, and the SVG has no way to say "upside down" anyway.
    f = sve::probeImage(bmp(50, -60));
    CHECK_EQ("bmp top-down height", f.height, 60);

    f = sve::probeImage(bmp(7, 9, 12));
    CHECK_EQ("bmp core header width", f.width, 7);
    CHECK_EQ("bmp core header height", f.height, 9);

    group("imageformat: what must be refused");

    CHECK_EQ("empty input", sve::probeImage(ByteArray()).isKnown(), false);
    CHECK_EQ("one byte", sve::probeImage(bytes({ 0x89 })).isKnown(), false);
    CHECK_EQ("text", sve::probeImage(bytes(std::string("not a picture at all"))).isKnown(), false);

    // TIFF and SVG: MuseScore accepts both, a browser shows neither, so the
    // probe has to call them unknown and let the crossed-box placeholder in.
    CHECK_EQ("tiff (II)", sve::probeImage(bytes({ 'I', 'I', 0x2A, 0 })).isKnown(), false);
    CHECK_EQ("tiff (MM)", sve::probeImage(bytes({ 'M', 'M', 0, 0x2A })).isKnown(), false);
    CHECK_EQ("svg", sve::probeImage(bytes(std::string("<svg xmlns=\"...\"></svg>"))).isKnown(), false);

    // A zero dimension is a header that parsed but cannot be drawn with: the
    // provider divides a target size by it.
    CHECK_EQ("png 0x0", sve::probeImage(png(0, 0)).isKnown(), false);
    CHECK_EQ("png 0 wide", sve::probeImage(png(0, 10)).isKnown(), false);
    CHECK_EQ("gif 0 high", sve::probeImage(gif("GIF89a", 10, 0)).isKnown(), false);

    // A width past INT32_MAX reads back negative. Rejecting is what keeps it
    // out of the <image> width attribute.
    CHECK_EQ("png width overflows int", sve::probeImage(png(0x80000000u, 10)).isKnown(), false);
    // INT32_MIN has no positive counterpart; negating it is undefined, so the
    // probe leaves it at zero and the dimension check below rejects it.
    CHECK_EQ("bmp INT32_MIN height", sve::probeImage(bmp(10, INT32_MIN)).isKnown(), false);

    group("imageformat: truncation must not read past the end");

    // Every prefix of a valid header, for each format. Two things are being
    // asked, and neither of them is "a short file must be unrecognizable" —
    // a BMP header is complete after 26 bytes and stays valid no matter what
    // was cut off behind it, so recognizing one is the correct answer:
    //
    //  * no prefix may touch a byte it does not own. That is what the
    //    sanitizer build turns into a failure (-DSVE_SANITIZE=ON), and it is
    //    the reason this loop walks every length rather than a few;
    //  * a prefix that IS recognized has to agree with the whole file. The
    //    geometry lives in the header, so any prefix long enough to carry it
    //    must yield the same numbers — different ones would mean the probe
    //    read something that merely happened to be in range.
    const struct { ByteArray data; int w; int h; const char* name; } truncs[] = {
        { png(640, 480), 640, 480, "png" },
        { jpeg(1024, 768), 1024, 768, "jpeg" },
        { gif("GIF89a", 10, 20), 10, 20, "gif" },
        { bmp(50, 60), 50, 60, "bmp" },
    };
    for (const auto& t : truncs) {
        for (size_t n = 0; n < t.data.size(); ++n) {
            ByteArray cut(t.data.constData(), n);
            sve::ImageFormat probed = sve::probeImage(cut);
            ++g_checks;
            if (probed.isKnown() && (probed.width != t.w || probed.height != t.h)) {
                char what[96];
                std::snprintf(what, sizeof(what), "truncated %s at %zu bytes disagrees", t.name, n);
                fail(what, __LINE__,
                     std::to_string(probed.width) + "x" + std::to_string(probed.height),
                     std::to_string(t.w) + "x" + std::to_string(t.h));
            }
        }
    }

    // Below the bytes the format needs, nothing can be identified.
    CHECK_EQ("png cut inside the signature",
             sve::probeImage(ByteArray(png(8, 8).constData(), 7)).isKnown(), false);
    CHECK_EQ("png cut before IHDR is complete",
             sve::probeImage(ByteArray(png(8, 8).constData(), 23)).isKnown(), false);
    CHECK_EQ("gif cut inside the signature",
             sve::probeImage(ByteArray(gif("GIF89a", 8, 8).constData(), 5)).isKnown(), false);
    CHECK_EQ("gif cut before the size",
             sve::probeImage(ByteArray(gif("GIF89a", 8, 8).constData(), 9)).isKnown(), false);
    CHECK_EQ("bmp cut before the header is complete",
             sve::probeImage(ByteArray(bmp(8, 8).constData(), 25)).isKnown(), false);

    group("imageformat: the JPEG segment walk");

    // A fill byte (0xFF) before a marker is legal and must not derail the walk.
    {
        std::vector<uint8_t> v = { 0xFF, 0xD8, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00 };
        std::vector<uint8_t> tail = { 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x01, 0x2C, 0x00, 0xC8, 0x01, 0x01, 0x11, 0x00 };
        v.insert(v.end(), tail.begin(), tail.end());
        sve::ImageFormat probed = sve::probeImage(ByteArray(v.data(), v.size()));
        CHECK_EQ("jpeg with fill bytes: width", probed.width, 200);
        CHECK_EQ("jpeg with fill bytes: height", probed.height, 300);
    }

    // DHT (C4), JPG (C8) and DAC (CC) sit inside the SOF marker range and are
    // NOT frame headers. Reading geometry out of one yields nonsense.
    for (uint8_t marker : { 0xC4, 0xC8, 0xCC }) {
        ByteArray b = jpeg(100, 200, marker);
        char what[64];
        std::snprintf(what, sizeof(what), "jpeg marker %02X is not a frame header", marker);
        ++g_checks;
        if (sve::probeImage(b).isKnown()) {
            fail(what, __LINE__, "known", "unknown");
        }
    }

    // SOF1, SOF2 (progressive) and SOF15 all carry the geometry.
    for (uint8_t marker : { 0xC1, 0xC2, 0xCF }) {
        ByteArray b = jpeg(320, 240, marker);
        sve::ImageFormat probed = sve::probeImage(b);
        char what[64];
        std::snprintf(what, sizeof(what), "jpeg marker %02X carries geometry", marker);
        ++g_checks;
        if (probed.width != 320 || probed.height != 240) {
            fail(what, __LINE__,
                 std::to_string(probed.width) + "x" + std::to_string(probed.height), "320x240");
        }
    }

    // A zero-length segment would leave the cursor where it is: the walk must
    // give up rather than spin.
    CHECK_EQ("jpeg zero segment length",
             sve::probeImage(bytes({ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00 })).isKnown(), false);
    // A length that runs off the end must not be followed.
    CHECK_EQ("jpeg segment length past the end",
             sve::probeImage(bytes({ 0xFF, 0xD8, 0xFF, 0xE0, 0xFF, 0xFF, 0x00, 0x00 })).isKnown(), false);
    // Bytes that are not on a marker at all.
    CHECK_EQ("jpeg desynced",
             sve::probeImage(bytes({ 0xFF, 0xD8, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 })).isKnown(), false);
}

// ---------------------------------------------------------------------------
// cryptographichash.cpp — MD4, the content id of an .mscz Pictures/ entry
// ---------------------------------------------------------------------------

static void testMd4()
{
    group("md4: the RFC 1320 test suite");

    // A wrong digest here does not corrupt anything - it makes every embedded
    // picture unresolvable, because the name of a Pictures/ entry IS this value.
    const sve::CryptographicHash h;
    struct { const char* in; const char* want; } cases[] = {
        { "", "31d6cfe0d16ae931b73c59d7e0c089c0" },
        { "a", "bde52cb31de33e46245e05fbdbd6fb24" },
        { "abc", "a448017aaf21d8525fc10ae87aa6729d" },
        { "message digest", "d9130a8164549fe818874806e1c7014b" },
        { "abcdefghijklmnopqrstuvwxyz", "d79e1c308aa5bbcdeea8ed63df412da9" },
        { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
          "043f8582f241db351ce627e153e7f0e4" },
        { "123456789012345678901234567890123456789012345678901234567890"
          "12345678901234567890", "e33b4ddc9c38f2199c3e7b164fcc0536" },
    };
    for (const auto& c : cases) {
        std::string label = std::string("md4(\"") + (std::strlen(c.in) > 24 ? "<long>" : c.in) + "\")";
        CHECK_EQ(label.c_str(),
                 hex(h.hash(bytes(std::string(c.in)), muse::ICryptographicHash::Algorithm::Md4)),
                 std::string(c.want));
    }

    // The padding branches: exactly at, one below and one above a block
    // boundary, and across the 56-byte mark where the length field goes into
    // the next block. These are where a hand-written padder gets it wrong.
    group("md4: padding around the block boundaries");
    for (size_t n : { 54u, 55u, 56u, 57u, 63u, 64u, 65u, 119u, 120u, 128u }) {
        ByteArray digest = h.hash(bytes(std::string(n, 'x')), muse::ICryptographicHash::Algorithm::Md4);
        char what[64];
        std::snprintf(what, sizeof(what), "md4 of %zu bytes is 16 bytes", n);
        CHECK_EQ(what, static_cast<int>(digest.size()), 16);
    }
}

// ---------------------------------------------------------------------------
// enginefilesystem.h — the read confinement
// ---------------------------------------------------------------------------

//! A throwaway tree on the real file system: the resource root, a file beside
//! it that no read may reach, and a sibling whose name shares the root's
//! prefix. Removed again by the destructor.
struct ScratchTree {
    std::filesystem::path base;
    std::filesystem::path resources;

    ScratchTree()
    {
        base = std::filesystem::temp_directory_path()
               / ("sve-tests-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        resources = base / "resources";
        std::filesystem::create_directories(resources / "engraving" / "styles");
        std::filesystem::create_directories(base / "resources-backup");
        std::filesystem::create_directories(base / "uploads");
        write(resources / "engraving" / "styles" / "chords.xml", "chords");
        write(base / "secret.txt", "secret");
        write(base / "resources-backup" / "old.xml", "old");
        write(base / "uploads" / "score.mscz", "score");
        write(base / "uploads" / "other.mscz", "other");
    }

    ~ScratchTree()
    {
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }

    static void write(const std::filesystem::path& p, const char* text)
    {
        std::ofstream(p, std::ios::binary) << text;
    }

    std::string at(const std::filesystem::path& rel) const { return (base / rel).generic_string(); }
};

//! What a read hands back: the file's text, or "<refused>" for any failure.
static std::string readText(const sve::EngineFileSystem& fs, const std::string& path)
{
    ByteArray data;
    if (!fs.readFile(muse::io::path_t(path), data)) {
        return "<refused>";
    }
    return std::string(reinterpret_cast<const char*>(data.constData()), data.size());
}

static void testEngineFileSystem()
{
    group("EngineFileSystem: reads stay inside the opened roots");

    const ScratchTree t;
    sve::EngineFileSystem fs(t.resources.generic_string());

    // The qrc remap, which is what the class exists for.
    CHECK_EQ("qrc path reads", readText(fs, ":/engraving/styles/chords.xml"), std::string("chords"));
    CHECK_EQ("qrc path with a .. that stays inside",
             readText(fs, ":/engraving/styles/../styles/chords.xml"), std::string("chords"));
    CHECK_EQ("the resource file by its real path",
             readText(fs, t.at("resources/engraving/styles/chords.xml")), std::string("chords"));

    // chordDescriptionFile, as upstream's ChordList::read() builds it from a
    // relative style value: pasted behind ":/engraving/styles/" as it came.
    CHECK_EQ("style value climbing out of the qrc tree",
             readText(fs, ":/engraving/styles/../../../secret.txt"), std::string("<refused>"));
    CHECK_EQ("qrc root climbing out", readText(fs, ":/../secret.txt"), std::string("<refused>"));
    // The same style value written as an absolute path never reaches the
    // remap at all.
    CHECK_EQ("absolute path outside", readText(fs, t.at("secret.txt")), std::string("<refused>"));
    // "resources-backup" starts with "resources"; only the separator test
    // tells them apart.
    CHECK_EQ("sibling sharing the root's prefix",
             readText(fs, t.at("resources-backup/old.xml")), std::string("<refused>"));
    CHECK_EQ("exists() is fenced too", static_cast<bool>(fs.exists(muse::io::path_t(t.at("secret.txt")))), false);
    CHECK_EQ("fileSize() is fenced too",
             static_cast<bool>(fs.fileSize(muse::io::path_t(t.at("secret.txt"))).ret), false);
    CHECK_EQ("empty path", readText(fs, ""), std::string("<refused>"));

    // A directory inside the root: allowed, but not a file. On glibc the
    // open succeeds and the size query fails; that must be an error, never a
    // resize() to 2^64-1.
    CHECK_EQ("a directory is not a file", readText(fs, ":/engraving"), std::string("<refused>"));

    // allowRead() of a file opens that file and nothing beside it.
    fs.allowRead(muse::io::path_t(t.at("uploads/score.mscz")));
    CHECK_EQ("the opened score reads", readText(fs, t.at("uploads/score.mscz")), std::string("score"));
    CHECK_EQ("its neighbour stays shut", readText(fs, t.at("uploads/other.mscz")), std::string("<refused>"));
    CHECK_EQ("climbing out of an opened file",
             readText(fs, t.at("uploads/score.mscz/../other.mscz")), std::string("<refused>"));
    // A .mscx is read in Dir mode, and upstream's DirReader first asks
    // whether the score's directory exists. That has to answer yes without
    // opening the directory for reads.
    CHECK_EQ("exists() of the opened file's directory",
             static_cast<bool>(fs.exists(muse::io::path_t(t.at("uploads")))), true);
    CHECK_EQ("exists() of a neighbour stays shut",
             static_cast<bool>(fs.exists(muse::io::path_t(t.at("uploads/other.mscz")))), false);
    CHECK_EQ("the directory itself does not read", readText(fs, t.at("uploads")), std::string("<refused>"));

    // allowRead() of a directory opens what is under it - and still only that.
    fs.allowRead(muse::io::path_t(t.at("uploads")));
    CHECK_EQ("under an opened directory", readText(fs, t.at("uploads/other.mscz")), std::string("other"));
    CHECK_EQ("out of an opened directory", readText(fs, t.at("uploads/../secret.txt")), std::string("<refused>"));
    // A trailing separator on the root must not change what counts as inside.
    fs.allowRead(muse::io::path_t(t.at("resources-backup") + "/"));
    CHECK_EQ("root opened with a trailing slash",
             readText(fs, t.at("resources-backup/old.xml")), std::string("old"));
    CHECK_EQ("still nothing beside it", readText(fs, t.at("secret.txt")), std::string("<refused>"));
}

// ---------------------------------------------------------------------------
// svgprimitives.h — number, text and blob serialization
// ---------------------------------------------------------------------------

static void testFmt()
{
    group("fmt: SVG numbers");

    CHECK_EQ("integer", sve::fmt(12.0), std::string("12"));
    CHECK_EQ("one decimal", sve::fmt(1.5), std::string("1.5"));
    CHECK_EQ("three decimals", sve::fmt(1.2345), std::string("1.234"));
    CHECK_EQ("negative", sve::fmt(-8.25), std::string("-8.25"));
    CHECK_EQ("zero", sve::fmt(0.0), std::string("0"));

    // Below the printing threshold: a coordinate this small is zero, and
    // "0.000" would be three bytes of noise on every element.
    CHECK_EQ("denormal collapses", sve::fmt(1e-12), std::string("0"));
    CHECK_EQ("negative denormal collapses", sve::fmt(-1e-12), std::string("0"));

    // The ones that would otherwise produce a document no SVG parser accepts.
    CHECK_EQ("nan", sve::fmt(std::nan("")), std::string("0"));
    CHECK_EQ("inf", sve::fmt(1.0 / 0.0), std::string("0"));
    CHECK_EQ("-inf", sve::fmt(-1.0 / 0.0), std::string("0"));
    // A double large enough that "%.3f" overruns the 64-byte buffer. Truncated
    // it would be a valid-looking but DIFFERENT number, which is worse than 0.
    CHECK_EQ("overruns the buffer", sve::fmt(1e300), std::string("0"));
}

static void testXmlEscape()
{
    group("xmlEscape: the score's own text");

    CHECK_EQ("plain", sve::xmlEscape("Sonata No. 3"), std::string("Sonata No. 3"));
    CHECK_EQ("ampersand", sve::xmlEscape("Rodgers & Hart"), std::string("Rodgers &amp; Hart"));
    CHECK_EQ("angle brackets", sve::xmlEscape("<b>"), std::string("&lt;b&gt;"));
    CHECK_EQ("quote", sve::xmlEscape("say \"hi\""), std::string("say &quot;hi&quot;"));
    // The entity itself must not be double-escaped into something else.
    CHECK_EQ("already an entity", sve::xmlEscape("&amp;"), std::string("&amp;amp;"));

    // Control characters XML 1.0 cannot carry in any form. One of these in a
    // title used to make the whole page unparseable.
    //
    // Split string literals, not "a\x0cb": a hex escape in C++ is greedy and
    // swallows every hex digit that follows, so "a\x0cb" is the two characters
    // 'a' and 0x0CB — which is not what it looks like, and was wrong here
    // before the tests ran.
    CHECK_EQ("NUL dropped", sve::xmlEscape(std::string("a\0b", 3)), std::string("ab"));
    CHECK_EQ("form feed dropped", sve::xmlEscape("a\x0c" "b"), std::string("ab"));
    CHECK_EQ("escape dropped", sve::xmlEscape("a\x1b" "b"), std::string("ab"));
    CHECK_EQ("vertical tab dropped", sve::xmlEscape("a\x0b" "b"), std::string("ab"));
    CHECK_EQ("delete is not dropped", sve::xmlEscape("a\x7f" "b"), std::string("a\x7f" "b"));

    // The three that are legal and must survive.
    CHECK_EQ("tab kept", sve::xmlEscape("a\tb"), std::string("a\tb"));
    CHECK_EQ("newline kept", sve::xmlEscape("a\nb"), std::string("a\nb"));
    CHECK_EQ("carriage return kept", sve::xmlEscape("a\rb"), std::string("a\rb"));

    // UTF-8 continuation bytes are >= 0x80 and must pass through untouched;
    // treating char as signed is the trap here.
    CHECK_EQ("utf-8 survives", sve::xmlEscape("Dvořák"), std::string("Dvořák"));
}

static std::string b64(const std::string& in)
{
    std::ostringstream os;
    sve::base64(os, bytes(in));
    return os.str();
}

static void testBase64()
{
    group("base64: the data URI payload");

    // RFC 4648 section 10, which exercises all three padding cases.
    CHECK_EQ("empty", b64(""), std::string(""));
    CHECK_EQ("f", b64("f"), std::string("Zg=="));
    CHECK_EQ("fo", b64("fo"), std::string("Zm8="));
    CHECK_EQ("foo", b64("foo"), std::string("Zm9v"));
    CHECK_EQ("foob", b64("foob"), std::string("Zm9vYg=="));
    CHECK_EQ("fooba", b64("fooba"), std::string("Zm9vYmE="));
    CHECK_EQ("foobar", b64("foobar"), std::string("Zm9vYmFy"));

    // High bytes: a picture is binary, and a signed char would index the
    // table out of bounds or produce the wrong glyph.
    {
        std::ostringstream os;
        sve::base64(os, bytes({ 0xFF, 0xFE, 0xFD }));
        CHECK_EQ("high bytes", os.str(), std::string("//79"));
    }
    {
        std::ostringstream os;
        sve::base64(os, bytes({ 0x00, 0x00, 0x00 }));
        CHECK_EQ("zero bytes", os.str(), std::string("AAAA"));
    }
    // Length is always a multiple of four, for every input length.
    for (size_t n = 0; n <= 32; ++n) {
        std::ostringstream os;
        sve::base64(os, bytes(std::string(n, '\xA5')));
        char what[64];
        std::snprintf(what, sizeof(what), "%zu bytes encode to a multiple of 4", n);
        CHECK_EQ(what, static_cast<int>(os.str().size() % 4), 0);
    }
}

int main()
{
    std::printf("scoreview-engine unit tests\n");

    testImageFormat();
    testMd4();
    testEngineFileSystem();
    testFmt();
    testXmlEscape();
    testBase64();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
