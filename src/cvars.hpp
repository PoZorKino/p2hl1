#pragma once

#include "sdk.hpp"

namespace cvars {

bool init(CreateInterfaceFn engine_factory);
void shutdown();

ConVar *find(const char *name);
ConVar *create(const char *name, const char *value, const char *help, bool has_min = false, float min = 0, bool has_max = false, float max = 0);
void set(ConVar *var, const char *value);

inline ConVar *root(ConVar *var) {
	return var && var->m_pParent ? var->m_pParent : var;
}
inline float get_float(ConVar *var) {
	return var ? root(var)->m_fValue : 0.0f;
}
inline int get_int(ConVar *var) {
	return var ? root(var)->m_nValue : 0;
}

}
