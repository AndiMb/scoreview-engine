#pragma once

// muse::global ships only a Qt implementation of ICryptographicHash
// (global/internal/cryptographichash.cpp, QCryptographicHash), and it is
// dropped with GLOBAL_NO_INTERNAL. Without one registered, ImageStore::add()
// calls through an unresolved inject and the load of every score carrying a
// picture segfaults — the store hashes each Pictures/ entry before anything
// else looks at it.
//
// MD4 is not a choice made here: it is written into the .mscz format. The
// name of a Pictures/ entry IS the MD4 of its content, and Image::loadFromStore
// finds its bytes by reading that name back as a hash (ImageStore::getImage).
// A different digest, or a stub, would leave every embedded picture
// unresolvable. Nothing here authenticates anything; the value is a content id.

#include "global/icryptographichash.h"

namespace sve {
class CryptographicHash : public muse::ICryptographicHash
{
public:
    muse::ByteArray hash(const muse::ByteArray& data, Algorithm alg) const override;
};
}
