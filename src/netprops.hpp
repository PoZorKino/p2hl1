#pragma once

#include "sdk.hpp"

namespace netprops {

int server(void *server_game_dll, const char *class_name, const char *prop);
int client(void *client_dll, const char *class_name, const char *prop);

}
