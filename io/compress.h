#pragma once

#include "litestl/util/vector.h"

#include <cstddef>
#include <cstdint>

namespace sculptcore::io {
using litestl::util::Vector;

/* lz4hc compression level. The HC default (9) already meets/exceeds zlib
 * level-3 ratio while decompressing far faster; bump toward 12 only if a
 * caller cares more about size than compress throughput. */
inline constexpr int kDefaultCompressLevel = 9;

/* Plain lz4 instead of lz4hc: ~20x the compress throughput for a modestly
 * worse ratio. For blobs produced on an interactive path and never written to
 * disk -- undo snapshots above all -- the ratio is not worth the stall. The
 * block format is identical, so the decoder does not care which produced it. */
inline constexpr int kFastCompressLevel = 0;

/* Compress [src, src+srcSize) into @p dst (resized to the compressed bytes).
 * @p hcLevel of kFastCompressLevel selects plain lz4. Returns the compressed
 * byte count, or 0 on failure / empty input. */
size_t compressBlock(const void *src,
                     size_t srcSize,
                     Vector<uint8_t> &dst,
                     int hcLevel = kDefaultCompressLevel);

/* Decompress @p compSize bytes from @p src into @p dst, which is resized to
 * @p rawSize (the known uncompressed length). Returns true only when the
 * decoder consumed cleanly and produced exactly @p rawSize bytes. Uses the
 * bounds-checked lz4 decoder so corrupt input can't overrun the buffer. */
bool decompressBlock(const void *src, size_t compSize, size_t rawSize, Vector<uint8_t> &dst);

} // namespace sculptcore::io
