#include "rtti.hpp"

#include <windows.h>

#include <cstring>

namespace rtti {

namespace {

struct Section {
	uintptr_t begin;
	uintptr_t end;
};

bool section(HMODULE mod, const char *name, Section &out) {
	auto base = reinterpret_cast<uintptr_t>(mod);
	auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
	auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
	auto sec = IMAGE_FIRST_SECTION(nt);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
		if (!std::strncmp(reinterpret_cast<const char *>(sec->Name), name, 8)) {
			out.begin = base + sec->VirtualAddress;
			out.end = out.begin + sec->Misc.VirtualSize;
			return true;
		}
	}
	return false;
}

uintptr_t find_dword(const Section &s, uintptr_t value, uintptr_t from = 0) {
	for (uintptr_t p = from ? from : s.begin; p + 4 <= s.end; p += 4) {
		if (*reinterpret_cast<uint32_t *>(p) == value) return p;
	}
	return 0;
}

}

void **vtable(const char *module, const char *mangled_class) {
	auto mod = GetModuleHandleA(module);
	if (!mod) return nullptr;
	Section data, rdata;
	if (!section(mod, ".data", data) || !section(mod, ".rdata", rdata)) return nullptr;

	auto len = std::strlen(mangled_class);
	uintptr_t type_desc = 0;
	for (uintptr_t p = data.begin; p + len + 1 <= data.end; ++p) {
		if (!std::memcmp(reinterpret_cast<void *>(p), mangled_class, len + 1)) {
			type_desc = p - 8;
			break;
		}
	}
	if (!type_desc) return nullptr;

	for (uintptr_t ref = find_dword(rdata, type_desc); ref; ref = find_dword(rdata, type_desc, ref + 4)) {
		auto col = ref - 12;
		auto sig = *reinterpret_cast<uint32_t *>(col);
		auto offset = *reinterpret_cast<uint32_t *>(col + 4);
		if (sig != 0 || offset != 0) continue;
		auto meta = find_dword(rdata, col);
		if (meta) return reinterpret_cast<void **>(meta + 4);
	}
	return nullptr;
}

}
