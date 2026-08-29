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

// shadow copy of src/importexport/midi/internal/midishared/midifile.cpp —
// diffs against upstream, marked "shadow diff" below: the fp plumbing moves
// from QIODevice to muse::io::IODevice, and the MIDI *import* path
// (read/readTrack/readEvent/readShort/readLong/getvl) is not carried over.

#include "midifile.h"

#include "containers.h"

#include "log.h"

using namespace mu::engraving;

namespace mu::iex::midi {
MidiFile::MidiFile()
{
    fp               = 0;
    _division        = 0;
    _isDivisionInTps = false;
    _format          = 1;
    _noRunningStatus = false;

    status           = 0;
}

//---------------------------------------------------------
//   write
//    returns true on error
//---------------------------------------------------------

bool MidiFile::write(muse::io::IODevice* out)  // shadow diff: was QIODevice*
{
    fp = out;
    write("MThd", 4);
    writeLong(6);                   // header len
    writeShort(_format);            // format
    writeShort(static_cast<int>(_tracks.size()));
    writeShort(_division);
    for (const auto& t: _tracks) {
        if (writeTrack(t)) {
            return true;
        }
    }
    return false;
}

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void MidiFile::writeEvent(const MidiEvent& event)
{
    switch (event.type()) {
    case ME_NOTEON:
        writeStatus(ME_NOTEON, event.channel());
        put(event.pitch());
        put(event.velo());
        break;

    case ME_NOTEOFF:
        writeStatus(ME_NOTEOFF, event.channel());
        put(event.pitch());
        put(event.velo());
        break;

    case ME_PITCHBEND:
        writeStatus(ME_PITCHBEND, event.channel());
        put(event.dataA());
        put(event.dataB());
        break;

    case ME_CONTROLLER:
        switch (event.controller()) {
        case CTRL_PROGRAM:
            writeStatus(ME_PROGRAM, event.channel());
            put(event.value() & 0x7f);
            break;
        case CTRL_PRESS:
            writeStatus(ME_AFTERTOUCH, event.channel());
            put(event.value() & 0x7f);
            break;
        default:
            writeStatus(ME_CONTROLLER, event.channel());
            put(event.controller());
            put(event.value() & 0x7f);
            break;
        }
        break;

    case ME_META:
        put(ME_META);
        put(event.metaType());
        // Don't null terminate text meta events
        if (event.metaType() >= 0x1 && event.metaType() <= 0x14) {
            putvl(event.len() - 1);
            write(event.edata(), event.len() - 1);
        } else {
            putvl(event.len());
            write(event.edata(), event.len());
        }
        resetRunningStatus();               // really ?!
        break;

    case ME_SYSEX:
        put(ME_SYSEX);
        putvl(event.len() + 1);            // including 0xf7
        write(event.edata(), event.len());
        put(ME_ENDSYSEX);
        resetRunningStatus();
        break;
    }
}

//---------------------------------------------------------
//   writeTrack
//---------------------------------------------------------

bool MidiFile::writeTrack(const MidiTrack& t)
{
    write("MTrk", 4);
    int64_t lenpos = fp->pos();  // shadow diff: was qint64
    writeLong(0);                   // dummy len

    status   = -1;
    int tick = 0;
    for (const auto& i : t.events()) {
        int ntick = i.first;
        putvl(ntick - tick);        // write tick delta
        //
        // if track channel != -1, then use this
        //    channel for all events in this track
        //
        if (t.outChannel() != -1) {
            writeEvent(i.second);
        }
        tick = ntick;
    }

    //---------------------------------------------------
    //    write "End Of Track" Meta
    //    write Track Len
    //

    putvl(1);
    put(0xff);          // Meta
    put(0x2f);          // EOT
    putvl(0);           // len 0
    int64_t endpos = fp->pos();  // shadow diff: was qint64
    fp->seek(lenpos);
    writeLong(endpos - lenpos - 4); // tracklen
    fp->seek(endpos);
    return false;
}

//---------------------------------------------------------
//   writeStatus
//---------------------------------------------------------

void MidiFile::writeStatus(int nstat, int c)
{
    nstat |= (c & 0xf);
    //
    //  running status; except for Sysex- and Meta Events
    //
    if (_noRunningStatus || (((nstat & 0xf0) != 0xf0) && (nstat != status))) {
        status = nstat;
        put(nstat);
    }
}

//---------------------------------------------------------
//   write
//---------------------------------------------------------

bool MidiFile::write(const void* p, int64_t len)
{
    // shadow diff: IODevice writes uint8_t and reports errors as std::string
    size_t rv = fp->write(reinterpret_cast<const uint8_t*>(p), static_cast<size_t>(len));
    if (static_cast<int64_t>(rv) == len) {
        return false;
    }
    LOGD("write midifile failed: %s", fp->errorString().c_str());
    return true;
}

//---------------------------------------------------------
//   writeShort
//---------------------------------------------------------

void MidiFile::writeShort(int i)
{
    // shadow diff: was fp->putChar
    put(static_cast<unsigned char>(i >> 8));
    put(static_cast<unsigned char>(i));
}

//---------------------------------------------------------
//   writeLong
//---------------------------------------------------------

void MidiFile::writeLong(int i)
{
    // shadow diff: was fp->putChar
    put(static_cast<unsigned char>(i >> 24));
    put(static_cast<unsigned char>(i >> 16));
    put(static_cast<unsigned char>(i >> 8));
    put(static_cast<unsigned char>(i));
}

/*---------------------------------------------------------
 *    putvl
 *    Write variable-length number (7 bits per byte, MSB first)
 *---------------------------------------------------------*/

void MidiFile::putvl(unsigned val)
{
    unsigned long buf = val & 0x7f;
    while ((val >>= 7) > 0) {
        buf <<= 8;
        buf |= 0x80;
        buf += (val & 0x7f);
    }
    for (;;) {
        put(buf);
        if (buf & 0x80) {
            buf >>= 8;
        } else {
            break;
        }
    }
}

//---------------------------------------------------------
//   MidiTrack
//---------------------------------------------------------

MidiTrack::MidiTrack()
{
    _outChannel = -1;
    _outPort    = -1;
    _drumTrack  = false;
}

MidiTrack::~MidiTrack()
{
}

//---------------------------------------------------------
//   insert
//---------------------------------------------------------

void MidiTrack::insert(int tick, const MidiEvent& event)
{
    _events.insert({ tick, event });
}

void MidiTrack::setOutChannel(int n)
{
    _outChannel = n;
    if (_outChannel == 9) {
        _drumTrack = true;
    }
}

//---------------------------------------------------------
//   mergeNoteOnOffAndFindMidiType
//    - find matching note on / note off events and merge
//      into a note event with tick duration
//---------------------------------------------------------

void MidiTrack::mergeNoteOnOff()
{
    std::multimap<int, MidiEvent> el;

    int hbank = 0xff;
    int lbank = 0xff;
    int rpnh  = -1;
    int rpnl  = -1;
    int datah = 0;
    int datal = 0;
    int dataType = 0;   // 0 : disabled, 0x20000 : rpn, 0x30000 : nrpn;

    for (auto i = _events.begin(); i != _events.end(); ++i) {
        MidiEvent& ev = i->second;
        if (ev.type() == ME_INVALID) {
            continue;
        }
        if ((ev.type() != ME_NOTEON) && (ev.type() != ME_NOTEOFF)) {
            if (ev.type() == ME_CONTROLLER) {
                int val  = ev.value();
                int cn   = ev.controller();
                if (cn == CTRL_HBANK) {
                    hbank = val;
                    ev.setType(ME_INVALID);
                    continue;
                } else if (cn == CTRL_LBANK) {
                    lbank = val;
                    ev.setType(ME_INVALID);
                    continue;
                } else if (cn == CTRL_HDATA) {
                    datah = val;

                    // check if a CTRL_LDATA follows
                    // e.g. wie have a 14 bit controller:
                    auto ii = i;
                    ++ii;
                    bool found = false;
                    for (; ii != _events.end(); ++ii) {
                        MidiEvent& ev1 = ii->second;
                        if (ev1.type() == ME_CONTROLLER) {
                            if (ev1.controller() == CTRL_LDATA) {
                                // handle later
                                found = true;
                            }
                            break;
                        }
                    }
                    if (!found) {
                        if (rpnh == -1 || rpnl == -1) {
                            LOGD("parameter number not defined, data 0x%x", datah);
                            ev.setType(ME_INVALID);
                            continue;
                        } else {
                            ev.setController(dataType | (rpnh << 8) | rpnl);
                            ev.setValue(datah);
                        }
                    } else {
                        ev.setType(ME_INVALID);
                        continue;
                    }
                } else if (cn == CTRL_LDATA) {
                    datal = val;

                    if (rpnh == -1 || rpnl == -1) {
                        LOGD("parameter number not defined, data 0x%x 0x%x, tick %d, channel %d",
                             datah, datal, i->first, ev.channel());
                        continue;
                    }
                    // assume that the sequence is always
                    //    CTRL_HDATA - CTRL_LDATA
                    // eg. that LDATA is always send last

                    // 14 Bit RPN/NRPN
                    ev.setController((dataType + 0x30000) | (rpnh << 8) | rpnl);
                    ev.setValue((datah << 7) | datal);
                } else if (cn == CTRL_HRPN) {
                    rpnh = val;
                    dataType = 0x20000;
                    ev.setType(ME_INVALID);
                    continue;
                } else if (cn == CTRL_LRPN) {
                    rpnl = val;
                    dataType = 0x20000;
                    ev.setType(ME_INVALID);
                    continue;
                } else if (cn == CTRL_HNRPN) {
                    rpnh = val;
                    dataType = 0x30000;
                    ev.setType(ME_INVALID);
                    continue;
                } else if (cn == CTRL_LNRPN) {
                    rpnl = val;
                    dataType = 0x30000;
                    ev.setType(ME_INVALID);
                    continue;
                } else if (cn == CTRL_PROGRAM) {
                    ev.setValue((hbank << 16) | (lbank << 8) | ev.value());
                    // TODO el.insert(ev);
                    ev.setType(ME_INVALID);
                    continue;
                }
            } else if (ev.type() == ME_SYSEX) {
                int len = ev.len();
                const uchar* buffer = ev.edata();
                // Yamaha
                if (buffer[0] == 0x43) {
                    int type   = buffer[1] & 0xf0;
                    if (type == 0x10) {
                        if (len == 7 && buffer[2] == 0x4c && buffer[3] == 0x08 && buffer[5] == 7) {
                            // part mode
                            // 0 - normal
                            // 1 - DRUM
                            // 2 - DRUM 1
                            // 3 - DRUM 2
                            // 4 - DRUM 3
                            // 5 - DRUM 4
                            if (buffer[6] != 0 && buffer[4] == ev.channel()) {
                                _drumTrack = true;
                            }
                            ev.setType(ME_INVALID);
                            continue;
                        }
                    }
                }
            }
            el.insert(std::pair<int, MidiEvent>(i->first, ev));
            ev.setType(ME_INVALID);
            continue;
        }
        int tick = i->first;
        if (ev.type() == ME_NOTEOFF || ev.velo() == 0) {
            LOGD("-extra note off at %d", tick);
            ev.setType(ME_INVALID);
            continue;
        }
        MidiEvent note(ME_NOTE, ev.channel(), ev.dataA(), ev.dataB());

        auto k = i;
        ++k;
        for (; k != _events.end(); ++k) {
            MidiEvent& e = k->second;
            if (e.type() != ME_NOTEON && e.type() != ME_NOTEOFF) {
                continue;
            }
            if ((e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0))
                && (e.pitch() == note.pitch())) {
                int t = k->first - tick;
                if (t <= 0) {
                    t = 1;
                }
                note.setLen(t);
                e.setType(ME_INVALID);
                break;
            }
        }
        if (k == _events.end()) {
            LOGD("-no note-off for note at %d", tick);
            note.setLen(1);
        }
        el.insert(std::pair<int, MidiEvent>(tick, note));
        ev.setType(ME_INVALID);
    }
    _events = el;
}

//---------------------------------------------------------
//   separateChannel
//    if a track contains events for different midi channels,
//    then split events into separate tracks
//---------------------------------------------------------

void MidiFile::separateChannel()
{
    for (size_t i = 0; i < _tracks.size(); ++i) {
        // create a list of channels used in current track
        std::vector<int> channel;
        MidiTrack& midiTrack = _tracks[i];          // current track
        for (const auto& ie : midiTrack.events()) {
            const MidiEvent& e = ie.second;
            if (e.isChannelEvent() && !muse::contains(channel, static_cast<int>(e.channel()))) {
                channel.push_back(e.channel());
            }
        }
        midiTrack.setOutChannel(channel.empty() ? 0 : channel[0]);
        size_t nn = channel.size();
        if (nn <= 1) {
            continue;
        }
        std::sort(channel.begin(), channel.end());
        // -- split --
        // insert additional tracks, assign to them found channels
        for (size_t ii = 1; ii < nn; ++ii) {
            MidiTrack t;
            t.setOutChannel(channel[ii]);
            _tracks.insert(_tracks.begin() + i + ii, t);
        }

        //! NOTE: Midi track memory area may be invalid after inserting new elements into tracks
        //!       Let's get the actual track data again
        MidiTrack& actualMidiTrack = _tracks[i];

        // extract all different channel events from current track to inserted tracks
        for (auto ie = actualMidiTrack.events().begin(); ie != actualMidiTrack.events().end();) {
            const MidiEvent& e = ie->second;
            if (e.isChannelEvent()) {
                int ch  = e.channel();
                size_t idx = muse::indexOf(channel, ch);
                MidiTrack& t = _tracks[i + idx];
                if (&t != &actualMidiTrack) {
                    t.insert(ie->first, e);
                    ie = actualMidiTrack.events().erase(ie);
                    continue;
                }
            }
            ++ie;
        }
        i += nn - 1;
    }
}
} // namespace mu::iex::midi
