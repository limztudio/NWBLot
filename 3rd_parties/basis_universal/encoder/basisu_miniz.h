// NWBLot adapter: Basis Universal's historical bundled miniz copy is replaced
// with the repository-wide miniz package required by the third-party policy.
#pragma once

#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif

#include <miniz.h>

namespace buminiz{
using ::mz_free;
using ::mz_compress;
using ::mz_compressBound;
using ::mz_crc32;
using ::mz_inflate;
using ::mz_inflateEnd;
using ::mz_inflateInit;
using ::mz_stream;
using ::mz_ulong;
using ::mz_uncompress;
using ::tdefl_compress_mem_to_heap;
using ::tdefl_write_image_to_png_file_in_memory_ex;
using ::tinfl_decompress_mem_to_heap;

inline constexpr int MZ_NO_FLUSH = ::MZ_NO_FLUSH;
inline constexpr int MZ_OK = ::MZ_OK;
inline constexpr int MZ_STREAM_END = ::MZ_STREAM_END;
}
