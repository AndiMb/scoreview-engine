// mscz2media — native CLI of the Qt-free engine.
//
// Loads a .mscz/.mscx, lays it out and writes the requested outputs: SVG
// pages, MIDI, spos/mpos, metadata JSON, and per-page DrawData JSON for
// diagnostics. The native twin of the wasm build — same loader, same
// writers, so the two produce the same bytes.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "config/engravinginit.h"

#include "bufferedpaintprovider.h"
#include "painter.h"
#include "utils/drawdatajson.h"

#include "global/io/buffer.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/page.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/tempo.h"
#include "engraving/rendering/score/scorerenderer.h"
#include "api/scoreloader.h"
#include "meta/scoremeta.h"
#include "positions/positionswriter.h"
#include "shadow/exportmidi.h"
#include "svg/svgwriter.h"
#include "view/scoreview.h"

using namespace muse;
using namespace muse::draw;
using namespace mu::engraving;

static int usage()
{
    std::printf("usage: mscz2media <score.mscz|mscx> --resources <dir> [--out <dir>] [outputs...]\n"
                "\n"
                "  --resources <dir>  directory standing in for the qrc \":/\" tree (repo: resources/)\n"
                "  --out <dir>        output directory (default: .)\n"
                "  --midi             write score.mid (repeats expanded, RPNs exported)\n"
                "  --spos             write spos.json (segment positions + playback events)\n"
                "  --mpos             write mpos.json (measure positions + playback events)\n"
                "  --meta             write meta.json (score metadata; tracks always empty)\n"
                "  --svg              write page-<n>.svg per page (text as glyph outlines)\n"
                "  --drawdata         write page-<n>.drawdata.json per page (diagnostics)\n"
                "  --dump-playback    print tempomap and expanded repeat list (diagnostics)\n"
                "\n"
                "views (every output except --midi then describes the view):\n"
                "  --excerpt <n>      a stored excerpt, 0-based as in meta.json \"excerpts\"\n"
                "  --part <ids>       generate an excerpt of the given part ids (comma-separated)\n"
                "  --view <json>      {\"format\":\"orig|phone|phonel\",\"concertPitch\":bool,\"transposeSemitones\":n}\n"
                "  --mapping          write mapping.json (baseElid, staffMap) for the view\n");
    return 2;
}

int main(int argc, char** argv)
{
    std::string scorePath, resourceRoot, outDir = ".";
    bool wantDrawData = false;
    bool wantMidi = false;
    bool wantSpos = false;
    bool wantMpos = false;
    bool wantMeta = false;
    bool wantSvg = false;
    bool wantDumpPlayback = false;
    bool wantMapping = false;
    bool isView = false;
    int excerptId = -1;
    std::string partIds, viewJson;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--resources") && i + 1 < argc) {
            resourceRoot = argv[++i];
        } else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) {
            outDir = argv[++i];
        } else if (!std::strcmp(argv[i], "--drawdata")) {
            wantDrawData = true;
        } else if (!std::strcmp(argv[i], "--midi")) {
            wantMidi = true;
        } else if (!std::strcmp(argv[i], "--spos")) {
            wantSpos = true;
        } else if (!std::strcmp(argv[i], "--mpos")) {
            wantMpos = true;
        } else if (!std::strcmp(argv[i], "--meta")) {
            wantMeta = true;
        } else if (!std::strcmp(argv[i], "--svg")) {
            wantSvg = true;
        } else if (!std::strcmp(argv[i], "--dump-playback")) {
            wantDumpPlayback = true;
        } else if (!std::strcmp(argv[i], "--excerpt") && i + 1 < argc) {
            excerptId = std::atoi(argv[++i]);
            isView = true;
        } else if (!std::strcmp(argv[i], "--part") && i + 1 < argc) {
            partIds = argv[++i];
            isView = true;
        } else if (!std::strcmp(argv[i], "--view") && i + 1 < argc) {
            viewJson = argv[++i];
            isView = true;
        } else if (!std::strcmp(argv[i], "--mapping")) {
            wantMapping = true;
        } else if (argv[i][0] == '-') {
            return usage();
        } else {
            scorePath = argv[i];
        }
    }
    if (scorePath.empty() || resourceRoot.empty()) {
        return usage();
    }

    if (!sve::initEngraving(resourceRoot)) {
        std::fprintf(stderr, "mscz2media: engine init failed (resources at '%s'?)\n", resourceRoot.c_str());
        return 1;
    }

    MasterScore* score = sve::loadScore(muse::io::path_t(scorePath));
    if (!score) {
        std::fprintf(stderr, "mscz2media: failed to read %s\n", scorePath.c_str());
        return 1;
    }

    // The view every output below describes. MIDI stays the main score's:
    // a view changes the picture, not the piece (see src/view/).
    MasterScore* master = score;
    Score* view = master;
    if (isView) {
        muse::String error;
        sve::ScoreViews::captureBase(master);
        if (!partIds.empty()) {
            std::vector<std::string> ids;
            size_t from = 0;
            for (;;) {
                const size_t comma = partIds.find(',', from);
                ids.push_back(partIds.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
                if (comma == std::string::npos) {
                    break;
                }
                from = comma + 1;
            }
            excerptId = sve::ScoreViews::generateExcerpt(master, ids, &error);
            if (excerptId < 0) {
                std::fprintf(stderr, "mscz2media: %s\n", error.toStdString().c_str());
                return 1;
            }
        }
        view = sve::ScoreViews::score(master, excerptId);
        if (!view) {
            std::fprintf(stderr, "mscz2media: no excerpt %d\n", excerptId);
            return 1;
        }
        sve::ViewOptions options;
        if (!sve::ScoreViews::parseOptions(viewJson, options, &error)
            || !sve::ScoreViews::apply(view, options, &error)) {
            std::fprintf(stderr, "mscz2media: %s\n", error.toStdString().c_str());
            return 1;
        }
    }

    std::printf("pages=%zu measures=%zu tracks=%zu\n",
                view->npages(), view->nmeasures(), view->ntracks());

    if (wantDumpPlayback) {
        std::printf("tempomap (multiplier=%f):\n", score->tempomap()->tempoMultiplier().val);
        for (const auto& e : *score->tempomap()) {
            std::printf("  tick=%d tempo=%f pause=%f type=%d\n",
                        e.first, e.second.tempo.val, e.second.pause, static_cast<int>(e.second.type));
        }
        std::printf("repeatList(expand=true):\n");
        for (const RepeatSegment* rs : score->repeatList(true)) {
            std::printf("  utick=%d tick=%d len=%d utime=%f timeOffset=%f\n",
                        rs->utick, rs->tick, rs->len(), rs->utime, rs->timeOffset);
        }
    }

    if (wantDrawData || wantMidi || wantSpos || wantMpos || wantMeta || wantSvg || wantMapping) {
        std::filesystem::create_directories(outDir);
    }

    auto writeBytes = [&](const std::string& name, const ByteArray& data) -> bool {
        std::string outPath = outDir + "/" + name;
        std::ofstream f(outPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.constData()), data.size());
        if (!f) {
            std::fprintf(stderr, "mscz2media: failed to write %s\n", outPath.c_str());
            return false;
        }
        std::printf("wrote %s (%zu bytes)\n", outPath.c_str(), data.size());
        return true;
    };

    if (wantSvg) {
        for (size_t pageNo = 0; pageNo < view->npages(); ++pageNo) {
            muse::ByteArray svg = sve::SvgWriter::write(view, pageNo);
            if (svg.empty()) {
                std::fprintf(stderr, "mscz2media: no SVG for page %zu\n", pageNo + 1);
                return 1;
            }
            if (!writeBytes("page-" + std::to_string(pageNo + 1) + ".svg", svg)) {
                return 1;
            }
        }
    }

    if (wantMeta) {
        if (!writeBytes("meta.json", sve::ScoreMeta::json(view))) {
            return 1;
        }
    }
    if (wantSpos) {
        sve::PositionsWriter writer(sve::PositionsWriter::ElementType::SEGMENT);
        if (!writeBytes("spos.json", writer.json(view))) {
            return 1;
        }
    }
    if (wantMpos) {
        sve::PositionsWriter writer(sve::PositionsWriter::ElementType::MEASURE);
        if (!writeBytes("mpos.json", writer.json(view, isView ? master : nullptr))) {
            return 1;
        }
    }

    if (wantMidi) {
        muse::io::Buffer buf;
        buf.open(muse::io::IODevice::ReadWrite);
        mu::iex::midi::ExportMidi exportMidi(score);
        // parameters as webmscore's saveMidi defaults: expand repeats, export RPNs
        exportMidi.write(&buf, true, true, score->synthesizerState());
        const muse::ByteArray& data = buf.data();
        if (data.empty()) {
            std::fprintf(stderr, "mscz2media: MIDI export produced no data\n");
            return 1;
        }
        if (!writeBytes("score.mid", data)) {
            return 1;
        }
    }

    if (wantMapping) {
        if (!writeBytes("mapping.json", sve::ScoreViews::mapping(master, view))) {
            return 1;
        }
    }

    if (wantDrawData) {
        for (size_t pageNo = 0; pageNo < view->npages(); ++pageNo) {
            auto provider = std::make_shared<BufferedPaintProvider>();
            {
                Painter painter(provider, "mscz2media");
                rendering::score::ScoreRenderer renderer;
                rendering::IScoreRenderer::ScorePaintOptions opt;
                opt.deviceDpi = 360;
                opt.fromPage = static_cast<int>(pageNo);
                opt.toPage = static_cast<int>(pageNo);
                opt.printPageBackground = false;
                renderer.paintScore(&painter, view, opt);
                painter.endDraw();
            }
            DrawDataPtr dd = provider->drawData();
            if (!dd) {
                std::fprintf(stderr, "mscz2media: no DrawData for page %zu\n", pageNo + 1);
                return 1;
            }
            ByteArray json = DrawDataJson::toJson(dd);
            std::string outPath = outDir + "/page-" + std::to_string(pageNo + 1) + ".drawdata.json";
            std::ofstream f(outPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(json.constData()), json.size());
            if (!f) {
                std::fprintf(stderr, "mscz2media: failed to write %s\n", outPath.c_str());
                return 1;
            }
            std::printf("wrote %s (%zu bytes)\n", outPath.c_str(), json.size());
        }
    }

    return 0;
}
