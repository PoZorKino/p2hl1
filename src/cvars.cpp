#include "cvars.hpp"

#include <vector>

namespace cvars {

namespace {

using FindFn = void *(__thiscall *)(void *icvar, const char *name);
using RegFn = void(__thiscall *)(void *icvar, ConCommandBase *cmd);
using CreateFn = void(__thiscall *)(void *var, const char *name, const char *value, int flags, const char *help, bool has_min, float min, bool has_max, float max, void *callback);
using DtorFn = void(__thiscall *)(void *var, int flags);
using SetFn = void(__thiscall *)(void *var, const char *value);

constexpr int kRegister = 9;
constexpr int kUnregister = 10;
constexpr int kFind = 13;
constexpr int kDtor = 9;
constexpr int kCreate = 27;
constexpr int kInternalSetValue = 12;

void *g_icvar;
void *g_convar_vtable;
void *g_convar_vtable2;
std::vector<ConVar *> g_owned;

}

bool init(CreateInterfaceFn engine_factory) {
	g_icvar = engine_factory("VEngineCvar007", nullptr);
	if (!g_icvar) return false;
	auto sv_lan = find("sv_lan");
	if (!sv_lan) return false;
	g_convar_vtable = sv_lan->vtable;
	g_convar_vtable2 = sv_lan->iconvar_vtable;
	return true;
}

void shutdown() {
	for (auto var : g_owned) {
		vfunc<RegFn>(g_icvar, kUnregister)(g_icvar, var);
		reinterpret_cast<DtorFn>(reinterpret_cast<void **>(g_convar_vtable2)[kDtor])(var, 0);
		::operator delete(var);
	}
	g_owned.clear();
}

ConVar *find(const char *name) {
	if (!g_icvar) return nullptr;
	return reinterpret_cast<ConVar *>(vfunc<FindFn>(g_icvar, kFind)(g_icvar, name));
}

ConVar *create(const char *name, const char *value, const char *help, bool has_min, float min, bool has_max, float max) {
	auto var = static_cast<ConVar *>(::operator new(sizeof(ConVar)));
	std::memset(var, 0, sizeof(ConVar));
	var->vtable = g_convar_vtable;
	var->iconvar_vtable = g_convar_vtable2;
	auto create_fn = reinterpret_cast<CreateFn>(reinterpret_cast<void **>(g_convar_vtable2)[kCreate]);
	create_fn(var, name, value, FCVAR_NOTIFY, help, has_min, min, has_max, max, nullptr);
	g_owned.push_back(var);
	return var;
}

void set(ConVar *var, const char *value) {
	var = root(var);
	if (!var) return;
	vfunc<SetFn>(var, kInternalSetValue)(var, value);
}

}
