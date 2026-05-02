#pragma once

#include "util/alloc.h"
#include "util/compiler_util.h"
#include "util/vector.h"
#include <algorithm>
#include <atomic>
#include <compare>
#include <cstring>
#include <format>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

namespace litestl::util {
// reserve enough space for a guid
template <typename Char, int static_size = 40> struct String;

template <size_t N> struct StrLiteral {
  constexpr StrLiteral(const char (&str)[N])
  {
    std::copy_n(str, N, value);
    value[N] = 0;
  }

  char value[N + 1];
};

template <typename Char, int static_size = 32> struct ConstStr {
  constexpr ConstStr()
  {
    size_ = 0;
    zero_data();
  }

  constexpr ConstStr(const ConstStr &b)
  {
    for (int i = 0; i < static_size; i++) {
      data_[i] = b.data_[i];
    }

    data_[b.size_] = 0;
    size_ = b.size_;
  }

  constexpr ConstStr(const char *str)
  {
    zero_data();

    const char *c = str;
    while (*c && size_ < static_size - 1) {
      data_[size_++] = *c;
      c++;
    }
  }

  template <size_t N> constexpr ConstStr(StrLiteral<N> lit)
  {
    size_ = N - 1 > static_size - 1 ? static_size - 1 : N - 1;

    for (int i = 0; i < size_; i++) {
      data_[i] = lit.value[i];
    }
    data_[size_] = 0;
  }

  constexpr bool operator==(const ConstStr &b)
  {
    if (size_ != b.size_) {
      return false;
    }

    for (int i = 0; i < size_; i++) {
      if (data_[i] != b.data_[i]) {
        return false;
      }
    }

    return true;
  }

  constexpr bool operator!=(const ConstStr &b)
  {
    return !operator==(b);
  }

  constexpr size_t size()
  {
    return size_;
  }

  constexpr Char operator[](int idx)
  {
    return data_[idx];
  }

  const Char *data()
  {
    return data_;
  }

private:
  constexpr void zero_data()
  {
    for (int i = 0; i < static_size; i++) {
      data_[i] = 0;
    }
  }

  Char data_[static_size];
  int size_ = 0;
};

using const_string = ConstStr<char>;

namespace detail {
template <typename Char> int strcmp(const Char *a, const Char *b)
{
  if (!a || !b) {
    return -1;
  }

  while (*a && *b) {
    if (*a < *b) {
      return -1;
    } else if (*a > *b) {
      return 1;
    }

    a++;
    b++;
  }

  if (*a || *b) {
    return *a ? -1 : 1;
  }

  return 0;
}
} // namespace detail

namespace detail {
template <typename S, typename QualValue> struct StringIter {
  using value_type = QualValue;
  using difference_type = std::ptrdiff_t;
  using reference = value_type &;
  using pointer = value_type *;
  using iterator_category = std::random_access_iterator_tag;

  S *str;
  difference_type i;

  StringIter() : str(nullptr), i(0)
  {
  }
  StringIter(S *s, difference_type i = 0) : str(s), i(i)
  {
  }
  StringIter(const StringIter &b) = default;
  StringIter &operator=(const StringIter &b) = default;

  reference operator*()
  {
    return (*str)[i];
  }
  reference operator*() const
  {
    return (*str)[i];
  }

  reference operator[](difference_type index)
  {
    return (*str)[i + index];
  }
  reference operator[](difference_type index) const
  {
    return (*str)[i + index];
  }

  StringIter &operator++()
  {
    i++;
    return *this;
  }
  StringIter operator++(int arg)
  {
    StringIter cpy = *this;
    i++;
    return cpy;
  }
  StringIter &operator--()
  {
    i--;
    return *this;
  }
  StringIter operator--(int arg)
  {
    StringIter cpy = *this;
    i--;
    return cpy;
  }
  bool operator==(const StringIter &) const = default;
  auto operator<=>(const StringIter &b) const
  {
    return i <=> b.i;
  }
  StringIter &operator+=(difference_type offset)
  {
    i += offset;
    return *this;
  }
  StringIter &operator-=(difference_type offset)
  {
    i -= offset;
    return *this;
  }
  StringIter operator+(difference_type offset) const
  {
    return StringIter(str, i + offset);
  }
  friend StringIter operator+(difference_type offset, const StringIter &b)
  {
    return StringIter(b.str, offset + b.i);
  }
  StringIter operator-(difference_type offset) const
  {
    return StringIter(str, i - offset);
  }
  friend difference_type operator-(const StringIter &b, const StringIter &c)
  {
    return b.i - c.i;
  }
};
} // namespace detail

template <typename Char> struct StringRef {
  using value_type = Char;

  StringRef()
  {
  }
  StringRef(const char *c) : data_(c), size_(strlen(c))
  {
  }
  StringRef(const StringRef &b) : data_(b.data_), size_(b.size_)
  {
  }

  operator String<Char>()
  {
    return String<Char>(data_);
  }

  bool operator!=(const StringRef &vb) const
  {
    return !operator==(vb);
  }

  bool operator==(const StringRef &vb) const
  {
    if (size_ != vb.size_) {
      return false;
    }

    for (int i = 0; i < size_; i++) {
      if (this->data_[i] != vb.data_[i]) {
        return false;
      }
    }

    return true;
  }

  using const_char_star = const char *;

  operator const_char_star() const
  {
    return data_;
  }

  inline const char *c_str() const
  {
    return data_;
  }

  inline const char operator[](int idx) const
  {
    return data_[idx];
  }

  inline size_t size() const
  {
    return size_;
  }

  bool operator==(StringRef &b) const
  {
    return detail::strcmp(data_, b.data_);
  }

  bool operator!=(StringRef &b) const
  {
    return !operator==(b);
  }

private:
  const char *data_ = nullptr;
  int size_ = 0;
};

// default for static_size is defined in forward declaration at the top of this file
template <typename Char, int static_size> class alignas(8) String {
public:
  using value_type = Char;
  using iterator = detail::StringIter<String, Char>;
  using const_iterator = detail::StringIter<const String, const Char>;

  String() : size_(0)
  {
    data_ = static_storage_;
    data_[0] = 0;
  }

  operator StringRef<Char>() const
  {
    return StringRef<Char>(data_);
  }

  template <size_t N> String(StrLiteral<N> lit)
  {
    data_ = static_storage_;
    ensure_size(N);

    // str literals include their null byte
    size_ = N - 1;

    for (int i = 0; i < N; i++) {
      data_[i] = lit.value[i];
    }
    data_[N] = 0;
  }

  ATTR_NO_OPT String(const String &b)
  {
    data_ = static_storage_;

    if (b.data_) {
      ensure_size(b.size_);
      size_ = b.size_;
      // std::copy_n(b.data_, size_, data_);
      for (int i = 0; i < b.size_; i++) {
        data_[i] = b.data_[i];
      }
      data_[size_] = 0;
    } else {
      size_ = 0;
      data_[0] = 0;
    }
  }

  String(String &&b)
  {
    size_ = b.size_;

    if (size_ < static_size - 1) {
      data_ = static_storage_;

      for (int i = 0; i < b.size_; i++) {
        data_[i] = b.data_[i];
      }

      data_[b.size_] = 0;
    } else {
      data_ = b.data_;
    }

    b.data_ = b.static_storage_;
    b.data_[0] = 0;
    b.size_ = 0;
  }

  String &operator=(const String &b)
  {
    if (&b == this) {
      return *this;
    }

    ensure_size(b.size_);
    size_ = b.size_;

    for (int i = 0; i < size_; i++) {
      data_[i] = b.data_[i];
    }
    data_[size_] = 0;
    return *this;
  }

  inline Char &operator[](intptr_t idx)
  {
    return data_[idx];
  }

  inline const Char &operator[](intptr_t idx) const
  {
    return data_[idx];
  }

  DEFAULT_MOVE_ASSIGNMENT(String)

  ~String()
  {
    if (data_ && data_ != static_storage_) {
      alloc::release(static_cast<void *>(data_));
    }
  }

  String(const char *str)
  {
    data_ = static_storage_;
    size_ = 0;
    int len = strlen(str);

    ensure_size(len);
    size_ = len;

    for (int i = 0; i < len; i++) {
      data_[i] = str[i];
    }
    data_[len] = 0;
  }

  String(const StringRef<Char> &ref)
  {
    String(ref.c_str());
  }

  const char *c_str() const
  {
    return data_;
  }

  operator const char *() const
  {
    return data_;
  }

  bool operator!=(const String &vb) const
  {
    return !operator==(vb);
  }

  bool operator==(const String &b) const
  {
    if (size_ != b.size_) {
      return false;
    }
    for (int i = 0; i < size_; i++) {
      if (data_[i] != b.data_[i]) {
        return false;
      }
    }
    return true;
  }

  String operator+(const String &b) const
  {
    return String(*this).operator+=(b);
  }

  String operator+(const std::string &b) const
  {
    return String(*this).operator+=(b);
  }
  String operator+(const char *b) const
  {
    return String(*this).operator+=(b);
  }

  String &operator+=(const String &b)
  {
    ensure_size(size_ + b.size_);

    int j = size_;
    for (int i = 0; i < b.size_; i++) {
      data_[j++] = b.data_[i];
    }

    size_ += b.size_;
    data_[size_] = 0;

    return *this;
  }
  String operator+(Char b) const
  {
    return String(*this).operator+=(b);
  }

  String &operator+=(const Char b)
  {
    ensure_size(size_ + 1);
    data_[size_++] = b;
    data_[size_] = 0;
    return *this;
  }
  String &operator+=(const std::string &b)
  {
    operator+=(String(b.c_str()));
    return *this;
  }

  String &operator+=(const Char *b)
  {
    operator+=(String(b));
    return *this;
  }

  size_t size() const
  {
    return size_;
  }

  bool starts_with(const String &b) const
  {
    if (size_ < b.size_) {
      return false;
    }
    for (int i = 0; i < b.size_; i++) {
      if (data_[i] != b.data_[i]) {
        return false;
      }
    }
    return true;
  }
  bool ends_with(const String &b) const
  {
    if (size_ < b.size_) {
      return false;
    }
    for (int i = 0; i < b.size_; i++) {
      if (data_[size_ - b.size_ + i] != b.data_[i]) {
        return false;
      }
    }
    return true;
  }

  Vector<String> split(const Char splitC)
  {
    String current;
    Vector<String> result = {};

    int count = size();
    for (int i = 0; i < count; i++) {
      const Char c = data_[i];
      if (c == splitC) {
        result.append(String(current));
        current = "";
      } else {
        current += c;
      }
    }
    if (current.size_ > 0) {
      result.append(current);
    }
    return result;
  }

  String trimRight()
  {
    String b = *this;
    for (int i = 0; i < b.size_; i++) {
      if (b.data_[i] == ' ' || b.data_[i] == '\n' || b.data_[i] == '\r' ||
          b.data_[i] == '\t')
      {
        b.size_ = i;
        b.data_[i] = 0;
        return b;
      }
    }
    return b;
  }

  String trimLeft()
  {
    String b = *this;
    for (int i = 0; i < b.size_; i++) {
      if (b.data_[i] == ' ' || b.data_[i] == '\n' || b.data_[i] == '\r' ||
          b.data_[i] == '\t')
      {
        for (int j = 0; j < b.size_ - i; j++) {
          b.data_[j] = b.data_[j + i];
        }
        b.size_ -= i;
        b.data_[b.size_] = 0;
        return b;
      }
    }
    return b;
  }

  String trim()
  {
    return trimLeft().trimRight();
  }

  String substr(int start)
  {
    return substr(start, size() - start);
  }

  String substr(int start, int count)
  {
    String b;
    if (count < 0) {
      count += size();
    }
    count = std::min(count, int(size()) - start);
    for (int i = start; i < start + count; i++) {
      b += data_[i];
    }
    return b;
  }

  iterator begin()
  {
    return iterator(this, 0);
  }
  iterator end()
  {
    return iterator(this, size_);
  }
  const_iterator begin() const
  {
    return const_iterator(this, 0);
  }
  const_iterator end() const
  {
    return const_iterator(this, size_);
  }

  size_t search(std::regex re) const
  {
    std::match_results<const_iterator> m;
    bool result = std::regex_search(begin(), end(), m, re);

    if (!result) {
      return -1;
    }

    return size_t(m[0].first - data_);
  }

  bool contains(const String &b) const
  {
    size_t j = 0;
    for (size_t i = 0; i < size_t(size_); i++) {
      if (data_[i] == b[j]) {
        j++;
        if (j == b.size()) {
          return true;
        }
      } else {
        j = 0;
      }
    }
    return false;
  }

private:
  /* Ensures data has at least size+1 elements, *does not set size_!* */
  void ensure_size(int size)
  {
    if (size > size_) {
      Char *data2;

      if (size < static_size - 1) {
        data2 = static_storage_;
      } else {
        data2 = static_cast<Char *>(alloc::alloc("string", size + 1));
      }

      int i;
      for (i = 0; i < size_; i++) {
        data2[i] = data_[i];
      }
      data2[size_] = 0;

      if (data_ != static_storage_) {
        alloc::release(static_cast<void *>(data_));
      }
      data_ = data2;
    }
  }

  Char *data_;
  int size_ = 0; /* does not include null-terminating byte. */
  Char static_storage_[static_size];
};

template <typename Char> String<Char> operator+(const Char *a, const String<Char> &b)
{
  return String<Char>(a) + b;
}

using string = String<char>;
using stringref = StringRef<char>;

using StringKey = int;

/* Get a unique integer key for str. */
StringKey get_stringkey(const stringref str);

static_assert(std::random_access_iterator<detail::StringIter<string, char>>);
static_assert(std::random_access_iterator<detail::StringIter<const string, const char>>);
// static_assert(std::random_access_iterator<detail::StringIter<const char, const char>>);

} // namespace litestl::util

template <typename T>
struct std::formatter<litestl::util::String<T>, T> : std::formatter<T> {
  auto format(litestl::util::String<T> const &s, format_context &ctx) const
  {
    auto str = std::basic_string<T>(s.begin(), s.end());
    const auto formatter = std::formatter<std::basic_string<T>>();
    return formatter.format(str, ctx);
  }
};
