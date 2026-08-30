#include "imageformat.h"

#include <cstring>

using namespace muse;

namespace sve {
namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]); }
uint16_t be16(const uint8_t* p) { return uint16_t(uint16_t(p[0]) << 8 | p[1]); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | uint32_t(p[0]); }
uint16_t le16(const uint8_t* p) { return uint16_t(uint16_t(p[1]) << 8 | p[0]); }

bool startsWith(const uint8_t* p, size_t size, const char* sig, size_t sigSize)
{
    return size >= sigSize && std::memcmp(p, sig, sigSize) == 0;
}

// JPEG is the one format whose size is not at a fixed offset: the frame
// header sits behind a chain of length-prefixed segments.
ImageFormat probeJpeg(const uint8_t* p, size_t size)
{
    size_t i = 2;   // behind SOI
    while (i + 3 < size) {
        if (p[i] != 0xFF) {
            break;      // not on a marker any more — give up rather than guess
        }
        uint8_t marker = p[i + 1];
        if (marker == 0xFF) {
            ++i;        // fill byte, the next byte is the marker
            continue;
        }
        // Standalone markers carry no length field.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) {
            i += 2;
            continue;
        }
        size_t length = be16(p + i + 2);
        // SOF0…SOF15 hold the frame geometry; DHT (C4), JPG (C8) and DAC (CC)
        // share the range and do not.
        bool isFrameHeader = marker >= 0xC0 && marker <= 0xCF
                             && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (isFrameHeader) {
            if (i + 9 > size) {
                break;
            }
            return { "image/jpeg", be16(p + i + 7), be16(p + i + 5) };
        }
        if (length < 2) {
            break;
        }
        i += 2 + length;
    }
    return {};
}
}

ImageFormat probeImage(const ByteArray& data)
{
    const uint8_t* p = data.constData();
    size_t size = data.size();
    ImageFormat f;

    if (startsWith(p, size, "\x89PNG\r\n\x1a\n", 8)) {
        // Only an IHDR as the first chunk is a PNG the size can be read from.
        if (size >= 24 && std::memcmp(p + 12, "IHDR", 4) == 0) {
            f = { "image/png", static_cast<int>(be32(p + 16)), static_cast<int>(be32(p + 20)) };
        }
    } else if (startsWith(p, size, "\xff\xd8", 2)) {
        f = probeJpeg(p, size);
    } else if (startsWith(p, size, "GIF87a", 6) || startsWith(p, size, "GIF89a", 6)) {
        if (size >= 10) {
            f = { "image/gif", le16(p + 6), le16(p + 8) };
        }
    } else if (startsWith(p, size, "BM", 2) && size >= 26) {
        uint32_t headerSize = le32(p + 14);
        if (headerSize == 12) {                 // BITMAPCOREHEADER
            f = { "image/bmp", le16(p + 18), le16(p + 20) };
        } else if (headerSize >= 40) {          // BITMAPINFOHEADER and its successors
            // A negative height means the rows are stored top-down; the
            // picture is as tall either way.
            int32_t height = static_cast<int32_t>(le32(p + 22));
            f = { "image/bmp", static_cast<int32_t>(le32(p + 18)), height < 0 ? -height : height };
        }
    }

    if (f.width <= 0 || f.height <= 0) {
        return {};
    }
    return f;
}
}
