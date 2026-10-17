#pragma once

#include "sdk.hpp"

namespace movement {

bool init(CreateInterfaceFn engine_factory, CreateInterfaceFn server_factory);
void frame();
void shutdown();

}
