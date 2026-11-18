#pragma once

/* Slot tables and the dedup index of InternPool (attr/pool.h).
 *
 * SlotTable<Node, Shift>: a paged copy-on-write array of slot words. A word
 * holds a Node* (low two bits 0), a free-list link ((next << 2) | 1) or the
 * quarantine marker (3). Pages are shared between snapshots through an atomic
 * count; nodes are shared through their own atomic `holders` count, which is
 * the number of page buffers pointing at them:
 *   - copying a shared page before a write retains every node in it;
 *   - the last release of a page releases every node in it;
 *   - clearing a word in a unique page releases that word's node.
 *
 * InternIndex: content hash -> slot, two-level so snapshots don't copy it on
 * every insert (P1b B2). `base` and `delta` are open-addressing tables of
 * {h32, slot+1}, both atomically refcounted. Inserts and removals go straight
 * into a unique base; while the base is shared, inserts go into the delta and
 * removals are left lazy (stale records are rejected at lookup, which always
 * verifies the candidate against the current slot). The index is rebuilt from
 * the slot table when it grows or when delta + stale records pass
 * max(1024, live / 8). Index layout never affects results: lookups return the
 * one live slot holding an equal value, whichever record finds it. */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

#include "util/alloc.h"
#include "util/vector.h"

namespace litestl::attr::detail {

using SlotWord = uintptr_t;
constexpr SlotWord kQuarantined = 3;
constexpr uint32_t kFreeEnd = 0x1fffffffu; /* end of the free list; slots < kFreeEnd */

inline bool word_is_node(SlotWord w)
{
  return w != 0 && (w & 3) == 0;
}
inline bool word_is_free(SlotWord w)
{
  return (w & 3) == 1;
}
inline SlotWord free_word(uint32_t next)
{
  return (SlotWord(next) << 2) | 1;
}
inline uint32_t free_next(SlotWord w)
{
  return uint32_t(w >> 2);
}

/** Aligned, alloc-tracked block with an intrusive atomic count. */
template <typename H> H *alloc_with_header(const char *tag, size_t bytes, size_t align)
{
  void *block = alloc::alloc(tag, bytes + align);
  const uintptr_t base = (reinterpret_cast<uintptr_t>(block) + align - 1) & ~uintptr_t(align - 1);
  H *h = new (reinterpret_cast<void *>(base)) H();
  h->block = block;
  return h;
}

template <typename Node, int Shift> class SlotTable {
  static constexpr uint32_t kPage = 1u << Shift;
  static constexpr uint32_t kMask = kPage - 1;

  struct Page {
    std::atomic<uint32_t> rc{1};
    void *block = nullptr;
    SlotWord w[kPage];
  };

  util::Vector<Page *> pages_;
  uint32_t size_ = 0;

  static Page *new_page()
  {
    Page *p = alloc_with_header<Page>("attr::SlotPage", sizeof(Page), alignof(Page) > 16 ? alignof(Page) : 16);
    memset(static_cast<void *>(p->w), 0, sizeof(p->w));
    return p;
  }

  static void release_page(Page *p)
  {
    if (p && p->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      for (uint32_t i = 0; i < kPage; i++) {
        if (word_is_node(p->w[i])) {
          Node::release(reinterpret_cast<Node *>(p->w[i]));
        }
      }
      void *block = p->block;
      p->~Page();
      alloc::release(block);
    }
  }

  Page *unique_page(uint32_t pi)
  {
    Page *p = pages_[int(pi)];
    if (p->rc.load(std::memory_order_acquire) == 1) {
      return p;
    }
    Page *c = new_page();
    memcpy(static_cast<void *>(c->w), static_cast<const void *>(p->w), sizeof(p->w));
    for (uint32_t i = 0; i < kPage; i++) {
      if (word_is_node(c->w[i])) {
        Node::retain(reinterpret_cast<Node *>(c->w[i]));
      }
    }
    release_page(p);
    pages_[int(pi)] = c;
    return c;
  }

public:
  SlotTable() = default;
  SlotTable(const SlotTable &b) : pages_(b.pages_), size_(b.size_)
  {
    for (Page *p : pages_) {
      p->rc.fetch_add(1, std::memory_order_relaxed);
    }
  }
  SlotTable(SlotTable &&b) noexcept : pages_(std::move(b.pages_)), size_(b.size_)
  {
    b.pages_.clear();
    b.size_ = 0;
  }
  SlotTable &operator=(const SlotTable &b)
  {
    if (this != &b) {
      SlotTable tmp(b);
      *this = std::move(tmp);
    }
    return *this;
  }
  SlotTable &operator=(SlotTable &&b) noexcept
  {
    if (this != &b) {
      clear();
      pages_ = std::move(b.pages_);
      size_ = b.size_;
      b.pages_.clear();
      b.size_ = 0;
    }
    return *this;
  }
  ~SlotTable()
  {
    clear();
  }

  void clear()
  {
    for (Page *p : pages_) {
      release_page(p);
    }
    pages_.clear();
    size_ = 0;
  }

  uint32_t size() const
  {
    return size_;
  }
  uint32_t page_count() const
  {
    return uint32_t(pages_.size());
  }
  static constexpr uint32_t page_slots()
  {
    return kPage;
  }

  SlotWord get(uint32_t i) const
  {
    return pages_[int(i >> Shift)]->w[i & kMask];
  }

  /** Writes a word, copying a shared page first. Releases the node the word
   * held (the caller transfers one `holders` reference for a new node word). */
  void set(uint32_t i, SlotWord w)
  {
    Page *p = unique_page(i >> Shift);
    const SlotWord old = p->w[i & kMask];
    p->w[i & kMask] = w;
    if (word_is_node(old)) {
      Node::release(reinterpret_cast<Node *>(old));
    }
  }

  /** Appends a word (transfers one `holders` reference for a node word). */
  void push_back(SlotWord w)
  {
    if ((size_ & kMask) == 0) {
      pages_.append(new_page());
    }
    const uint32_t i = size_++;
    unique_page(i >> Shift)->w[i & kMask] = w;
  }

  bool shares_page_with(const SlotTable &o, uint32_t pi) const
  {
    return pi < page_count() && pi < o.page_count() && pages_[int(pi)] == o.pages_[int(pi)];
  }
  /** Bytes of slot pages (for benchmarks). */
  static constexpr size_t page_bytes()
  {
    return sizeof(Page);
  }
};

/* ------------------------------------------------------------------------
 * InternIndex
 * ---------------------------------------------------------------------- */

struct IndexRec {
  uint32_t h32;
  uint32_t slot_plus1; /* 0 empty, kTomb deleted */
};
constexpr uint32_t kTomb = 0xffffffffu;

class IndexTable {
public:
  std::atomic<uint32_t> rc{1};
  void *block = nullptr;
  uint32_t cap = 0; /* power of two */
  uint32_t used = 0; /* live records */
  uint32_t tombs = 0;
  IndexRec *recs = nullptr;

  static IndexTable *make(uint32_t cap)
  {
    const size_t bytes = sizeof(IndexTable) + 16 + size_t(cap) * sizeof(IndexRec);
    IndexTable *t = alloc_with_header<IndexTable>("attr::InternIndex", bytes, 16);
    t->cap = cap;
    t->recs = reinterpret_cast<IndexRec *>(reinterpret_cast<char *>(t) + ((sizeof(IndexTable) + 15) & ~size_t(15)));
    memset(static_cast<void *>(t->recs), 0, size_t(cap) * sizeof(IndexRec));
    return t;
  }
  static IndexTable *copy(const IndexTable *s)
  {
    IndexTable *t = make(s->cap);
    memcpy(static_cast<void *>(t->recs), static_cast<const void *>(s->recs), size_t(s->cap) * sizeof(IndexRec));
    t->used = s->used;
    t->tombs = s->tombs;
    return t;
  }
  static void release(IndexTable *t)
  {
    if (t && t->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      void *block = t->block;
      t->~IndexTable();
      alloc::release(block);
    }
  }
  static IndexTable *retain(IndexTable *t)
  {
    if (t) {
      t->rc.fetch_add(1, std::memory_order_relaxed);
    }
    return t;
  }
  bool unique() const
  {
    return rc.load(std::memory_order_acquire) == 1;
  }
  size_t bytes() const
  {
    return size_t(cap) * sizeof(IndexRec);
  }

  void insert(uint32_t h32, uint32_t slot)
  {
    uint32_t i = h32 & (cap - 1);
    while (recs[i].slot_plus1 != 0 && recs[i].slot_plus1 != kTomb) {
      i = (i + 1) & (cap - 1);
    }
    if (recs[i].slot_plus1 == kTomb) {
      tombs--;
    }
    recs[i] = IndexRec{h32, slot + 1};
    used++;
  }
  bool erase(uint32_t h32, uint32_t slot)
  {
    for (uint32_t i = h32 & (cap - 1), n = 0; recs[i].slot_plus1 != 0 && n < cap; i = (i + 1) & (cap - 1), n++) {
      if (recs[i].h32 == h32 && recs[i].slot_plus1 == slot + 1) {
        recs[i].slot_plus1 = kTomb;
        used--;
        tombs++;
        return true;
      }
    }
    return false;
  }
  /** Calls f(slot) for each record with this h32 until f returns true. */
  template <typename F> bool find(uint32_t h32, F &&f) const
  {
    for (uint32_t i = h32 & (cap - 1), n = 0; recs[i].slot_plus1 != 0 && n < cap; i = (i + 1) & (cap - 1), n++) {
      if (recs[i].slot_plus1 != kTomb && recs[i].h32 == h32 && f(recs[i].slot_plus1 - 1)) {
        return true;
      }
    }
    return false;
  }
};

inline uint32_t index_capacity_for(uint32_t live)
{
  uint32_t c = 16;
  while (c / 2 < live) {
    c <<= 1;
  }
  return c;
}

class InternIndex {
  IndexTable *base_ = nullptr;
  IndexTable *delta_ = nullptr;
  uint32_t stale_ = 0; /* lazy removals while the base was shared */
  uint64_t copied_bytes_ = 0;

  void own_delta()
  {
    if (!delta_) {
      delta_ = IndexTable::make(64);
      copied_bytes_ += delta_->bytes();
    }
    else if (!delta_->unique()) {
      IndexTable *c = IndexTable::copy(delta_);
      copied_bytes_ += c->bytes();
      IndexTable::release(delta_);
      delta_ = c;
    }
    if ((delta_->used + delta_->tombs + 1) * 4 > delta_->cap * 3) {
      IndexTable *g = IndexTable::make(index_capacity_for(delta_->used + 1) * 2);
      copied_bytes_ += g->bytes();
      for (uint32_t i = 0; i < delta_->cap; i++) {
        const IndexRec r = delta_->recs[i];
        if (r.slot_plus1 != 0 && r.slot_plus1 != kTomb) {
          g->insert(r.h32, r.slot_plus1 - 1);
        }
      }
      IndexTable::release(delta_);
      delta_ = g;
    }
  }

public:
  InternIndex() = default;
  InternIndex(const InternIndex &b)
      : base_(IndexTable::retain(b.base_)), delta_(IndexTable::retain(b.delta_)), stale_(b.stale_)
  {
  }
  InternIndex(InternIndex &&b) noexcept : base_(b.base_), delta_(b.delta_), stale_(b.stale_)
  {
    b.base_ = b.delta_ = nullptr;
    b.stale_ = 0;
  }
  InternIndex &operator=(const InternIndex &b)
  {
    if (this != &b) {
      InternIndex tmp(b);
      *this = std::move(tmp);
    }
    return *this;
  }
  InternIndex &operator=(InternIndex &&b) noexcept
  {
    if (this != &b) {
      IndexTable::release(base_);
      IndexTable::release(delta_);
      base_ = b.base_;
      delta_ = b.delta_;
      stale_ = b.stale_;
      b.base_ = b.delta_ = nullptr;
      b.stale_ = 0;
    }
    return *this;
  }
  ~InternIndex()
  {
    IndexTable::release(base_);
    IndexTable::release(delta_);
  }

  /** Bytes allocated because the base was shared (delta tables, their copies,
   * materialisations) so far (benchmarks). */
  uint64_t copied_bytes() const
  {
    return copied_bytes_;
  }

  /** Rebuilds from scratch; slots(f) must call f(h32, slot) for every live slot. */
  template <typename ForEachLive> void rebuild(uint32_t live, ForEachLive &&slots)
  {
    IndexTable *t = IndexTable::make(index_capacity_for(live));
    if (base_ && !base_->unique()) {
      copied_bytes_ += t->bytes(); /* materialisation of a shared base */
    }
    slots([&](uint32_t h32, uint32_t slot) { t->insert(h32, slot); });
    IndexTable::release(base_);
    IndexTable::release(delta_);
    base_ = t;
    delta_ = nullptr;
    stale_ = 0;
  }

  /** True if the index should be rebuilt from the slot table before the next insert. */
  bool needs_rebuild(uint32_t live) const
  {
    if (!base_) {
      return true;
    }
    if (base_->unique() && !delta_) {
      return (base_->used + base_->tombs + 1) * 4 > base_->cap * 3 || base_->tombs * 4 > base_->cap;
    }
    const uint32_t dl = delta_ ? delta_->used : 0;
    const uint32_t lim = live / 8 > 1024 ? live / 8 : 1024;
    return dl + stale_ > lim || (base_->unique() && (base_->used + base_->tombs + 1) * 4 > base_->cap * 3);
  }

  void insert(uint32_t h32, uint32_t slot)
  {
    if (base_->unique() && !delta_) {
      base_->insert(h32, slot);
      return;
    }
    own_delta();
    delta_->insert(h32, slot);
  }

  void erase(uint32_t h32, uint32_t slot)
  {
    if (delta_) {
      if (!delta_->unique()) {
        own_delta();
      }
      if (delta_->erase(h32, slot)) {
        return;
      }
    }
    if (base_->unique()) {
      base_->erase(h32, slot);
    }
    else {
      stale_++;
    }
  }

  /** f(slot) -> bool verifies a candidate; returns the first verified slot or -1. */
  template <typename F> int64_t find(uint32_t h32, F &&verify) const
  {
    int64_t found = -1;
    auto f = [&](uint32_t s) {
      if (verify(s)) {
        found = s;
        return true;
      }
      return false;
    };
    if (delta_ && delta_->find(h32, f)) {
      return found;
    }
    if (base_ && base_->find(h32, f)) {
      return found;
    }
    return -1;
  }

  /** Every non-stale record (for audit): f(h32, slot). */
  template <typename F> void for_each(F &&f) const
  {
    for (const IndexTable *t : {base_, delta_}) {
      if (!t) {
        continue;
      }
      for (uint32_t i = 0; i < t->cap; i++) {
        if (t->recs[i].slot_plus1 != 0 && t->recs[i].slot_plus1 != kTomb) {
          f(t->recs[i].h32, t->recs[i].slot_plus1 - 1);
        }
      }
    }
  }
};

} // namespace litestl::attr::detail
