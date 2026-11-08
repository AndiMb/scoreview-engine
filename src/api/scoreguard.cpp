#include "scoreguard.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>

#include <zlib.h>

#include "global/types/bytearray.h"
#include "platform/cryptographichash.h"

using std::string_view;

namespace sve::guard {
namespace {
bool fail(Report& r, std::string why)
{
    if (r.ok) {
        r.ok = false;
        r.reason = std::move(why);
    }
    return false;
}

// Strict non-negative decimal: optional surrounding whitespace, digits only.
// An entity, a sign or anything else fails - the upstream reader would decode
// "&#48;" to "0", and a check that did not would be beside the point.
bool parseCount(string_view s, long& out)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    if (b == e || e - b > 9) {
        return false;
    }
    long v = 0;
    for (size_t i = b; i < e; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10 + (s[i] - '0');
    }
    out = v;
    return true;
}

bool validVersion(string_view v)
{
    // "4.70", "3.02", "1.14": what MscLoader::readMasterScore splits at the
    // dot and indexes as sl[0] and sl[1] without counting them (issue #37).
    const size_t dot = v.find('.');
    if (dot == string_view::npos || dot == 0 || dot > 3 || v.size() - dot - 1 == 0 || v.size() - dot - 1 > 3) {
        return false;
    }
    for (size_t i = 0; i < v.size(); ++i) {
        if (i != dot && (v[i] < '0' || v[i] > '9')) {
            return false;
        }
    }
    return true;
}

bool endsWith(string_view s, string_view suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ---------------------------------------------------------------------------
// XML. Not a parser - pugixml does that, later, and rejects what is not
// well-formed. This walks the tags of a document to see the shape the
// readers will recurse over and the few values they trust.

class XmlScan
{
public:
    XmlScan(const char* data, size_t size, Report& r)
        : m_p(data), m_end(data + size), m_r(r) {}

    void run()
    {
        while (m_r.ok && m_p < m_end) {
            const char* lt = static_cast<const char*>(std::memchr(m_p, '<', m_end - m_p));
            if (!lt) {
                break;
            }
            m_p = lt;
            if (startsWith("<!--")) {
                if (!skipPast("-->") || !noCapture("a comment")) {
                    return;
                }
            } else if (startsWith("<![CDATA[")) {
                if (!noCapture("CDATA") || !skipPast("]]>")) {
                    return;
                }
            } else if (startsWith("<?")) {
                if (!skipPast("?>")) {
                    return;
                }
            } else if (startsWith("<!")) {
                // DOCTYPE and friends: no score file has one.
                fail(m_r, "markup declaration in score XML");
                return;
            } else if (startsWith("</")) {
                endTag(lt);
            } else {
                startTag();
            }
        }
    }

private:
    enum class Capture { None, Repeat, ImagePath };

    bool startsWith(const char* s) const
    {
        const size_t n = std::strlen(s);
        return static_cast<size_t>(m_end - m_p) >= n && std::memcmp(m_p, s, n) == 0;
    }

    bool skipPast(const char* s)
    {
        const size_t n = std::strlen(s);
        const char* q = std::search(m_p, m_end, s, s + n);
        if (q == m_end) {
            return fail(m_r, "unterminated markup in score XML");
        }
        m_p = q + n;
        return true;
    }

    bool noCapture(const char* what)
    {
        if (m_capture != Capture::None) {
            return fail(m_r, std::string(what) + " inside a value the engine checks");
        }
        return true;
    }

    static bool nameChar(char c)
    {
        return !(std::isspace(static_cast<unsigned char>(c)) || c == '/' || c == '>' || c == '=');
    }

    string_view readName()
    {
        const char* b = m_p;
        while (m_p < m_end && nameChar(*m_p)) {
            ++m_p;
        }
        return string_view(b, m_p - b);
    }

    void skipSpace()
    {
        while (m_p < m_end && std::isspace(static_cast<unsigned char>(*m_p))) {
            ++m_p;
        }
    }

    void endTag(const char* lt)
    {
        m_p += 2;
        readName();
        const char* gt = static_cast<const char*>(std::memchr(m_p, '>', m_end - m_p));
        if (!gt) {
            fail(m_r, "unterminated end tag in score XML");
            return;
        }
        m_p = gt + 1;
        if (m_capture != Capture::None) {
            endCapture(string_view(m_captureStart, lt - m_captureStart));
            m_capture = Capture::None;
        }
        if (m_stack.empty()) {
            return;     // not well-formed; pugixml will say so
        }
        closeElement(m_stack.back());
        m_stack.pop_back();
    }

    void startTag()
    {
        if (!noCapture("an element")) {
            return;
        }
        ++m_p;
        const string_view name = readName();
        std::vector<std::pair<string_view, string_view> > attrs;
        bool selfClosing = false;
        for (;;) {
            skipSpace();
            if (m_p >= m_end) {
                fail(m_r, "unterminated start tag in score XML");
                return;
            }
            if (*m_p == '>') {
                ++m_p;
                break;
            }
            if (*m_p == '/') {
                ++m_p;
                if (m_p >= m_end || *m_p != '>') {
                    fail(m_r, "malformed start tag in score XML");
                    return;
                }
                ++m_p;
                selfClosing = true;
                break;
            }
            const string_view an = readName();
            skipSpace();
            if (an.empty() || m_p >= m_end || *m_p != '=') {
                fail(m_r, "malformed attribute in score XML");
                return;
            }
            ++m_p;
            skipSpace();
            if (m_p >= m_end || (*m_p != '"' && *m_p != '\'')) {
                fail(m_r, "malformed attribute in score XML");
                return;
            }
            const char q = *m_p++;
            const char* vb = m_p;
            const char* ve = static_cast<const char*>(std::memchr(m_p, q, m_end - m_p));
            if (!ve) {
                fail(m_r, "unterminated attribute in score XML");
                return;
            }
            m_p = ve + 1;
            attrs.emplace_back(an, string_view(vb, ve - vb));
        }
        openElement(name, attrs, selfClosing);
        if (!m_r.ok) {
            return;
        }
        if (selfClosing) {
            closeElement(name);
        } else {
            m_stack.push_back(name);
        }
    }

    static const string_view* attr(const std::vector<std::pair<string_view, string_view> >& attrs, string_view n)
    {
        for (const auto& a : attrs) {
            if (a.first == n) {
                return &a.second;
            }
        }
        return nullptr;
    }

    void openElement(string_view name, const std::vector<std::pair<string_view, string_view> >& attrs, bool selfClosing)
    {
        const string_view parent = m_stack.empty() ? string_view() : m_stack.back();

        // Issue #29: the readers recurse once per level of nesting on a fixed
        // wasm stack, and running out of it is a trap no handler can catch.
        if (static_cast<int>(m_stack.size()) + 1 > MAX_ELEMENT_DEPTH) {
            fail(m_r, "score XML nested too deeply");
            return;
        }

        if (name == "museScore") {
            if (const string_view* v = attr(attrs, "version")) {
                if (!validVersion(*v)) {
                    fail(m_r, "malformed museScore version");      // issue #37
                    return;
                }
                m_r.versionMajors.push_back(std::atoi(std::string(*v).c_str()));
            }
            m_staffScopes.push_back(0);
        } else if (name == "Score") {
            const long nesting = std::count(m_stack.begin(), m_stack.end(), string_view("Score")) + 1;
            if (nesting > MAX_SCORE_NESTING) {
                fail(m_r, "<Score> nested too deeply");           // issue #29
                return;
            }
            m_staffScopes.push_back(0);
        } else if (name == "Staff") {
            if (parent == "Part") {
                if (!m_staffScopes.empty()) {
                    ++m_staffScopes.back();
                }
            } else if (const string_view* id = attr(attrs, "id")) {
                // Content staff: must be one the parts defined (issue #36).
                long n = 0;
                const long defined = m_staffScopes.empty() ? 0 : m_staffScopes.back();
                if (!parseCount(*id, n) || n < 1 || n > defined) {
                    fail(m_r, "<Staff id> outside the staves the parts define");
                    return;
                }
            }
        } else if (name == "clef" || name == "concertClef" || name == "transposingClef") {
            if (const string_view* s = attr(attrs, "staff")) {
                long n = 0;
                if (!parseCount(*s, n) || n < 1 || n > MAX_CLEF_STAFF) {
                    fail(m_r, "clef staff index out of range");    // issue #30
                    return;
                }
            }
        } else if (name == "endRepeat") {
            if (!selfClosing) {
                beginCapture(Capture::Repeat);                     // issue #33
            }
        } else if (name == "path" && parent == "Image") {
            if (!selfClosing) {
                beginCapture(Capture::ImagePath);                  // issue #34
            }
        }

        if (endsWith(name, "Segment")) {
            if (const string_view* no = attr(attrs, "no")) {
                long n = 0;
                if (!parseCount(*no, n) || n > MAX_SEGMENT_NO) {
                    fail(m_r, "spanner segment number out of range"); // issue #31
                    return;
                }
            }
        }
    }

    void closeElement(string_view name)
    {
        if ((name == "museScore" || name == "Score") && !m_staffScopes.empty()) {
            m_staffScopes.pop_back();
        }
    }

    void beginCapture(Capture c)
    {
        m_capture = c;
        m_captureStart = m_p;
    }

    void endCapture(string_view text)
    {
        if (m_capture == Capture::Repeat) {
            long n = 0;
            if (!parseCount(text, n) || n > MAX_REPEAT_COUNT) {
                fail(m_r, "repeat count out of range");
                return;
            }
            m_repeatTotal += n;
            if (m_repeatTotal > MAX_REPEAT_TOTAL) {
                fail(m_r, "too many repeats in total");
            }
        } else if (m_capture == Capture::ImagePath) {
            if (text.find('&') != string_view::npos) {
                fail(m_r, "escaped image path");
                return;
            }
            m_r.imageRefs.emplace_back(text);
        }
    }

    const char* m_p;
    const char* m_end;
    Report& m_r;
    std::vector<string_view> m_stack;
    std::vector<long> m_staffScopes;   // staves defined by <Part>s, per <museScore>/<Score>
    long m_repeatTotal = 0;
    Capture m_capture = Capture::None;
    const char* m_captureStart = nullptr;
};

// ---------------------------------------------------------------------------
// Zip. Mirrors what ZipContainer (musescore/src/framework/global/
// serialization/internal/zipcontainer.cpp) reads: the end-of-directory record
// found by the same backwards scan, sizes from the central directory, the
// compression method from the local header. Issue #40: that code trusts the
// sizes, so everything it will trust is checked here first, and every deflate
// stream is inflated once, into nothing or into a bounded buffer, to see that
// it really produces what its header says.

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

std::string fixPath(const uint8_t* p, size_t n)
{
    std::string s(reinterpret_cast<const char*>(p), n);
    std::replace(s.begin(), s.end(), '\\', '/');
    for (bool changed = true; changed;) {
        changed = false;
        if (!s.empty() && s.front() == '/') {
            s.erase(0, 1);
            changed = true;
        } else if (s.rfind("./", 0) == 0) {
            s.erase(0, 2);
            changed = true;
        } else if (s.rfind("../", 0) == 0) {
            s.erase(0, 3);
            changed = true;
        }
    }
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Inflates a raw deflate stream. Keeps the output only when `out` is given;
// fails as soon as it would exceed `declared`, and unless it ends at exactly
// `declared` bytes.
bool inflateChecked(const uint8_t* in, size_t inSize, uint32_t declared, std::vector<uint8_t>* out, Report& r)
{
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return fail(r, "zlib initialisation failed");
    }
    zs.next_in = const_cast<Bytef*>(in);
    zs.avail_in = static_cast<uInt>(inSize);
    uint8_t buf[16384];
    uint64_t produced = 0;
    int res = Z_OK;
    while (res != Z_STREAM_END) {
        zs.next_out = buf;
        zs.avail_out = sizeof(buf);
        res = ::inflate(&zs, Z_NO_FLUSH);
        if (res != Z_OK && res != Z_STREAM_END) {
            inflateEnd(&zs);
            return fail(r, "corrupt or truncated zip entry");
        }
        const size_t got = sizeof(buf) - zs.avail_out;
        produced += got;
        if (produced > declared) {
            inflateEnd(&zs);
            return fail(r, "zip entry inflates beyond its declared size");
        }
        if (out) {
            out->insert(out->end(), buf, buf + got);
        }
        if (res == Z_OK && got == 0 && zs.avail_in == 0) {
            inflateEnd(&zs);
            return fail(r, "corrupt or truncated zip entry");
        }
    }
    inflateEnd(&zs);
    if (produced != declared) {
        return fail(r, "zip entry size does not match its header");
    }
    return true;
}

void checkZip(const uint8_t* d, size_t size, Report& r)
{
    if (size < 22 || u32(d) != 0x04034b50) {
        fail(r, "not a zip file");
        return;
    }
    size_t eod = 0;
    bool found = false;
    for (size_t i = 0; i <= 65535 && size >= 22 + i; ++i) {
        if (u32(d + size - 22 - i) == 0x06054b50) {
            eod = size - 22 - i;
            found = true;
            break;
        }
    }
    if (!found) {
        fail(r, "zip end-of-directory record not found");
        return;
    }
    const size_t entries = u16(d + eod + 10);
    size_t cd = u32(d + eod + 16);
    if (entries > MAX_ZIP_ENTRIES) {
        fail(r, "too many zip entries");
        return;
    }

    uint64_t total = 0;
    CryptographicHash md4;
    for (size_t i = 0; i < entries && r.ok; ++i) {
        if (cd > size || size - cd < 46 || u32(d + cd) != 0x02014b50) {
            fail(r, "malformed zip central directory");
            return;
        }
        const uint8_t* h = d + cd;
        const uint16_t versionNeeded = u16(h + 6);
        const uint16_t flags = u16(h + 8);
        const uint32_t csize = u32(h + 20);
        const uint32_t usize = u32(h + 24);
        const size_t nameLen = u16(h + 28);
        const size_t next = 46 + nameLen + u16(h + 30) + u16(h + 32);
        const size_t local = u32(h + 42);
        if (size - cd < next) {
            fail(r, "malformed zip central directory");
            return;
        }
        const std::string name = fixPath(h + 46, nameLen);
        cd += next;

        // ZipContainer reads both sizes into an int (issue #40).
        if (csize > 0x7fffffffu || usize > 0x7fffffffu || usize > MAX_ENTRY_SIZE) {
            fail(r, "zip entry too large");
            return;
        }
        total += usize;
        if (total > MAX_TOTAL_INFLATED) {
            fail(r, "zip contents too large");
            return;
        }
        if (local > size || size - local < 30 || u32(d + local) != 0x04034b50) {
            fail(r, "malformed zip local header");
            return;
        }
        const size_t dataStart = local + 30 + u16(d + local + 26) + u16(d + local + 28);
        if (dataStart > size || size - dataStart < csize) {
            fail(r, "zip entry data outside the file");
            return;
        }
        if ((flags & 1) || versionNeeded > 20) {
            continue;   // ZipContainer will not extract it at all
        }

        const std::string lname = lower(name);
        const bool xml = endsWith(lname, ".mscx") || endsWith(lname, ".mss");
        const bool picture = name.rfind("Pictures/", 0) == 0;
        const uint16_t method = u16(d + local + 8);
        std::vector<uint8_t> content;
        if (method == 0) {
            if (xml || picture) {
                const size_t n = std::min<size_t>(csize, usize);
                content.assign(d + dataStart, d + dataStart + n);
            }
        } else if (method == 8) {
            if (!inflateChecked(d + dataStart, csize, usize, (xml || picture) ? &content : nullptr, r)) {
                return;
            }
        } else {
            continue;   // unsupported: ZipContainer returns nothing for it
        }

        if (xml) {
            checkXml(reinterpret_cast<const char*>(content.data()), content.size(), r);
        } else if (picture) {
            muse::ByteArray ba(content.data(), content.size());
            const muse::ByteArray h16 = md4.hash(ba, muse::ICryptographicHash::Algorithm::Md4);
            static const char* D = "0123456789abcdef";
            std::string hex;
            for (size_t k = 0; k < h16.size(); ++k) {
                hex += D[h16[k] >> 4];
                hex += D[h16[k] & 15];
            }
            r.pictureHashes.push_back(hex);
        }
    }
}
}

void checkXml(const char* data, size_t size, Report& report)
{
    XmlScan(data, size, report).run();
}

Report checkBytes(const std::string& suffix, const uint8_t* data, size_t size)
{
    Report r;
    if (size > MAX_FILE_SIZE) {
        fail(r, "score file too large");
    } else if (suffix == "mscz") {
        checkZip(data, size, r);
    } else {
        checkXml(reinterpret_cast<const char*>(data), size, r);
    }
    return r;
}

Report checkFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Report r;
        fail(r, "cannot read score file");
        return r;
    }
    const std::streamoff size = f.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > MAX_FILE_SIZE) {
        Report r;
        fail(r, "score file too large");
        return r;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    const std::string suffix = lower(path.substr(path.find_last_of('.') + 1));
    return checkBytes(suffix, buf.data(), buf.size());
}
}
