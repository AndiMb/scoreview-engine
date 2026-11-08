#pragma once

// Pre-read checks on an untrusted score, run before the upstream reader sees
// a single byte of it.
//
// The MuseScore submodule is never patched (src/shadow/README.md), and its
// readers trust the file: nesting depth, the zip header sizes and a handful
// of integer attributes go straight into recursion, allocation sizes and
// container indices. Each of those was an audit finding (issues #29-#40).
// Rather than shadow-copying several thousand lines of reader code, the
// engine looks at the file first and refuses one that steps outside what any
// real score does. The bounds are generous: the 2,900 scores in the
// submodule's test and vtest corpora stay far inside every one of them (the
// numbers are next to each limit below).
//
// This is a pure function of the file's bytes: no engine state, no IOC, so
// tests/main.cpp exercises it directly. The checks that need engine state
// (the global image store) live in scoreloader.cpp and use what this returns.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sve::guard {
// Largest observed in the corpora in brackets.
constexpr size_t MAX_FILE_SIZE = 256u << 20;          // whole .mscz/.mscx (6 MB)
constexpr uint32_t MAX_ENTRY_SIZE = 64u << 20;        // one zip entry, inflated (6 MB)
constexpr uint64_t MAX_TOTAL_INFLATED = 256u << 20;   // all zip entries together (6 MB)
constexpr size_t MAX_ZIP_ENTRIES = 4096;
constexpr int MAX_ELEMENT_DEPTH = 64;                 // (14)
constexpr int MAX_SCORE_NESTING = 4;                  // <Score> inside <Score> (2)
constexpr int MAX_CLEF_STAFF = 64;                    // <clef staff="n"> (3)
constexpr int MAX_SEGMENT_NO = 1000;                  // <SlurSegment no="n"> etc. (1)
constexpr int MAX_REPEAT_COUNT = 100;                 // one <endRepeat> (8)
constexpr long MAX_REPEAT_TOTAL = 4096;               // all <endRepeat> of a file (128)

struct Report {
    bool ok = true;
    std::string reason;                       // set when !ok

    // Major version of every <museScore version="x.y"> seen (main score and
    // excerpt files alike).
    std::vector<int> versionMajors;

    // Lower-case hex MD4 of the bytes of each Pictures/ entry of the
    // container: what the store will file them under.
    std::vector<std::string> pictureHashes;

    // The 32-hex store name every <Image><path> refers to (other names cannot
    // resolve from the store, see ImageStore::getImage).
    std::vector<std::string> imageRefs;
};

// Checks the score file at `path` (.mscz or .mscx by its suffix).
Report checkFile(const std::string& path);

// The same on bytes already in memory; `suffix` is "mscz" or "mscx".
Report checkBytes(const std::string& suffix, const uint8_t* data, size_t size);

// One XML document (an .mscx or .mss), accumulating into `report`.
void checkXml(const char* data, size_t size, Report& report);
}
