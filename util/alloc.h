#pragma once

#include <utility>
#include <cstddef>

#define NO_DEBUG_ALLOC

#ifdef NO_DEBUG_ALLOC
#include <cstdio>
#include <cstdlib>
#endif

/*
 * Leak debugger allocator.
 *
 * We don't use operator new or delete, instead
 * we have alloc::New and alloc::Delete.  These
 * take a string tag identifying the allocated object
 *
 * All allocated objects (well, memory blocks) are stored in a linked list,
 * which is used to identify leaks.  This is done by calling alloc::print_blocks,
 * typically on application exit after everything has been deallocated.
 */
namespace litestl::alloc {

#ifndef NO_DEBUG_ALLOC
/** Allocates a block of memory with a named tag. Tag is usually a string literal. */
void *alloc(const char *tag, size_t size);
/** Release a block of memory allocated with alloc::alloc. */
void release(void *mem);
/** Prints all the blocks allocated by this thread. */
bool print_blocks(bool printPermanent);
/** Print a block */
void print_block(const void *mem);
/** Returns the total memory allocated by all threads. */
int getMemorySize();
/** Returns the total permanent memory allocated by all threads. */
int getPermanentMemorySize();
/** Begins a permanent allocation scope. Allocations made while active are excluded from leak reports. */
void pushPermanentAlloc();
/** Ends a permanent allocation scope. */
void popPermanentAlloc();

namespace detail {
/** Retrieves the debug tag string associated with an allocation. */
const char *getMemoryTag(void *vmem);
}
/** Retrieves the debug tag string associated with an allocation. */
template<typename T> static const char *getMemoryTag(T *mem) {
  return detail::getMemoryTag(static_cast<void*>(mem));
}

#else
static void pushPermanentAlloc() {}
static void popPermanentAlloc() {}
static int getMemorySize() {
  return -1;
}
static int getPermanentMemorySize() {
  return -1;
}
static void *alloc(const char *tag, size_t size)
{
  return malloc(size);
}
static void release(void *mem) {
  free(mem);
}
template<typename T> static const char *getMemoryTag(T *mem) {
  return nullptr;
}

static bool print_blocks(bool printPermanent) {
  return false;
}

static void print_block(const void *mem) {
}
#endif

/** Allocates and constructs a single object using placement new. */
template <typename T, typename... Args> inline T *New(const char *tag, Args... args)
{
  void *mem = alloc(tag, sizeof(T));

  return new (mem) T(std::forward<Args>(args)...);
}

/** Allocates and constructs an array of objects using placement new. Returns nullptr if size is 0. */
template <typename T, typename... Args>
inline T *NewArray(const char *tag, size_t size, Args... args)
{
  if (size == 0) {
    return nullptr;
  }

  void *mem = alloc(tag, sizeof(T) * size);
  T *elem = static_cast<T *>(mem);

  for (int i = 0; i < size; i++) {
    new (elem) T(std::forward<Args>(args)...);
  }

  return static_cast<T *>(elem);
}

/** Destructs and releases a single object allocated with New. */
template <typename T> inline void Delete(T *arg)
{
  if (arg) {
    arg->~T();
    release(static_cast<void *>(arg));
  }
}

/** Destructs and releases an array of objects allocated with NewArray. */
template <typename T> inline void DeleteArray(T *arg, size_t size)
{
  if (arg) {
    for (int i = 0; i < size; i++) {
      arg[i].~T();
    }

    release(static_cast<void *>(arg));
  }
}

/**
 * Allocate permanent things that shouldn't show up
 * in the leak list.
 * 
 * {
 *    alloc::PermanentGuard guard;
 *    string *s = alloc::New<string>("a permanent string");
 * }
 *  
 */
struct PermanentGuard {
  PermanentGuard() {
    pushPermanentAlloc();
  }
  ~PermanentGuard() {
    popPermanentAlloc();
  }
};

}; // namespace litestl::alloc
