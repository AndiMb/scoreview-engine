#pragma once

// Qt-free port of webmscore's web/wasmres.{h,cpp}: responses cross the wasm
// boundary as a malloc'd block [u32 retCode][u32 size][data...] that the JS
// side (web-public/src/helper.js, class WasmRes) reads and frees. The layout
// is the wire contract with the wrapper — do not change one side alone.

#include <stdint.h>

#include "global/types/bytearray.h"
#include "global/io/buffer.h"
#include "global/types/ret.h"
#include "global/types/string.h"

namespace sve {
typedef const uint8_t* WasmResBytes;

struct WasmRes {
public:
    WasmRes(muse::ByteArray data, muse::Ret ret = muse::make_ok());

    WasmRes(muse::String str)
        : WasmRes(str.toUtf8()) {}

    WasmRes(uint32_t num)
        : WasmRes(numberToByteArray(num)) {}

    WasmRes()
        : WasmRes(muse::ByteArray()) {}

    static WasmRes error(int code, const muse::String& message);

    inline operator WasmResBytes() {
        return (WasmResBytes)reallocData(m_buffer.data());
    }

private:
    muse::io::Buffer m_buffer;

    static const char* reallocData(muse::ByteArray data);

    static inline muse::ByteArray numberToByteArray(uint32_t num) {
        return muse::ByteArray((const char*)&num, sizeof(num));
    }
};
}
