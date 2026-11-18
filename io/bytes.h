#pragma once

/* Minimal little-endian byte stream for canonical formats (attribute
 * columns, content-addressed blobs). Header-only, so consumers don't link
 * the io library (and through it binding and lz4).
 *
 * ByteReader is bounds-checked: reading past the end sets a sticky error and
 * yields zeros, never undefined behaviour. Callers bound counts with
 * remaining() before allocating. ByteWriter output is limited to < 2 GB
 * (util::Vector uses int sizes). */

#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "util/vector.h"

namespace litestl::io {

static_assert(std::endian::native == std::endian::little, "litestl assumes a little-endian host");

class ByteWriter {
public:
  util::Vector<uint8_t, 64> bytes;

  void put(const void *p, size_t n)
  {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    const int start = bytes.size();
    bytes.resize<false>(start + int(n));
    if (n) {
      memcpy(bytes.data() + start, b, n);
    }
  }

  void u8(uint8_t v)
  {
    put(&v, 1);
  }
  void u32(uint32_t v)
  {
    put(&v, 4);
  }
  void u64(uint64_t v)
  {
    put(&v, 8);
  }
  void i64(int64_t v)
  {
    put(&v, 8);
  }
  /** u64 length prefix, then the bytes. */
  void str(std::string_view s)
  {
    u64(s.size());
    put(s.data(), s.size());
  }

  size_t size() const
  {
    return size_t(bytes.size());
  }
  const uint8_t *data() const
  {
    /* util::Vector has no const data() */
    return const_cast<util::Vector<uint8_t, 64> &>(bytes).data();
  }
};

class ByteReader {
public:
  ByteReader(const void *data, size_t size)
      : p_(static_cast<const uint8_t *>(data)), end_(p_ + size)
  {
  }

  /** Copies n bytes into out; on over-read sets the error flag and zero-fills. */
  bool get(void *out, size_t n)
  {
    if (failed_ || size_t(end_ - p_) < n) {
      failed_ = true;
      memset(out, 0, n);
      return false;
    }
    if (n) {
      memcpy(out, p_, n);
    }
    p_ += n;
    return true;
  }

  /** Returns a view of the next n bytes and advances, or nullptr on over-read. */
  const uint8_t *view(size_t n)
  {
    if (failed_ || size_t(end_ - p_) < n) {
      failed_ = true;
      return nullptr;
    }
    const uint8_t *r = p_;
    p_ += n;
    return r;
  }

  uint8_t u8()
  {
    uint8_t v;
    get(&v, 1);
    return v;
  }
  uint32_t u32()
  {
    uint32_t v;
    get(&v, 4);
    return v;
  }
  uint64_t u64()
  {
    uint64_t v;
    get(&v, 8);
    return v;
  }
  int64_t i64()
  {
    int64_t v;
    get(&v, 8);
    return v;
  }
  /** Length-prefixed string view into the buffer; empty on failure or if the
   * declared length exceeds remaining(). */
  std::string_view str()
  {
    const uint64_t n = u64();
    if (failed_ || n > remaining()) {
      failed_ = true;
      return {};
    }
    const uint8_t *s = view(size_t(n));
    return s ? std::string_view(reinterpret_cast<const char *>(s), size_t(n)) : std::string_view();
  }

  size_t remaining() const
  {
    return failed_ ? 0 : size_t(end_ - p_);
  }
  bool failed() const
  {
    return failed_;
  }
  void fail()
  {
    failed_ = true;
  }
  bool at_end() const
  {
    return !failed_ && p_ == end_;
  }

private:
  const uint8_t *p_;
  const uint8_t *end_;
  bool failed_ = false;
};

} // namespace litestl::io
