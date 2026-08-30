#include "wasmres.h"

#include <cstdlib>
#include <cstring>

using namespace muse;

namespace sve {
WasmRes WasmRes::error(int code, const String& message)
{
    Ret ret(code, message.toStdString());
    return WasmRes(message.toUtf8(), ret);
}

WasmRes::WasmRes(ByteArray data, Ret ret)
{
    m_buffer.open(io::Buffer::ReadWrite);

    // write error code
    m_buffer.write(numberToByteArray(ret.code()));

    // write data
    uint32_t size = static_cast<uint32_t>(data.size());
    m_buffer.write(numberToByteArray(size));
    m_buffer.write(data);

    m_buffer.close();
}

// realloc/copy the data block so that it can be referred by its ptr and then
// freed c-style from the JS side
const char* WasmRes::reallocData(ByteArray data)
{
    auto size = data.size() + 1;   // ByteArray guarantees a trailing '\0'
    auto buf = (char*)malloc(size);
    if (!buf) {
        // Out of heap. A null pointer is the only honest answer - the wrapper
        // reads the block through it and checks for 0 (helper.js, class
        // WasmRes); memcpy'ing into address 0 would corrupt the module's low
        // memory instead.
        return nullptr;
    }
    memcpy(buf, data.constData(), size);
    return buf;
}
}
