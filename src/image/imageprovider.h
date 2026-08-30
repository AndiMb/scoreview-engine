#pragma once

// The Qt-free IImageProvider. It has to exist at all: Image::init() calls
// imageProvider()->createPixmap() through a GlobalInject, and with nothing
// registered that call goes through a null pointer — under wasm the whole
// load traps with "null function or function signature mismatch", so a score
// with an embedded picture would not open at all.
//
// Nothing here decodes an image. createPixmap reads the header for the pixel
// size (imageformat.h) and carries the ORIGINAL bytes on; the SVG writer
// embeds them as a data URI, which is the only consumer in this build. Qt's
// provider re-encoded everything to PNG — pointless here, and it would inflate
// every embedded JPEG.
//
// The remaining methods belong to paths this build does not have: writing an
// .mscz and the score thumbnail. They answer emptily rather than assert; that
// keeps a future caller from crashing on a picture that is merely undrawable.

#include "draw/iimageprovider.h"

namespace sve {
class ImageProvider : public muse::draw::IImageProvider
{
public:
    std::shared_ptr<muse::draw::Pixmap> createPixmap(const muse::ByteArray& data) const override;
    std::shared_ptr<muse::draw::Pixmap> createPixmap(int w, int h, int dpm, const muse::draw::Color& color) const override;

    muse::draw::Pixmap scaled(const muse::draw::Pixmap& origin, const muse::Size& s) const override;

    muse::draw::IPaintProviderPtr painterForImage(std::shared_ptr<muse::draw::Pixmap> pixmap) override;

    void saveAsPng(std::shared_ptr<muse::draw::Pixmap> px, muse::io::IODevice* device) override;
};
}
