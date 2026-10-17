#include "netprops.hpp"

namespace netprops {

namespace {

constexpr int kGetAllServerClasses = 10;
constexpr int kGetAllClasses = 8;

constexpr int kSendOffsetMask = 0xFFFF;

int search_send(SendTable *table, const char *prop) {
	if (!table) return -1;
	for (int i = 0; i < table->m_nProps; ++i) {
		auto p = &table->m_pProps[i];
		if (p->m_pVarName && !std::strcmp(p->m_pVarName, prop)) return p->m_Offset & kSendOffsetMask;
	}
	for (int i = 0; i < table->m_nProps; ++i) {
		auto p = &table->m_pProps[i];
		if (p->m_Type != DPT_DataTable || !p->m_pDataTable) continue;
		int sub = search_send(p->m_pDataTable, prop);
		if (sub >= 0) return (p->m_Offset & kSendOffsetMask) + sub;
	}
	return -1;
}

int search_recv(RecvTable *table, const char *prop) {
	if (!table) return -1;
	for (int i = 0; i < table->m_nProps; ++i) {
		auto p = &table->m_pProps[i];
		if (p->m_pVarName && !std::strcmp(p->m_pVarName, prop)) return p->m_Offset;
	}
	for (int i = 0; i < table->m_nProps; ++i) {
		auto p = &table->m_pProps[i];
		if (p->m_RecvType != DPT_DataTable || !p->m_pDataTable) continue;
		int sub = search_recv(p->m_pDataTable, prop);
		if (sub >= 0) return p->m_Offset + sub;
	}
	return -1;
}

}

int server(void *server_game_dll, const char *class_name, const char *prop) {
	using Fn = ServerClass *(__thiscall *)(void *);
	for (auto c = vfunc<Fn>(server_game_dll, kGetAllServerClasses)(server_game_dll); c; c = c->m_pNext) {
		if (!std::strcmp(c->m_pNetworkName, class_name)) return search_send(c->m_pTable, prop);
	}
	return -1;
}

int client(void *client_dll, const char *class_name, const char *prop) {
	using Fn = ClientClass *(__thiscall *)(void *);
	for (auto c = vfunc<Fn>(client_dll, kGetAllClasses)(client_dll); c; c = c->m_pNext) {
		if (!std::strcmp(c->m_pNetworkName, class_name)) return search_recv(c->m_pRecvTable, prop);
	}
	return -1;
}

}
