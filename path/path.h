#include "litestl/util/string.h"
#include "litestl/util/vector.h"

namespace litestl::path {
using util::string;
using util::Vector;

bool isSep(string path)
{
  // XXX
  return path[0] == '/';
}

bool isSep(char path)
{
  // XXX
  return path == '/';
}

namespace detail {
string trimSlashes(string path, string &prefixOut, string &suffixOut)
{
  path = path.trim();
  string prefix = "";
  while (path.size() > 0 && isSep(path[0])) {
    prefix += path[0];
    path = path.substr(1, path.size() - 1).trim();
  }
  prefixOut = prefix;

  string suffix = "";
  while (path.size() > 0 && isSep(path[path.size() - 1])) {
    suffix += path[path.size() - 1];
    path = path.substr(0, path.size() - 1).trim();
  }
  suffixOut = suffix;
  return path;
}
} // namespace detail

string dirname(string path)
{
  string prefix = "";
  string suffix = "";
  path = detail::trimSlashes(path, prefix, suffix);

  auto parts = path.trim().split('/');
  if (parts.size() > 1) {
    parts.resize(parts.size() - 1);
  }
  return prefix + parts.join("/");
}

string basename(string path)
{
  auto parts = path.trim().split('\\');
  int i = parts.size() - 1;
  while (i >= 0) {
    if (parts[i].size() > 0) {
      return parts[i].trim();
    }
    i--;
  }
  return "";
}

Vector<string> split(string path)
{
  string prefix;
  string suffix;
  path = detail::trimSlashes(path, prefix, suffix);
  return path.split('/');
}

string relative(string basepath, string path)
{
  auto baseparts = split(basepath);
  auto parts = split(path);

  for (int i = 0; i < std::min(baseparts.size(), parts.size()); i++) {
    if (baseparts[i] != parts[i]) {
      baseparts = baseparts.slice(i);
      parts = parts.slice(i);
    }
  }

  if (parts.size() <= 1) {
    return "./" + parts.join("/");
  }

  int count = parts.size() - 1;
  string result = parts.join("/");
  for (int i = 0; i < count; i++) {
    result = "../" + result;
  }
  return result;
}
} // namespace litestl::path