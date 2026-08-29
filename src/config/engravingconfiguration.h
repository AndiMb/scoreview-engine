#pragma once

// With ENGRAVING_NO_INTERNAL there is no implementation of
// IEngravingConfiguration (the upstream one depends on QPageSize and is
// dropped with the internals). This is the minimal Qt-free answer: constants,
// as a converter needs them. The only value with substance is defaultPageSize
// (A4 in inches, as QPageSize::size(A4, Inch) returns it).

#include "engraving/iengravingconfiguration.h"

namespace sve {
class EngravingConfiguration : public mu::engraving::IEngravingConfiguration
{
public:
    using Color = mu::engraving::Color;
    using SizeF = mu::engraving::SizeF;
    using String = muse::String;
    using path_t = muse::io::path_t;
    using voice_idx_t = mu::engraving::voice_idx_t;

    path_t appDataPath() const override { return path_t(); }

    path_t defaultStyleFilePath() const override { return m_defaultStyle; }
    void setDefaultStyleFilePath(const path_t& path) override { m_defaultStyle = path; }
    muse::async::Channel<path_t> defaultStyleFilePathChanged() const override { return m_pathCh; }

    path_t partStyleFilePath() const override { return m_partStyle; }
    void setPartStyleFilePath(const path_t& path) override { m_partStyle = path; }
    muse::async::Channel<path_t> partStyleFilePathChanged() const override { return m_pathCh; }

    SizeF defaultPageSize() const override { return SizeF(210.0 / 25.4, 297.0 / 25.4); }

    bool canLayoutIcons() const override { return false; }
    String iconsFontFamily() const override { return String(); }

    Color defaultColor() const override { return Color::BLACK; }
    Color scoreInversionColor() const override { return Color::WHITE; }
    Color lassoColor() const override { return Color(0, 0, 255); }
    Color warningColor() const override { return Color(255, 165, 0); }
    Color warningSelectedColor() const override { return Color(255, 165, 0); }
    Color criticalColor() const override { return Color(255, 0, 0); }
    Color criticalBackgroundColor() const override { return Color::WHITE; }
    Color criticalSelectedColor() const override { return Color(255, 0, 0); }
    Color thumbnailBackgroundColor() const override { return Color::WHITE; }
    Color noteBackgroundColor() const override { return Color::WHITE; }
    Color fontPrimaryColor() const override { return Color::BLACK; }
    Color voiceColor(voice_idx_t) const override { return Color::BLACK; }

    double guiScaling() const override { return 1.0; }

    Color selectionColor(voice_idx_t, bool, bool) const override { return Color(0, 120, 215); }
    void setSelectionColor(voice_idx_t, Color) override {}
    muse::async::Channel<voice_idx_t, Color> selectionColorChanged() const override { return m_selColCh; }

    bool dynamicsApplyToAllVoices() const override { return false; }
    void setDynamicsApplyToAllVoices(bool) override {}
    muse::async::Channel<bool> dynamicsApplyToAllVoicesChanged() const override { return m_boolCh; }

    bool autoUpdateFretboardDiagrams() const override { return false; }
    void setAutoUpdateFretboardDiagrams(bool) override {}
    muse::async::Channel<bool> autoUpdateFretboardDiagramsChanged() const override { return m_boolCh; }

    Color formattingColor() const override { return Color(128, 128, 128); }
    muse::async::Channel<Color> formattingColorChanged() const override { return m_colCh; }

    Color invisibleColor() const override { return Color(128, 128, 128); }
    muse::async::Channel<Color> invisibleColorChanged() const override { return m_colCh; }

    Color unlinkedColor() const override { return Color(255, 0, 0); }
    muse::async::Channel<Color> unlinkedColorChanged() const override { return m_colCh; }

    Color frameColor() const override { return Color(128, 128, 128); }
    muse::async::Channel<Color> frameColorChanged() const override { return m_colCh; }

    Color scoreGreyColor() const override { return Color(128, 128, 128); }

    Color highlightSelectionColor(voice_idx_t) const override { return Color(0, 120, 215); }

    const DebuggingOptions& debuggingOptions() const override { return m_debug; }
    void setDebuggingOptions(const DebuggingOptions& options) override { m_debug = options; }
    muse::async::Notification debuggingOptionsChanged() const override { return m_notif; }

    bool isAccessibleEnabled() const override { return false; }

    bool doNotSaveEIDsForBackCompat() const override { return false; }
    void setDoNotSaveEIDsForBackCompat(bool) override {}

    bool allowReadingImagesFromOutsideMscz() const override { return false; }

    bool guitarProImportExperimental() const override { return false; }
    bool negativeFretsAllowed() const override { return false; }
    void setGuitarProMultivoiceEnabled(bool) override {}
    bool guitarProMultivoiceEnabled() const override { return false; }
    bool minDistanceForPartialSkylineCalculated() const override { return false; }
    bool specificSlursLayoutWorkaround() const override { return false; }
    bool preferSameStringForTranspose() const override { return false; }
    void setPreferSameStringForTranspose(bool) override {}

    int maxScaledImageDim() const override { return 2048; }
    void setMaxScaledImageDim(int) override {}

private:
    path_t m_defaultStyle;
    path_t m_partStyle;
    DebuggingOptions m_debug;
    muse::async::Channel<path_t> m_pathCh;
    muse::async::Channel<voice_idx_t, Color> m_selColCh;
    muse::async::Channel<bool> m_boolCh;
    muse::async::Channel<Color> m_colCh;
    muse::async::Notification m_notif;
};
}
