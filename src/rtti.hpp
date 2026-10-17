#pragma once

#include <cstdint>

namespace rtti {

void **vtable(const char *module, const char *mangled_class);

}
