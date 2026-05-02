#pragma once

#include <ranges>

namespace litestl::util {
/**
 * Represents a contiguous range starting from index 0 up to `start + size`.
 * Primarily useful for iterating over a count of elements.
 *
 * Example:
 * @code
 * // iterate over 5 indices: 0, 1, 2, 3, 4
 * for (int i : IndexRange(5)) {
 *   process(i);
 * }
 * @endcode
 */
struct IndexRange {
  int start, size;

  /** Default constructs an empty range. */
  constexpr IndexRange() : start(0), size(0)
  {
  }

  /** Constructs a range of `count` indices starting from 0. */
  constexpr IndexRange(int count) : start(0), size(count)
  {
  }

  /** Constructs a range with the given start offset and size. */
  constexpr IndexRange(int a, int b) : start(a), size(b)
  {
  }

  /** Forward iterator that yields sequential integer values. */
  struct iterator {
    using value_type = int;
    using difference_type = int;

    constexpr iterator() : i_(0)
    {
    }
    constexpr iterator(int i) : i_(i)
    {
    }
    constexpr iterator(const iterator &b) : i_(b.i_)
    {
    }

    constexpr bool operator==(const iterator &b) const
    {
      return b.i_ == i_;
    }
    constexpr bool operator!=(const iterator &b) const
    {
      return !operator==(b);
    }

    constexpr int operator*() const
    {
      return i_;
    }
    constexpr iterator &operator++()
    {
      i_++;

      return *this;
    }
    constexpr iterator operator++(int)
    {
      iterator cpy = *this;
      i_++;
      return cpy;
    }
    constexpr iterator &operator--()
    {
      i_--;

      return *this;
    }
    constexpr iterator operator--(int)
    {
      iterator cpy = *this;
      i_--;
      return cpy;
    }

    constexpr difference_type operator-(const iterator &b) const
    {
      return i_ - b.i_;
    }

  private:
    int i_;
  };

  using const_iterator = iterator;

  /** Returns an iterator to index 0. */
  constexpr const_iterator begin() const
  {
    return const_iterator(start);
  }

  /** Returns an iterator past the last index (`start + size`). */
  constexpr const_iterator end() const
  {
    return const_iterator(start + size);
  }

  /** Returns an iterator to index 0. */
  constexpr iterator begin()
  {
    return iterator(start);
  }

  /** Returns an iterator past the last index (`start + size`). */
  constexpr iterator end()
  {
    return iterator(start + size);
  }
};
} // namespace litestl::util
