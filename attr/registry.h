#pragma once

/* ColumnTypeRegistry: stable type key -> ColumnType, for deserialization.
 *
 * Registration is explicit (no static registrar objects: those are silently
 * dropped from static libraries). Builtin column types are registered when
 * global() is first used; other modules call their own register function
 * from their init routine. Column<...>::static_type() also registers lazily,
 * but read() correctness must never depend on that.
 *
 * Every operation takes a mutex. The registry's storage uses std containers
 * and plain new (process lifetime, never freed), so it is invisible to the
 * litestl leak tracker. */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "column_base.h"

namespace litestl::attr {

class ColumnTypeRegistry {
public:
  static ColumnTypeRegistry &global();

  /** Registers column class Col under Col::type_key(). Idempotent for the
   * same class; returns null (and *err) if another class owns the key. */
  template <typename Col> const ColumnType *add(const char **err = nullptr)
  {
    return add_impl(
        Col::type_key(), uint32_t(sizeof(typename Col::value_type)), &Col::create, class_tag<Col>(), err);
  }

  template <typename Col> const ColumnType *add_or_die()
  {
    const char *err = nullptr;
    const ColumnType *t = add<Col>(&err);
    if (!t) {
      fprintf(stderr, "litestl::attr registry: %s (%s)\n", err, Col::type_key().c_str());
      abort();
    }
    return t;
  }

  const ColumnType *find(std::string_view key) const;
  size_t size() const;

private:
  struct Entry {
    std::string key;
    ColumnType type;
    const void *tag;
  };

  ColumnTypeRegistry() = default;
  const ColumnType *add_impl(std::string key,
                             uint32_t value_size,
                             util::UniquePtr<ColumnBase> (*create)(const uint8_t *, int),
                             const void *tag,
                             const char **err);

  template <typename Col> static const void *class_tag()
  {
    static const char tag = 0;
    return &tag;
  }

  mutable std::mutex mutex_;
  std::map<std::string, Entry *, std::less<>> entries_;
};

} // namespace litestl::attr
