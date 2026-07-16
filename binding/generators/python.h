#pragma once

#include "../binding_base.h"
#include "util/map.h"
#include "util/string.h"
#include "util/vector.h"

namespace litestl::binding::generators {

/** Emit Python .pyi stub modules for the given root types (the Python sibling
 * of generateTypescript). Returns a new Map of relative path -> file content;
 * caller owns it (alloc::Delete).
 */
util::Map<util::string, util::string> *generatePython(
    util::Vector<const BindingBase *> &types);

} // namespace litestl::binding::generators
