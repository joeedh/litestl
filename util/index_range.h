#pragma once
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
  IndexRange() : start(0), size(0)
  {
  }

  /** Constructs a range of `count` indices starting from 0. */
  IndexRange(int count) : start(0), size(count)
  {
  }

  /** Constructs a range with the given start offset and size. */
  IndexRange(int a, int b) : start(a), size(b)
  {
  }

  /** Forward iterator that yields sequential integer values. */
  struct iterator {
    iterator(int i) : i_(i)
    {
    }
    iterator(const iterator &b) : i_(b.i_)
    {
    }

    bool operator==(const iterator &b) const
    {
      return b.i_ == i_;
    }
    bool operator!=(const iterator &b) const
    {
      return !operator==(b);
    }

    int operator*() const
    {
      return i_;
    }
    iterator &operator++()
    {
      i_++;

      return *this;
    }

  private:
    int i_;
  };

  /** Returns an iterator to index 0. */
  iterator begin() const
  {
    return iterator(0);
  }

  /** Returns an iterator past the last index (`start + size`). */
  iterator end() const
  {
    return iterator(start + size);
  }
};
} // namespace litestl::util
