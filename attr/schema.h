#pragma once

/* Compile-time attribute schemas.
 *
 * An attribute definition binds a name (string literal), a column type,
 * flags and a default once, as a type:
 *
 *   using Pos  = attr::AttrDef<"pos", attr::Column<math::double3>>;
 *   struct Next : attr::AttrDef<".c.next", attr::Column<uint32_t>, attr::ColumnFlags::Topo> {
 *     static uint32_t default_value() { return 0xffffffffu; }   // custom default
 *   };
 *   using CoedgeSchema = attr::Schema<Next, ...>;
 *
 * A TypedGroup<Schema> registers the schema's attributes as the group's
 * builtin columns, in order, and resolves them by name at compile time:
 *
 *   attr::TypedGroup<VertexSchema> verts;
 *   verts.get<"pos">().set(i, p);   // Column<double3>&; no slot indices
 *
 * Errors hoisted to compile time: unknown names, duplicate names in a
 * schema, non-column types, and using a column as the wrong type. Optional
 * (non-builtin) attributes reuse the same definitions through
 * attr::ensure<Def>(group) / attr::find<Def>(group), so a name is never
 * spelled apart from its type. */

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "attr/group.h"
#include "util/string.h"

namespace litestl::attr {

template <util::StrLiteral Name, typename Col, ColumnFlags Flags = ColumnFlags::None> struct AttrDef {
  using column_type = Col;
  using value_type = typename Col::value_type;
  static constexpr std::string_view name = Name.sv();
  static constexpr ColumnFlags flags = Flags;
  /** Shadow in a derived struct to give a non-zero default. */
  static value_type default_value()
  {
    return value_type{};
  }
};

template <typename D>
concept AttrDefinition = requires {
  typename D::column_type;
  typename D::value_type;
  { D::name } -> std::convertible_to<std::string_view>;
  { D::flags } -> std::convertible_to<ColumnFlags>;
  { D::default_value() } -> std::convertible_to<typename D::value_type>;
} && std::is_base_of_v<ColumnBase, typename D::column_type>;

namespace detail {
template <typename...> constexpr bool always_false = false;
template <auto...> constexpr bool always_false_v = false;
} // namespace detail

template <AttrDefinition... Defs> struct Schema {
  static constexpr size_t size = sizeof...(Defs);
  static constexpr size_t npos = size_t(-1);
  static constexpr std::array<std::string_view, size> names = {Defs::name...};

  static constexpr size_t index_of(std::string_view n)
  {
    for (size_t i = 0; i < size; i++) {
      if (names[i] == n) {
        return i;
      }
    }
    return npos;
  }

  template <typename D> static constexpr size_t index_of_def()
  {
    size_t i = 0, found = npos;
    ((std::is_same_v<D, Defs> ? (found = (found == npos ? i : found), ++i) : ++i), ...);
    return found;
  }

  static constexpr bool unique_names()
  {
    for (size_t i = 0; i < size; i++) {
      for (size_t j = i + 1; j < size; j++) {
        if (names[i] == names[j]) {
          return false;
        }
      }
    }
    return true;
  }
  static_assert(unique_names(), "attr::Schema: duplicate attribute names");

  template <size_t I> using def_at = std::tuple_element_t<I, std::tuple<Defs...>>;

  /** A schema with more attributes appended (e.g. a domain plus user builtins). */
  template <AttrDefinition... More> using extend = Schema<Defs..., More...>;

  /** Adds every attribute as a builtin column, in schema order. The group
   * must not have dynamic columns yet. */
  static void register_into(AttrGroup &g)
  {
    (register_one<Defs>(g), ...);
  }

private:
  template <typename D> static void register_one(AttrGroup &g)
  {
    if (!g.template add_builtin<typename D::column_type>(D::name, D::default_value(), D::flags)) {
      fprintf(stderr, "attr::Schema: cannot register builtin \"%.*s\"\n", int(D::name.size()), D::name.data());
      abort();
    }
  }
};

/** An AttrGroup whose builtin columns are given by a compile-time Schema. */
template <typename S> class TypedGroup {
public:
  using schema = S;

  explicit TypedGroup(int page_shift = kDefaultPageShift) : g_(page_shift)
  {
    S::register_into(g_);
  }

  /** The column of the attribute named N (compile error if N isn't in S). */
  template <util::StrLiteral N> auto &get()
  {
    constexpr size_t i = S::index_of(N.sv());
    if constexpr (i == S::npos) {
      static_assert(detail::always_false_v<N>, "attr::TypedGroup::get: no attribute named this in the schema");
      return g_; /* unreachable */
    }
    else {
      using D = typename S::template def_at<i>;
      return g_.template builtin<typename D::column_type>(uint32_t(i));
    }
  }
  template <util::StrLiteral N> const auto &get() const
  {
    return const_cast<TypedGroup *>(this)->template get<N>();
  }

  /** The column of definition D (compile error if D isn't in S). */
  template <AttrDefinition D> typename D::column_type &get()
  {
    constexpr size_t i = S::template index_of_def<D>();
    static_assert(i != S::npos, "attr::TypedGroup::get: this attribute definition is not in the schema");
    return g_.template builtin<typename D::column_type>(uint32_t(i));
  }
  template <AttrDefinition D> const typename D::column_type &get() const
  {
    return const_cast<TypedGroup *>(this)->template get<D>();
  }

  /** Slot index of a named attribute, for code that needs it (MixContext,
   * claimed_slots); a compile-time constant. */
  template <util::StrLiteral N> static constexpr uint32_t slot()
  {
    constexpr size_t i = S::index_of(N.sv());
    static_assert(i != S::npos, "attr::TypedGroup::slot: no attribute named this in the schema");
    return uint32_t(i);
  }

  AttrGroup &group()
  {
    return g_;
  }
  const AttrGroup &group() const
  {
    return g_;
  }
  uint32_t size() const
  {
    return g_.size();
  }
  void resize(uint32_t n)
  {
    g_.resize(n);
  }

private:
  AttrGroup g_;
};

/** Optional (dynamic) attribute by definition: creates it with the
 * definition's default and flags, or returns null on a conflicting column. */
template <AttrDefinition D> typename D::column_type *ensure(AttrGroup &g)
{
  return g.template ensure<typename D::column_type>(D::name, D::default_value(), D::flags);
}

template <AttrDefinition D> typename D::column_type *find(AttrGroup &g)
{
  return g.template find<typename D::column_type>(D::name);
}

template <AttrDefinition D> const typename D::column_type *find(const AttrGroup &g)
{
  return const_cast<AttrGroup &>(g).template find<typename D::column_type>(D::name);
}

} // namespace litestl::attr
