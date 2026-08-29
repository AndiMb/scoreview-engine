/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

// shadow copy of src/importexport/midi/internal/midiexport/exportmidi.h —
// diffs against upstream, marked "shadow diff" below: the QString/QFile
// convenience overloads are not carried over (callers write the device
// themselves), and the device is muse::io::IODevice.

#include "midifile.h"                                    // shadow diff: the shadow copy, was "../midishared/midifile.h"
#include "engraving/compat/midi/pausemap.h"
#include "engraving/compat/midi/compatmidirenderinternal.h"

namespace mu::engraving {
class Score;
class TempoMap;
class SynthesizerState;
}

namespace mu::iex::midi {
//---------------------------------------------------------
//   ExportMidi
//---------------------------------------------------------

class ExportMidi
{
public:
    ExportMidi(engraving::Score* s) { m_score = s; }
    bool write(muse::io::IODevice* device, bool midiExpandRepeats, bool exportRPNs);   // shadow diff: was QIODevice*
    bool write(muse::io::IODevice* device, bool midiExpandRepeats, bool exportRPNs, const engraving::SynthesizerState& synthState);

private:
    void writeHeader(const engraving::CompatMidiRendererInternal::Context& context);

    MidiFile m_midiFile;
    engraving::Score* m_score = nullptr;
};
}
