// mscz2media — native CLI of the Qt-free engine.
//
// Loads a .mscz/.mscx, lays it out, and writes per-page DrawData JSON.
// SVG (Phase 3), MIDI, spos/mpos and metadata JSON follow; the CLI is the
// native twin of the future wasm build and produces the same outputs.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "config/engravinginit.h"

#include "bufferedpaintprovider.h"
#include "painter.h"
#include "utils/drawdatajson.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/page.h"
#include "engraving/rendering/score/scorerenderer.h"
#include "utils/scorerw.h"

using namespace muse;
using namespace muse::draw;
using namespace mu::engraving;

static int usage()
{
    std::printf("usage: mscz2media <score.mscz|mscx> --resources <dir> [--out <dir>] [--drawdata]\n"
                "\n"
                "  --resources <dir>  directory standing in for the qrc \":/\" tree (repo: resources/)\n"
                "  --out <dir>        output directory (default: .)\n"
                "  --drawdata         write page-<n>.drawdata.json per page\n");
    return 2;
}

int main(int argc, char** argv)
{
    std::string scorePath, resourceRoot, outDir = ".";
    bool wantDrawData = false;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--resources") && i + 1 < argc) {
            resourceRoot = argv[++i];
        } else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) {
            outDir = argv[++i];
        } else if (!std::strcmp(argv[i], "--drawdata")) {
            wantDrawData = true;
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

    MasterScore* score = ScoreRW::readScore(String::fromStdString(scorePath), true);
    if (!score) {
        std::fprintf(stderr, "mscz2media: failed to read %s\n", scorePath.c_str());
        return 1;
    }

    std::printf("pages=%zu measures=%zu tracks=%zu\n",
                score->npages(), score->nmeasures(), score->ntracks());

    if (wantDrawData) {
        std::filesystem::create_directories(outDir);
        for (size_t pageNo = 0; pageNo < score->npages(); ++pageNo) {
            auto provider = std::make_shared<BufferedPaintProvider>();
            {
                Painter painter(provider, "mscz2media");
                rendering::score::ScoreRenderer renderer;
                rendering::IScoreRenderer::ScorePaintOptions opt;
                opt.deviceDpi = 360;
                opt.fromPage = static_cast<int>(pageNo);
                opt.toPage = static_cast<int>(pageNo);
                opt.printPageBackground = false;
                renderer.paintScore(&painter, score, opt);
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
