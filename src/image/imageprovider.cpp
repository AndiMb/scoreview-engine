#include "imageprovider.h"

#include "imageformat.h"

#include "log.h"

using namespace muse;
using namespace muse::draw;

namespace sve {
std::shared_ptr<Pixmap> ImageProvider::createPixmap(const ByteArray& data) const
{
    ImageFormat format = probeImage(data);
    if (!format.isKnown()) {
        LOGW() << "unsupported image format, " << data.size() << " bytes";
        // Never a null shared_ptr and never a zero size: Image::init()
        // dereferences the result without checking, and TDraw's direct-draw
        // branch divides the target size by the pixel size. A 1x1 pixmap
        // without data survives both — and because it reports isNull(), the
        // scaled branch falls through to MuseScore's own crossed-box
        // placeholder and the SVG writer skips it.
        return std::make_shared<Pixmap>(ByteArray(), Size(1, 1));
    }
    return std::make_shared<Pixmap>(data, Size(format.width, format.height));
}

std::shared_ptr<Pixmap> ImageProvider::createPixmap(int w, int h, int, const Color&) const
{
    // Score::createThumbnail only, which this build never calls.
    return std::make_shared<Pixmap>(ByteArray(), Size(w, h));
}

Pixmap ImageProvider::scaled(const Pixmap& origin, const Size& s) const
{
    // No resampling: the SVG scales the embedded original to whatever width
    // and height the writer gives the <image>, and the size carried here is
    // exactly the target TDraw then draws at 1:1.
    return Pixmap(origin.data(), s);
}

IPaintProviderPtr ImageProvider::painterForImage(std::shared_ptr<Pixmap>)
{
    // Painting INTO an image needs a raster backend; this build renders to SVG.
    return nullptr;
}

void ImageProvider::saveAsPng(std::shared_ptr<Pixmap>, io::IODevice*)
{
    // Only the .mscz writer needs this, and writing is not supported.
    NOT_SUPPORTED;
}
}
