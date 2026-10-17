#include "movement.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

void msg(const char *fmt, ...) {
	using MsgFn = void (*)(const char *, ...);
	static auto fn = reinterpret_cast<MsgFn>(GetProcAddress(GetModuleHandleA("tier0.dll"), "Msg"));
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (fn) fn("%s", buf);
}

class IServerPluginCallbacks {
public:
	virtual bool Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory) = 0;
	virtual void Unload() = 0;
	virtual void Pause() = 0;
	virtual void UnPause() = 0;
	virtual const char *GetPluginDescription() = 0;
	virtual void LevelInit(const char *map) = 0;
	virtual void ServerActivate(void *edicts, int count, int max_clients) = 0;
	virtual void GameFrame(bool simulating) = 0;
	virtual void LevelShutdown() = 0;
	virtual void ClientFullyConnect(void *edict) = 0;
	virtual void ClientActive(void *edict) = 0;
	virtual void ClientDisconnect(void *edict) = 0;
	virtual void ClientPutInServer(void *edict, const char *name) = 0;
	virtual void SetCommandClient(int index) = 0;
	virtual void ClientSettingsChanged(void *edict) = 0;
	virtual int ClientConnect(bool *allow, void *edict, const char *name, const char *address, char *reject, int maxlen) = 0;
	virtual int ClientCommand(void *edict, const void *&args) = 0;
	virtual int NetworkIDValidated(const char *user, const char *id) = 0;
	virtual void OnQueryCvarValueFinished(int cookie, void *edict, int status, const char *name, const char *value) = 0;
	virtual void OnEdictAllocated(void *edict) = 0;
	virtual void OnEdictFreed(const void *edict) = 0;
};

class Plugin final : public IServerPluginCallbacks {
public:
	bool Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory) override {
		if (loaded) {
			msg("hl1mov: already loaded\n");
			return false;
		}
		loaded = movement::init(interfaceFactory, gameServerFactory);
		return loaded;
	}
	void Unload() override {
		if (!loaded) return;
		movement::shutdown();
		loaded = false;
	}
	void Pause() override {}
	void UnPause() override {}
	const char *GetPluginDescription() override { return "hl1mov: Half-Life 1 movement for Portal 2"; }
	void LevelInit(const char *) override {}
	void ServerActivate(void *, int, int) override {}
	void GameFrame(bool) override {
		if (loaded) movement::frame();
	}
	void LevelShutdown() override {}
	void ClientFullyConnect(void *) override {}
	void ClientActive(void *) override {}
	void ClientDisconnect(void *) override {}
	void ClientPutInServer(void *, const char *) override {}
	void SetCommandClient(int) override {}
	void ClientSettingsChanged(void *) override {}
	int ClientConnect(bool *, void *, const char *, const char *, char *, int) override { return 0; }
	int ClientCommand(void *, const void *&) override { return 0; }
	int NetworkIDValidated(const char *, const char *) override { return 0; }
	void OnQueryCvarValueFinished(int, void *, int, const char *, const char *) override {}
	void OnEdictAllocated(void *) override {}
	void OnEdictFreed(const void *) override {}

private:
	bool loaded = false;
};

static Plugin g_plugin;

extern "C" __declspec(dllexport) void *CreateInterface(const char *name, int *rc) {
	if (!std::strcmp(name, "ISERVERPLUGINCALLBACKS002")) {
		if (rc) *rc = 0;
		return static_cast<IServerPluginCallbacks *>(&g_plugin);
	}
	if (rc) *rc = 1;
	return nullptr;
}
