#pragma once

#include <cstdint>
#include <cstring>

#define FCVAR_NONE 0
#define FCVAR_NOTIFY (1 << 8)

#define IN_JUMP (1 << 1)
#define IN_DUCK (1 << 2)

#define FL_ONGROUND (1 << 0)
#define FL_DUCKING (1 << 1)

#define MASK_PLAYERSOLID 0x0201400B
#define COLLISION_GROUP_PLAYER_MOVEMENT 8

struct Vec3 {
	float x, y, z;
};

using CreateInterfaceFn = void *(*)(const char *name, int *rc);

struct ConCommandBase {
	void *vtable;
	ConCommandBase *m_pNext;
	bool m_bRegistered;
	const char *m_pszName;
	const char *m_pszHelpString;
	int m_nFlags;
};

struct ConVar : ConCommandBase {
	void *iconvar_vtable;
	ConVar *m_pParent;
	const char *m_pszDefaultValue;
	char *m_pszString;
	int m_StringLength;
	float m_fValue;
	int m_nValue;
	bool m_bHasMin;
	float m_fMinVal;
	bool m_bHasMax;
	float m_fMaxVal;
	uint8_t m_fnChangeCallback[20];
};

enum SendPropType {
	DPT_Int = 0,
	DPT_Float,
	DPT_Vector,
	DPT_VectorXY,
	DPT_String,
	DPT_Array,
	DPT_DataTable,
	DPT_Int64,
};

struct RecvTable;
struct SendTable;

struct RecvProp {
	char *m_pVarName;
	SendPropType m_RecvType;
	int m_Flags;
	int m_StringBufferSize;
	bool m_bInsideArray;
	const void *m_pExtraData;
	RecvProp *m_pArrayProp;
	void *m_ArrayLengthProxy;
	void *m_ProxyFn;
	void *m_DataTableProxyFn;
	RecvTable *m_pDataTable;
	int m_Offset;
	int m_ElementStride;
	int m_nElements;
	const char *m_pParentArrayPropName;
};

struct RecvTable {
	RecvProp *m_pProps;
	int m_nProps;
	void *m_pDecoder;
	char *m_pNetTableName;
	bool m_bInitialized;
	bool m_bInMainList;
};

struct SendProp {
	void *vtable;
	RecvProp *m_pMatchingRecvProp;
	SendPropType m_Type;
	int m_nBits;
	float m_fLowValue;
	float m_fHighValue;
	SendProp *m_pArrayProp;
	void *m_ArrayLengthProxy;
	int m_nElements;
	int m_ElementStride;
	char *m_pExcludeDTName;
	char *m_pParentArrayPropName;
	char *m_pVarName;
	float m_fHighLowMul;
	char m_priority;
	int m_Flags;
	void *m_ProxyFn;
	void *m_DataTableProxyFn;
	SendTable *m_pDataTable;
	int m_Offset;
	const void *m_pExtraData;
};

struct SendTable {
	SendProp *m_pProps;
	int m_nProps;
	char *m_pNetTableName;
	void *m_pPrecalc;
	uint8_t m_flags;
};

struct ClientClass {
	void *m_pCreateFn;
	void *m_pCreateEventFn;
	char *m_pNetworkName;
	RecvTable *m_pRecvTable;
	ClientClass *m_pNext;
	int m_ClassID;
};

struct ServerClass {
	char *m_pNetworkName;
	SendTable *m_pTable;
	ServerClass *m_pNext;
	int m_ClassID;
	int m_InstanceBaselineIndex;
};

struct alignas(4) Trace {
	Vec3 startpos;
	Vec3 endpos;
	Vec3 normal;
	float dist;
	uint8_t type;
	uint8_t signbits;
	uint8_t pad[2];
	float fraction;
	int contents;
	uint16_t dispFlags;
	bool allsolid;
	bool startsolid;
	uint8_t rest[20];
	void *ent;
	int hitbox;
};
static_assert(sizeof(Trace) == 84, "trace_t is 84 bytes in Portal 2");

template <typename T = void *>
inline T vfunc(void *obj, int index) {
	return reinterpret_cast<T>((*reinterpret_cast<void ***>(obj))[index]);
}

template <typename T>
inline T &field(void *base, int offset) {
	return *reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(base) + offset);
}

void msg(const char *fmt, ...);
