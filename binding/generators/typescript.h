#pragma once

#include "../binding_base.h"
#include "util/map.h"
#include "util/string.h"
#include "util/vector.h"

namespace litestl::binding::generators {
using util::string;
using util::Vector;
util::Map<string, string> *generateTypescript(Vector<const BindingBase *> &types);
} // namespace litestl::binding::generators
