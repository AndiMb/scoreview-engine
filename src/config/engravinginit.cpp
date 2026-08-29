#include "engravinginit.h"

#include "modularity/ioc.h"

#include "platform/enginefilesystem.h"
#include "config/engravingconfiguration.h"

#include "types/fontstypes.h"
#include "internal/fontsdatabase.h"
#include "internal/fontsengine.h"
#include "internal/fontprovider.h"

#include "engraving/internal/engravingfontsprovider.h"
#include "engraving/infrastructure/smufl.h"
#include "engraving/style/defaultstyle.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/drumset.h"
#include "engraving/dom/figuredbass.h"
#include "engraving/rendering/score/scorerenderer.h"

using namespace muse;
using namespace muse::draw;
using namespace mu::engraving;

static const char* MODULE_NAME = "scoreview-engine";

bool sve::initEngraving(const std::string& resourceRoot)
{
    auto ioc = modularity::globalIoc();
    modularity::ContextPtr ctx;

    ioc->registerExport<io::IFileSystem>(MODULE_NAME, new EngineFileSystem(resourceRoot));

    FontsDatabase* fdb = new FontsDatabase();
    ioc->registerExport<IFontsDatabase>(MODULE_NAME, fdb);
    ioc->registerExport<IFontsEngine>(MODULE_NAME, std::make_shared<FontsEngine>(ctx));
    ioc->registerExport<IFontProvider>(MODULE_NAME, std::make_shared<FontProvider>(ctx));

    auto engravingFonts = std::make_shared<EngravingFontsProvider>(ctx);
    ioc->registerExport<IEngravingFontsProvider>(MODULE_NAME, engravingFonts);

    auto conf = std::make_shared<EngravingConfiguration>();
    ioc->registerExport<IEngravingConfiguration>(MODULE_NAME, conf);

    ioc->registerExport<rendering::IScoreRenderer>(MODULE_NAME, new rendering::score::ScoreRenderer());

    // Fonts as in EngravingModule::onInit, trimmed to what conversion needs
    fdb->addFont(FontDataKey(u"Edwin", false, false), ":/fonts/edwin/Edwin-Roman.otf");
    fdb->addFont(FontDataKey(u"Edwin", false, true), ":/fonts/edwin/Edwin-Italic.otf");
    fdb->addFont(FontDataKey(u"Edwin", true, false), ":/fonts/edwin/Edwin-Bold.otf");
    fdb->addFont(FontDataKey(u"Edwin", true, true), ":/fonts/edwin/Edwin-BdIta.otf");

    fdb->addFont(FontDataKey(u"Bravura"), ":/fonts/bravura/Bravura.otf");
    engravingFonts->addInternalFont("Bravura", "Bravura", ":/fonts/bravura/Bravura.otf");
    fdb->addFont(FontDataKey(u"Bravura Text"), ":/fonts/bravura/BravuraText.otf");
    fdb->addFont(FontDataKey(u"Leland"), ":/fonts/leland/Leland.otf");
    engravingFonts->addInternalFont("Leland", "Leland", ":/fonts/leland/Leland.otf");
    fdb->addFont(FontDataKey(u"Leland Text"), ":/fonts/leland/LelandText.otf");

    fdb->addFont(FontDataKey(u"FreeSerif"), ":/fonts/FreeSerif.ttf");
    fdb->addFont(FontDataKey(u"FreeSerif", true, false), ":/fonts/FreeSerifBold.ttf");
    fdb->addFont(FontDataKey(u"FreeSerif", false, true), ":/fonts/FreeSerifItalic.ttf");
    fdb->addFont(FontDataKey(u"FreeSerif", true, true), ":/fonts/FreeSerifBoldItalic.ttf");
    fdb->addFont(FontDataKey(u"FreeSans"), ":/fonts/FreeSans.ttf");
    fdb->addFont(FontDataKey(u"MScoreTabulature"), ":/fonts/mscoreTab.ttf");
    fdb->addFont(FontDataKey(u"MscoreBC"), ":/fonts/mscore-BC.ttf");
    fdb->addFont(FontDataKey(u"Campania"), ":/fonts/campania/Campania.otf");

    fdb->setDefaultFont(Font::Type::Unknown, FontDataKey(u"Edwin"));
    fdb->setDefaultFont(Font::Type::Text, FontDataKey(u"Edwin"));
    fdb->setDefaultFont(Font::Type::Tablature, FontDataKey(u"FreeSerif"));
    fdb->setDefaultFont(Font::Type::MusicSymbolText, FontDataKey(u"Bravura Text"));
    fdb->setDefaultFont(Font::Type::MusicSymbol, FontDataKey(u"Bravura"));
    engravingFonts->setFallbackFont("Bravura");

    if (!Smufl::init()) {
        return false;
    }
    engravingFonts->loadAllFonts();
    if (!engravingFonts->fontByName("Leland")) {
        return false;
    }

    DefaultStyle::instance()->init(conf->defaultStyleFilePath(), conf->partStyleFilePath(), conf->defaultPageSize());
    StaffType::initStaffTypes(conf->defaultColor());
    Drumset::initDrumset();
    FiguredBass::readConfigFile(String());

    // Must come after StaffType::initStaffTypes — template reading resolves
    // staff type presets. Without the templates,
    // Instrument::setSingleNoteDynamicsFromTemplate falls back to true for
    // every instrument and the MIDI export grows CC events per note that the
    // Qt build (templates loaded) does not emit.
    if (!loadInstrumentTemplates(":/engraving/instruments/instruments.xml")) {
        return false;
    }

    return true;
}
