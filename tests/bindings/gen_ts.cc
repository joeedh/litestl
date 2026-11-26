/* Writes the fixture's generated TypeScript to <outdir>: gen_ts <outdir> */

#include "fixture.h"

#include "../../binding/generators/typescript.h"
#include "../../util/alloc.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: %s <outdir>\n", argv[0]);
    return 1;
  }
  using namespace litestl;
  binding::BindingManager *mgr = fixture::fixtureManager();

  util::Vector<const binding::BindingBase *> types;
  for (auto *type : mgr->getBindings().values()) {
    types.append(type);
  }
  auto *files = binding::generators::generateTypescript(types);

  std::filesystem::path root(argv[1]);
  for (auto &key : files->keys()) {
    std::filesystem::path p = root / key.c_str();
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    const auto &body = files->lookup(key);
    out.write(body.c_str(), body.size());
  }
  alloc::Delete(files);
  return 0;
}
