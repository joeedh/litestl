#include "compress.h"

#include "lz4.h"
#include "lz4hc.h"

namespace sculptcore::io {

size_t compressBlock(const void *src, size_t srcSize, Vector<uint8_t> &dst, int hcLevel)
{
  dst.resize(0);
  if (srcSize == 0 || srcSize > size_t(LZ4_MAX_INPUT_SIZE)) {
    return 0;
  }

  int bound = LZ4_compressBound(int(srcSize));
  dst.resize(size_t(bound));

  int n = LZ4_compress_HC(static_cast<const char *>(src),
                          reinterpret_cast<char *>(dst.data()),
                          int(srcSize),
                          bound,
                          hcLevel);
  if (n <= 0) {
    dst.resize(0);
    return 0;
  }

  dst.resize(size_t(n));
  return size_t(n);
}

bool decompressBlock(const void *src, size_t compSize, size_t rawSize, Vector<uint8_t> &dst)
{
  dst.resize(rawSize);
  if (rawSize == 0) {
    return true;
  }

  int n = LZ4_decompress_safe(static_cast<const char *>(src),
                              reinterpret_cast<char *>(dst.data()),
                              int(compSize),
                              int(rawSize));
  return n >= 0 && size_t(n) == rawSize;
}

} // namespace sculptcore::io
