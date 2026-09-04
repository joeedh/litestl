#include "platform/cpu.h"
#include "platform/platform.h"
#include "platform/time.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#if __has_include(<execinfo.h>)
#define LITESTL_HAVE_EXECINFO
#include <cxxabi.h>
#include <execinfo.h>
#include <vector>
#endif

// Line numbers come from addr2line, which needs each frame turned back into an
// offset within its own ELF object; dl_iterate_phdr supplies the load bias.
#if defined(LITESTL_HAVE_EXECINFO) && defined(__linux__) && __has_include(<link.h>)
#define LITESTL_HAVE_ADDR2LINE
#include <cstdint>
#include <cstdio>
#include <link.h>
#include <map>
#endif

namespace litestl::time {
void sleep_ms(int ms)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
void sleep_ns(int ns)
{
  std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}
} // namespace litestl::time

namespace litestl::platform {
int cpu_core_count()
{
  return 4;
}

int max_thread_count()
{
  return cpu_core_count() * 2;
}

#ifdef LITESTL_HAVE_EXECINFO
namespace {
/** Demangles the first Itanium-ABI mangled name in `symbol`, if there is one. */
std::string demangle(const char *symbol)
{
  const char *start = std::strstr(symbol, "_Z");
  if (!start) {
    return symbol;
  }

  size_t len = 0;
  while (std::isalnum(static_cast<unsigned char>(start[len])) || start[len] == '_' ||
         start[len] == '$' || start[len] == '.')
  {
    len++;
  }

  const std::string mangled(start, len);
  int status = 0;
  char *plain = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
  if (status != 0) {
    std::free(plain);
    return symbol;
  }

  std::string out(symbol, start - symbol);
  out += plain;
  out += start + len;
  std::free(plain);
  return out;
}

struct Frame {
  std::string func;
  std::string source;
};

#ifdef LITESTL_HAVE_ADDR2LINE
struct ModuleQuery {
  uintptr_t addr;
  std::string path;
  uintptr_t bias;
  bool found;
};

/** Absolute path of the running executable, empty if the link cannot be read. */
std::string exePath()
{
  char buf[4096];
  const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) {
    return "";
  }
  return std::string(buf, len);
}

int matchModule(struct dl_phdr_info *info, size_t, void *data)
{
  ModuleQuery *query = static_cast<ModuleQuery *>(data);

  for (int i = 0; i < info->dlpi_phnum; i++) {
    const ElfW(Phdr) &phdr = info->dlpi_phdr[i];
    if (phdr.p_type != PT_LOAD) {
      continue;
    }

    const uintptr_t start = info->dlpi_addr + phdr.p_vaddr;
    if (query->addr >= start && query->addr < start + phdr.p_memsz) {
      // the main executable reports an empty name, and /proc/self/exe cannot
      // stand in for it because addr2line would read its own link
      const bool named = info->dlpi_name && info->dlpi_name[0];
      query->path = named ? info->dlpi_name : exePath();
      query->bias = info->dlpi_addr;
      query->found = true;
      return 1;
    }
  }
  return 0;
}

std::string shellQuote(const std::string &path)
{
  std::string out = "'";
  for (const char c : path) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

/**
 * Fills in the function and source location of every frame addr2line can
 * place, leaving the rest untouched. One addr2line runs per loaded object.
 */
void resolveFrames(void *const *addrs, int count, std::vector<Frame> &frames)
{
  std::map<std::string, std::vector<int>> byModule;
  std::vector<uintptr_t> offsets(count, 0);

  for (int i = 0; i < count; i++) {
    ModuleQuery query = {reinterpret_cast<uintptr_t>(addrs[i]), "", 0, false};
    dl_iterate_phdr(matchModule, &query);
    if (!query.found || query.path.size() == 0) {
      continue;
    }

    // a return address points at the instruction after the call, which can sit
    // in the next line's range, or past the end of a noreturn callee entirely
    offsets[i] = query.addr - query.bias - 1;
    byModule[query.path].push_back(i);
  }

  for (const auto &entry : byModule) {
    std::string cmd = "addr2line -f -C -e " + shellQuote(entry.first);
    for (const int i : entry.second) {
      char hex[32];
      std::snprintf(hex, sizeof(hex), " 0x%zx", static_cast<size_t>(offsets[i]));
      cmd += hex;
    }
    cmd += " 2>/dev/null";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      continue;
    }

    char line[1024];
    auto readLine = [&](std::string &out) {
      if (!std::fgets(line, sizeof(line), pipe)) {
        return false;
      }
      out = line;
      while (out.size() > 0 && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
      }
      return true;
    };

    // addr2line answers each address with a function line and a source line
    for (const int i : entry.second) {
      std::string func, source;
      if (!readLine(func) || !readLine(source)) {
        break;
      }
      if (func != "??") {
        frames[i].func = func;
      }
      if (source.compare(0, 2, "??") != 0) {
        // DWARF marks several call sites on one line apart; the reader does not
        // need to tell them apart
        const size_t noise = source.find(" (discriminator ");
        frames[i].source = source.substr(0, noise);
      }
    }
    pclose(pipe);
  }
}
#endif
} // namespace

std::string getStackTrace()
{
  void *addrs[64];
  const int count = backtrace(addrs, 64);
  char **symbols = backtrace_symbols(addrs, count);
  std::vector<Frame> frames(count);

#ifdef LITESTL_HAVE_ADDR2LINE
  resolveFrames(addrs, count, frames);
#endif

  std::ostringstream out;

  // frame 0 is getStackTrace itself
  for (int i = 1; i < count; i++) {
    out << "[" << (i - 1) << "] ";

    // an object without debug info still has a symbol table, but addr2line
    // answers from it with the nearest preceding name, which is a guess
    if (frames[i].source.size() > 0) {
      // addr2line -C prints the full demangled signature
      const bool named = frames[i].func.size() > 0;
      out << (named ? frames[i].func : "UnknownFunction") << " at " << frames[i].source;
    } else if (symbols) {
      // already ends in the frame address
      out << demangle(symbols[i]);
    } else {
      out << "UnknownFunction - Address: " << addrs[i];
    }
    out << "\n";
  }

  std::free(symbols);
  return out.str();
}
#else
std::string getStackTrace()
{
  return "";
}
#endif
} // namespace litestl::platform
