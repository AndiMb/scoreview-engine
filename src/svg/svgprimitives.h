#pragma once

// The three pieces of SVG serialization that are pure functions of their
// input: a number, a text run, a blob of picture bytes. They used to be
// `static` inside drawdatasvg.cpp, which put them out of reach of a test —
// and they are the ones worth testing, because each of them decides whether
// the document a consumer receives parses at all.
//
// Header-only and free of engine types on purpose: tests/main.cpp links them
// without pulling in engraving, the fonts stack or msdfgen.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string>

#include "global/types/bytearray.h"

namespace sve {
//! Number formatting: enough precision for 1/100 px at page scale, no trailing
//! zeros — keeps files small and diffs readable.
inline std::string fmt(double v)
{
    // A degenerate coordinate must not make the whole page unparseable:
    // "nan"/"inf" are not SVG numbers, and %.3f of a huge double would overrun
    // the buffer and be truncated into a DIFFERENT number. Both collapse to 0,
    // which costs one element and keeps the document valid.
    if (!std::isfinite(v)) {
        return "0";
    }
    if (std::abs(v) < 1e-9) {
        return "0";
    }
    char buf[64];
    int written = std::snprintf(buf, sizeof(buf), "%.3f", v);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(buf)) {
        return "0";
    }
    std::string s(buf);
    while (!s.empty() && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s;
}

//! The five XML entities, and the control characters XML cannot carry at all.
inline std::string xmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default:
            // XML 1.0 has no spelling for these, escaped or not - a parser
            // must reject the whole document, so one stray byte would cost the
            // page rather than the character. Tab, LF and CR are the three
            // control characters that are legal and stay. The text here is the
            // score's title, so it is not ours to vouch for.
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            out += c;
            break;
        }
    }
    return out;
}

//! Standard base64 with padding (RFC 4648), straight into the stream — the
//! payload of an <image> data URI, so it is the largest thing in a page that
//! carries a picture.
inline void base64(std::ostream& os, const muse::ByteArray& data)
{
    static const char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t* p = data.constData();
    const size_t size = data.size();
    char quad[4];
    size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        uint32_t v = uint32_t(p[i]) << 16 | uint32_t(p[i + 1]) << 8 | p[i + 2];
        quad[0] = TABLE[(v >> 18) & 0x3F];
        quad[1] = TABLE[(v >> 12) & 0x3F];
        quad[2] = TABLE[(v >> 6) & 0x3F];
        quad[3] = TABLE[v & 0x3F];
        os.write(quad, 4);
    }
    if (i < size) {
        const bool twoBytes = i + 1 < size;
        uint32_t v = uint32_t(p[i]) << 16 | (twoBytes ? uint32_t(p[i + 1]) << 8 : 0);
        quad[0] = TABLE[(v >> 18) & 0x3F];
        quad[1] = TABLE[(v >> 12) & 0x3F];
        quad[2] = twoBytes ? TABLE[(v >> 6) & 0x3F] : '=';
        quad[3] = '=';
        os.write(quad, 4);
    }
}
}
