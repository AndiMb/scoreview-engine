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

// shadow copy of src/importexport/midi/internal/midishared/midifile.h —
// diffs against upstream, each marked "shadow diff" below:
//   * QIODevice -> muse::io::IODevice, qint64 -> int64_t
//   * Qt's global uchar typedef supplied for midievent.h, which is included
//     unmodified from the submodule
//   * the MIDI *import* path (read/readTrack/readEvent/getvl and its running
//     state) is not carried over — this build only exports

#include <vector>
#include <cstdint>

#include "global/io/iodevice.h"        // shadow diff: was <QIODevice>
using uchar = unsigned char;           // shadow diff: Qt supplied this typedef

#include "importexport/midi/internal/midishared/midievent.h"  // shadow diff: was "../midishared/midievent.h"

namespace mu::iex::midi {
class MidiTrack
{
    std::multimap<int, MidiEvent> _events;
    int _outChannel;
    int _outPort;
    bool _drumTrack;

public:
    MidiTrack();
    ~MidiTrack();

    bool empty() const;
    const std::multimap<int, MidiEvent>& events() const { return _events; }
    std::multimap<int, MidiEvent>& events() { return _events; }

    int outChannel() const { return _outChannel; }
    void setOutChannel(int n);
    int outPort() const { return _outPort; }
    void setOutPort(int n) { _outPort = n; }

    bool drumTrack() const { return _drumTrack; }

    void insert(int tick, const MidiEvent&);
    void mergeNoteOnOff();
};

//---------------------------------------------------------
//   MidiFile
//---------------------------------------------------------

class MidiFile
{
    muse::io::IODevice* fp;            // shadow diff: was QIODevice*
    std::vector<MidiTrack> _tracks;
    int _division;
    bool _isDivisionInTps;         ///< ticks per second, alternative - ticks per beat
    int _format;                 ///< midi file format (0-2)
    bool _noRunningStatus;       ///< do not use running status on output

    int status;                  ///< running status

    void writeEvent(const MidiEvent& event);

protected:
    // write
    bool write(const void*, int64_t);  // shadow diff: was qint64
    void writeShort(int);
    void writeLong(int);
    bool writeTrack(const MidiTrack&);
    void putvl(unsigned);
    void put(unsigned char c) { write(&c, 1); }
    void writeStatus(int type, int channel);

    void resetRunningStatus() { status = -1; }

public:
    MidiFile();
    bool write(muse::io::IODevice*);   // shadow diff: was QIODevice*

    std::vector<MidiTrack>& tracks() { return _tracks; }
    const std::vector<MidiTrack>& tracks() const { return _tracks; }

    int format() const { return _format; }
    void setFormat(int fmt) { _format = fmt; }

    int division() const { return _division; }
    bool isDivisionInTps() const { return _isDivisionInTps; }
    void setDivision(int val) { _division = val; }
    void separateChannel();
};
}
