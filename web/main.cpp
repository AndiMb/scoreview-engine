// scoreview-engine wasm entry — the Qt-free successor of webmscore's
// web/main.cpp, exposing the same C ABI for the supported surface: load,
// SVG, MIDI, spos/mpos, metadata, destroy. Everything the Qt build did
// through Qt (audio, PNG/PDF, MusicXML, excerpts) is not exported; the JS
// wrapper throws before the boundary. Responses use the WasmRes layout the
// wrapper's helper.js reads (see web/wasmres.h).

#include <emscripten/emscripten.h>

#include <cstdio>
#include <fstream>
#include <set>
#include <string>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "global/io/buffer.h"
#include "modularity/ioc.h"
#include "types/fontstypes.h"
#include "ifontsdatabase.h"
#include "thirdparty/kors_logger/src/logger.h"
#include "log.h"

#include "engraving/dom/masterscore.h"
#include "engraving/types/constants.h"

#include "api/scoreloader.h"
#include "config/engravinginit.h"
#include "meta/scoremeta.h"
#include "positions/positionswriter.h"
#include "shadow/exportmidi.h"
#include "svg/svgwriter.h"

#include "./wasmres.h"

using namespace muse;
using namespace mu::engraving;
using sve::WasmRes;
using sve::WasmResBytes;

static std::set<MasterScore*> s_instances;
static bool s_inited = false;

static WasmRes notSupported(const char* what)
{
    return WasmRes::error(static_cast<int>(Ret::Code::NotSupported),
                          String(u"NotSupported: %1").arg(String::fromUtf8(what)));
}

static MasterScore* scoreOf(uintptr_t score_ptr)
{
    auto score = reinterpret_cast<MasterScore*>(score_ptr);
    return s_instances.count(score) ? score : nullptr;
}

static WasmRes badScorePtr()
{
    return WasmRes::error(static_cast<int>(Ret::Code::UnknownError), u"invalid score pointer");
}

extern "C" {
EMSCRIPTEN_KEEPALIVE
int version()
{
    return Constants::MSC_VERSION;
}

EMSCRIPTEN_KEEPALIVE
void setLogLevel(int level)
{
    kors::logger::Logger::instance()->setLevel(static_cast<kors::logger::Level>(level));
}

EMSCRIPTEN_KEEPALIVE
void init(int, char**)
{
    if (s_inited) {
        return;
    }
    // The preloaded package lands at /resources (see CMakeLists), standing in
    // for the qrc ":/" tree exactly as --resources does for the native CLI.
    if (!sve::initEngraving("/resources")) {
        LOGE() << "scoreview-engine: engine init failed";
        return;
    }
    s_inited = true;
}

EMSCRIPTEN_KEEPALIVE
bool addFont(const char* fontPath)
{
    // Extra fonts (webmscore's CJK path): register the file as a Text
    // substitution so FontsEngine falls back to it for glyphs the score
    // fonts lack. The only public route that also feeds the substitution
    // list is IFontsDatabase::addAdditionalFonts(dir), which reads a
    // fontslist.json manifest — so write a one-entry manifest next to the
    // font and point it there. Family name and style come from FreeType.
    if (!s_inited || !fontPath || !*fontPath) {
        return false;
    }

    static FT_Library ftlib = nullptr;
    if (!ftlib && FT_Init_FreeType(&ftlib) != 0) {
        return false;
    }

    FT_Face ftface = nullptr;
    if (FT_New_Face(ftlib, fontPath, 0, &ftface) != 0) {
        LOGE() << "addFont: FreeType cannot read " << fontPath;
        return false;
    }
    std::string family = ftface->family_name ? ftface->family_name : "";
    bool bold = (ftface->style_flags & FT_STYLE_FLAG_BOLD) != 0;
    bool italic = (ftface->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
    FT_Done_Face(ftface);
    if (family.empty()) {
        LOGE() << "addFont: font has no family name: " << fontPath;
        return false;
    }

    std::string path(fontPath);
    size_t slash = path.find_last_of('/');
    std::string dir = (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
    std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);

    // Minimal JSON escaping — family names carry letters/digits/spaces, but
    // a quote or backslash must not break the manifest.
    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
        return out;
    };

    {
        std::ofstream manifest(dir + "/fontslist.json", std::ios::trunc);
        manifest << "[{\"file\":\"" << escape(file) << "\",\"family\":\"" << escape(family)
                 << "\",\"bold\":" << (bold ? "true" : "false")
                 << ",\"italic\":" << (italic ? "true" : "false") << "}]";
        if (!manifest) {
            return false;
        }
    }

    muse::modularity::globalIoc()
        ->resolve<muse::draw::IFontsDatabase>("scoreview-engine")
        ->addAdditionalFonts(muse::io::path_t(dir));
    return true;
}

EMSCRIPTEN_KEEPALIVE
WasmResBytes load(const char* format, const char* data, const uint32_t size, bool doLayout)
{
    std::string suffix(format);
    if (suffix != "mscz" && suffix != "mscx") {
        return notSupported("only mscz/mscx input is supported");
    }

    // MEMFS temp file: cheap, and keeps the loader identical to the CLI's.
    static int counter = 0;
    std::string path = "/tmp/score-" + std::to_string(counter++) + "." + suffix;
    {
        std::ofstream f(path, std::ios::binary);
        f.write(data, size);
        if (!f) {
            return WasmRes::error(static_cast<int>(Ret::Code::InternalError), u"cannot write temp file");
        }
    }

    MasterScore* score = sve::loadScore(io::path_t(path), doLayout);
    std::remove(path.c_str());
    if (!score) {
        return WasmRes::error(static_cast<int>(Ret::Code::BadData), u"cannot load score");
    }

    s_instances.insert(score);
    return WasmRes(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(score)));
}

EMSCRIPTEN_KEEPALIVE
WasmResBytes title(uintptr_t score_ptr)
{
    MasterScore* score = scoreOf(score_ptr);
    if (!score) {
        return badScorePtr();
    }
    return WasmRes(sve::ScoreMeta::title(score));
}

EMSCRIPTEN_KEEPALIVE
WasmResBytes npages(uintptr_t score_ptr, int excerptId)
{
    MasterScore* score = scoreOf(score_ptr);
    if (!score) {
        return badScorePtr();
    }
    if (excerptId != -1) {
        return notSupported("excerpts");
    }
    return WasmRes(static_cast<uint32_t>(score->npages()));
}

EMSCRIPTEN_KEEPALIVE
WasmResBytes saveSvg(uintptr_t score_ptr, int pageNumber, bool /*drawPageBackground*/, int excerptId)
{
    MasterScore* score = scoreOf(score_ptr);
    if (!score) {
        return badScorePtr();
    }
    if (excerptId != -1) {
        return notSupported("excerpts");
    }
    ByteArray svg = sve::SvgWriter::write(score, static_cast<size_t>(pageNumber));
    if (svg.empty()) {
        return WasmRes::error(static_cast<int>(Ret::Code::UnknownError), u"no such page");
    }
    return WasmRes(svg);
}

EMSCRIPTEN_KEEPALIVE
WasmResBytes saveMidi(uintptr_t score_ptr, bool midiExpandRepeats, bool exportRPNs, int excerptId)
{
    MasterScore* score = scoreOf(score_ptr);
    if (!score) {
        return badScorePtr();
    }
    if (excerptId != -1) {
        return notSupported("excerpts");
    }
    io::Buffer buf;
    buf.open(io::IODevice::ReadWrite);
    mu::iex::midi::ExportMidi exportMidi(score);
    exportMidi.write(&buf, midiExpandRepeats, exportRPNs, score->synthesizerState());
    if (buf.data().empty()) {
        return WasmRes::error(static_cast<int>(Ret::Code::UnknownError), u"MIDI export produced no data");
    }
    return WasmRes(buf.data());
}

EMSCRIPTEN_KEEPALIVE
WasmResBytes savePositions(uintptr_t score_ptr, bool ofSegments, int excerptId)
{
    MasterScore* score = scoreOf(score_ptr);
    if (!score) {
        return badScorePtr();
    }
    if (excerptId != -1) {
        return notSupported("excerpts");
    }
    sve::PositionsWriter writer(ofSegments ? sve::PositionsWriter::ElementType::SEGMENT
                                           : sve::PositionsWriter::ElementType::MEASURE);
    return WasmRes(writer.json(score));
}

EMSCRIPTEN_KEEPALIVE
WasmResBytes saveMetadata(uintptr_t score_ptr)
{
    MasterScore* score = scoreOf(score_ptr);
    if (!score) {
        return badScorePtr();
    }
    return WasmRes(sve::ScoreMeta::json(score));
}

EMSCRIPTEN_KEEPALIVE
void destroy(uintptr_t score_ptr)
{
    MasterScore* score = scoreOf(score_ptr);
    if (!score) {
        return;
    }
    s_instances.erase(score);
    delete score;
}

EMSCRIPTEN_KEEPALIVE
void destroyAll()
{
    for (MasterScore* score : s_instances) {
        delete score;
    }
    s_instances.clear();
}
}
