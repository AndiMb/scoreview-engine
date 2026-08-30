#pragma once

// What kind of picture the bytes of an .mscz `Pictures/` entry are, and how
// large. Qt's image plugins answered both in the Qt line; here the header of
// the formats a browser can display is read directly — the pixel size is what
// the layout needs (an autoscaled image derives its aspect ratio from it), the
// media type is what the SVG writer stamps on the data URI.
//
// The bytes are never decoded. The SVG carries the original encoding, so a
// JPEG stays a JPEG instead of being re-encoded to PNG the way Qt's provider
// did — nothing here has to know how to read a pixel.
//
// MuseScore also accepts TIFF, which no browser shows; it is reported as
// unknown, like anything else the header sniffing does not recognize.

#include "global/types/bytearray.h"

namespace sve {
struct ImageFormat {
    const char* mediaType = nullptr;   //!< nullptr: not a format this build can put into an SVG
    int width = 0;
    int height = 0;

    bool isKnown() const { return mediaType != nullptr; }
};

//! Reads width, height and media type out of the file header. Returns an
//! unknown format for anything not recognized, and for a header that claims a
//! zero dimension.
ImageFormat probeImage(const muse::ByteArray& data);
}
