#include "movement.hpp"

#include "cvars.hpp"
#include "netprops.hpp"
#include "rtti.hpp"

#include <windows.h>

#include <cmath>
#include <vector>

namespace movement {

namespace {

constexpr int kVtableMax = 256;

constexpr int kTracePlayerBBox = 15;
constexpr int kFriction = 23;
constexpr int kAirAccelerate = 24;
constexpr int kAirMove = 25;
constexpr int kAccelerate = 27;
constexpr int kStayOnGround = 29;
constexpr int kStartGravity = 33;
constexpr int kFinishGravity = 34;
constexpr int kCheckJumpButton = 36;
constexpr int kCategorizePosition = 50;
constexpr int kCheckParameters = 51;
constexpr int kDuck = 55;
constexpr int kFinishUnDuck = 57;
constexpr int kFinishDuck = 58;
constexpr int kSetDuckedEyeOffset = 64;

constexpr int kPlayer = 4;
constexpr int kMove = 8;
constexpr int kGravityDir = 1932;

constexpr int kMvButtons = 36;
constexpr int kMvOldButtons = 40;
constexpr int kMvForward = 44;
constexpr int kMvSide = 48;
constexpr int kMvUp = 52;
constexpr int kMvMaxSpeed = 56;
constexpr int kMvClientMaxSpeed = 60;
constexpr int kMvVelocity = 64;
constexpr int kMvOutWishVel = 104;
constexpr int kMvOrigin = 156;

constexpr int kGlobalsFrametime = 16;
constexpr int kGlobalsMaxClients = 20;
constexpr int kGlobalsTickInterval = 28;
constexpr int kConVarName = 12;
constexpr int kConVarFloat = 44;

constexpr float kGravity = 800.0f;
constexpr float kAccel = 10.0f;
constexpr float kAirAccel = 10.0f;
constexpr float kFrictionValue = 4.0f;
constexpr float kStopSpeed = 100.0f;
constexpr float kMaxSpeed = 320.0f;
constexpr float kEdgeFriction = 2.0f;
constexpr float kJumpSpeed = 268.3281573f;
constexpr float kDuckOffset = 18.0f;
constexpr int kDuckTimeMs = 1000;
constexpr int kDuckFinishMs = 600;
constexpr float kDuckSpeed = 0.333f;

using VoidFn = void(__thiscall *)(void *);
using BoolFn = bool(__thiscall *)(void *);
using AccelFn = void(__thiscall *)(void *, Vec3 &, float, float);
using EyeFn = void(__thiscall *)(void *, float);
using TraceFn = void(__thiscall *)(void *, const Vec3 &, const Vec3 &, unsigned int, int, Trace &);
using NotifyFn = void(__thiscall *)(void *, void *);

struct Side {
	const char *module;
	const char *name;
	bool server;
	void *gm;
	void **orig_vt;
	void **base_vt;
	void **hook_vt;
	int flags;
	int local;
	int ducked;
	int ducking;
	int duck_time;
	int water_jump_time;
	int ent_gravity;
	int surface_friction;
	void **globals;
	ConVar **gravity_var;
	ConVar **accelerate_var;
	int air_hl1;
	int air_portal;
	int jumps;
	float gravity_z;
	float air_speed;
};

Side g_side[2] = {
	{"server.dll", "server", true},
	{"client.dll", "client", false},
};

ConVar *hl1_movement;
ConVar *hl1_edgefriction;
ConVar *hl1_bhopcap;
ConVar *hl1_maxspeed;
ConVar *hl1_inputspeed;
ConVar *hl1_stayonground;
ConVar *hl1_debug;

ConVar *cl_forwardspeed;
ConVar *cl_backspeed;
ConVar *cl_sidespeed;

int g_debug_ticks;

bool readable(const void *p, size_t len) {
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
	if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
	auto end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	return reinterpret_cast<uintptr_t>(p) + len <= end;
}

struct Code {
	std::vector<int> load_disps;
	std::vector<int> mul_disps;
	std::vector<void **> abs_vars;
};

Code scan_code(const uint8_t *fn, int len) {
	Code c;
	for (int i = 0; i + 6 <= len; ++i) {
		const uint8_t *p = fn + i;
		if (p[0] == 0xF3 && p[1] == 0x0F && (p[2] == 0x10 || p[2] == 0x59) && i + 8 <= len) {
			uint8_t modrm = p[3];
			if ((modrm >> 6) == 2 && (modrm & 7) != 4) {
				int disp = *reinterpret_cast<const int *>(p + 4);
				(p[2] == 0x10 ? c.load_disps : c.mul_disps).push_back(disp);
				i += 7;
				continue;
			}
		}
		if (p[0] == 0xA1) {
			c.abs_vars.push_back(*reinterpret_cast<void **const *>(p + 1));
			i += 4;
			continue;
		}
		if (p[0] == 0x8B && (p[1] & 0xC7) == 0x05) {
			c.abs_vars.push_back(*reinterpret_cast<void **const *>(p + 2));
			i += 5;
			continue;
		}
	}
	return c;
}

bool is_convar(void *ptr, const char *name) {
	if (!readable(ptr, kConVarFloat + 4)) return false;
	auto str = field<const char *>(ptr, kConVarName);
	return readable(str, std::strlen(name) + 1) && !std::strcmp(str, name);
}

bool is_globals(void *ptr) {
	if (!readable(ptr, 64)) return false;
	float tick = field<float>(ptr, kGlobalsTickInterval);
	int clients = field<int>(ptr, kGlobalsMaxClients);
	return tick > 0.001f && tick < 0.1f && clients >= 1 && clients <= 256;
}

bool resolve_code(Side &s, void **portal_vt) {
	auto finish = reinterpret_cast<const uint8_t *>(portal_vt[kFinishGravity]);
	auto accel = reinterpret_cast<const uint8_t *>(s.base_vt[kAirAccelerate]);
	auto walk = reinterpret_cast<const uint8_t *>(portal_vt[28]);
	if (!readable(finish, 200) || !readable(accel, 250) || !readable(walk, 3000)) return false;

	Code fc = scan_code(finish, 200);
	if (fc.load_disps.size() >= 2) {
		s.water_jump_time = fc.load_disps[0];
		s.ent_gravity = fc.load_disps[1];
	}
	for (auto var : fc.abs_vars) {
		if (!readable(var, 4)) continue;
		void *ptr = *var;
		if (is_convar(ptr, "sv_gravity")) s.gravity_var = reinterpret_cast<ConVar **>(var);
		else if (is_globals(ptr)) s.globals = var;
	}

	Code ac = scan_code(accel, 250);
	for (int disp : ac.mul_disps) {
		if (disp > 1000 && disp < 20000) {
			s.surface_friction = disp;
			break;
		}
	}

	Code wc = scan_code(walk, 3000);
	for (auto var : wc.abs_vars) {
		if (readable(var, 4) && is_convar(*var, "sv_accelerate")) {
			s.accelerate_var = reinterpret_cast<ConVar **>(var);
			break;
		}
	}

	msg("hl1mov: %s code waterjump=%d gravity=%d surface=%d globals=%p sv_gravity=%p sv_accelerate=%p\n", s.name, s.water_jump_time, s.ent_gravity, s.surface_friction, s.globals, s.gravity_var, s.accelerate_var);
	return s.water_jump_time > 0 && s.ent_gravity > 0 && s.surface_friction > 0 && s.globals && s.gravity_var && s.accelerate_var;
}

template <typename T>
T orig(const Side &s, int slot) {
	return reinterpret_cast<T>(s.orig_vt[slot]);
}

template <typename T>
T base(const Side &s, int slot) {
	return reinterpret_cast<T>(s.base_vt[slot]);
}

void *player(void *gm) {
	return field<void *>(gm, kPlayer);
}

void *move(void *gm) {
	return field<void *>(gm, kMove);
}

float frametime(const Side &s) {
	return field<float>(*s.globals, kGlobalsFrametime);
}

float game_gravity(const Side &s) {
	return field<float>(*s.gravity_var, kConVarFloat);
}

float game_accelerate(const Side &s) {
	return field<float>(*s.accelerate_var, kConVarFloat);
}

bool active(void *gm) {
	if (!cvars::get_int(hl1_movement)) return false;
	if (!player(gm) || !move(gm)) return false;
	return field<Vec3>(gm, kGravityDir).z < -0.99f;
}

template <typename T>
void set_local(const Side &s, void *pl, int offset, T value) {
	T &ref = field<T>(pl, offset);
	if (ref == value) return;
	if (s.server) {
		void *local = reinterpret_cast<uint8_t *>(pl) + s.local;
		vfunc<NotifyFn>(local, 0)(local, &ref);
	}
	ref = value;
}

void trace_hull(void *gm, const Vec3 &start, const Vec3 &end, Trace &tr) {
	std::memset(&tr, 0, sizeof(tr));
	vfunc<TraceFn>(gm, kTracePlayerBBox)(gm, start, end, MASK_PLAYERSOLID, COLLISION_GROUP_PLAYER_MOVEMENT, tr);
}

float spline(float value, float scale) {
	value *= scale;
	float sq = value * value;
	return 3.0f * sq - 2.0f * sq * value;
}

float length(const Vec3 &v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

void add_gravity_difference(const Side &s, void *gm) {
	void *pl = player(gm);
	float ent = field<float>(pl, s.ent_gravity);
	if (ent == 0.0f) ent = 1.0f;
	float extra = (kGravity - game_gravity(s)) * ent * 0.5f * frametime(s);
	field<Vec3>(move(gm), kMvVelocity).z -= extra;
}

template <int S>
void __fastcall hk_start_gravity(void *gm, void *) {
	const Side &s = g_side[S];
	orig<VoidFn>(s, kStartGravity)(gm);
	if (active(gm)) add_gravity_difference(s, gm);
}

template <int S>
void __fastcall hk_finish_gravity(void *gm, void *) {
	const Side &s = g_side[S];
	orig<VoidFn>(s, kFinishGravity)(gm);
	if (active(gm) && field<float>(player(gm), s.water_jump_time) == 0.0f) add_gravity_difference(s, gm);
}

bool over_edge(const Side &s, void *gm, const Vec3 &vel, float speed) {
	void *pl = player(gm);
	const Vec3 &org = field<Vec3>(move(gm), kMvOrigin);
	float half = field<bool>(pl, s.ducked) ? kDuckOffset : kDuckOffset * 2.0f;
	Vec3 start{org.x + vel.x / speed * 16.0f, org.y + vel.y / speed * 16.0f, org.z - half};
	Vec3 stop{start.x, start.y, start.z - 34.0f};
	Trace tr;
	trace_hull(gm, start, stop, tr);
	return tr.fraction == 1.0f && !tr.startsolid;
}

template <int S>
void __fastcall hk_friction(void *gm, void *) {
	const Side &s = g_side[S];
	if (!active(gm)) return orig<VoidFn>(s, kFriction)(gm);

	void *pl = player(gm);
	void *mv = move(gm);
	if (field<float>(pl, s.water_jump_time) != 0.0f) return;

	Vec3 &vel = field<Vec3>(mv, kMvVelocity);
	float speed = length(vel);
	if (speed < 0.1f) return;

	float friction = kFrictionValue * field<float>(pl, s.surface_friction);
	if (field<int>(pl, s.flags) & FL_ONGROUND) {
		float edge = cvars::get_float(hl1_edgefriction);
		if (edge != 1.0f && over_edge(s, gm, vel, speed)) friction *= edge;
	}

	float control = speed < kStopSpeed ? kStopSpeed : speed;
	float drop = control * friction * frametime(s);
	float newspeed = speed - drop;
	if (newspeed < 0.0f) newspeed = 0.0f;
	if (newspeed == speed) return;
	newspeed /= speed;

	Vec3 &wish = field<Vec3>(mv, kMvOutWishVel);
	wish.x -= (1.0f - newspeed) * vel.x;
	wish.y -= (1.0f - newspeed) * vel.y;
	wish.z -= (1.0f - newspeed) * vel.z;
	vel.x *= newspeed;
	vel.y *= newspeed;
	vel.z *= newspeed;
}

template <int S>
void __fastcall hk_accelerate(void *gm, void *, Vec3 &wishdir, float wishspeed, float accel) {
	const Side &s = g_side[S];
	if (active(gm) && accel == game_accelerate(s)) accel = kAccel;
	orig<AccelFn>(s, kAccelerate)(gm, wishdir, wishspeed, accel);
}

template <int S>
void __fastcall hk_air_accelerate(void *gm, void *, Vec3 &wishdir, float wishspeed, float accel) {
	const Side &s = g_side[S];
	if (!active(gm)) return orig<AccelFn>(s, kAirAccelerate)(gm, wishdir, wishspeed, accel);
	base<AccelFn>(s, kAirAccelerate)(gm, wishdir, wishspeed, kAirAccel);
}

template <int S>
void __fastcall hk_air_move(void *gm, void *) {
	Side &s = g_side[S];
	bool on = active(gm);
	++(on ? s.air_hl1 : s.air_portal);
	s.gravity_z = field<Vec3>(gm, kGravityDir).z;
	auto call = on ? base<VoidFn>(s, kAirMove) : orig<VoidFn>(s, kAirMove);
	call(gm);
	if (move(gm)) {
		const Vec3 &vel = field<Vec3>(move(gm), kMvVelocity);
		s.air_speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
	}
}

template <int S>
void __fastcall hk_stay_on_ground(void *gm, void *) {
	if (active(gm) && !cvars::get_int(hl1_stayonground)) return;
	orig<VoidFn>(g_side[S], kStayOnGround)(gm);
}

template <int S>
void __fastcall hk_check_parameters(void *gm, void *) {
	if (active(gm)) {
		void *mv = move(gm);
		float maxspeed = cvars::get_float(hl1_maxspeed);
		if (maxspeed > 0.0f) {
			float &ms = field<float>(mv, kMvMaxSpeed);
			float &cms = field<float>(mv, kMvClientMaxSpeed);
			if (ms < maxspeed) ms = maxspeed;
			if (cms != 0.0f && cms < maxspeed) cms = maxspeed;
		}
		float input = cvars::get_float(hl1_inputspeed);
		if (input > 0.0f) {
			float &fwd = field<float>(mv, kMvForward);
			float &side = field<float>(mv, kMvSide);
			float fs = cvars::get_float(fwd >= 0.0f ? cl_forwardspeed : cl_backspeed);
			float ss = cvars::get_float(cl_sidespeed);
			if (fs > 0.0f) fwd *= input / fs;
			if (ss > 0.0f) side *= input / ss;
		}
	}
	orig<VoidFn>(g_side[S], kCheckParameters)(gm);
}

template <int S>
bool __fastcall hk_check_jump_button(void *gm, void *) {
	Side &s = g_side[S];
	auto call = orig<BoolFn>(s, kCheckJumpButton);
	if (!active(gm)) return call(gm);

	void *pl = player(gm);
	void *mv = move(gm);
	int &flags = field<int>(pl, s.flags);
	bool ducking = (flags & FL_DUCKING) != 0;
	Vec3 before = field<Vec3>(mv, kMvVelocity);

	if (ducking) flags &= ~FL_DUCKING;
	bool jumped = call(gm);
	if (ducking) flags |= FL_DUCKING;
	if (!jumped) return false;
	++s.jumps;

	Vec3 &vel = field<Vec3>(mv, kMvVelocity);
	if (vel.z - before.z > kJumpSpeed * 1.5f) return true;

	if (cvars::get_int(hl1_bhopcap)) {
		float cap = 1.7f * field<float>(mv, kMvMaxSpeed);
		float spd = std::sqrt(vel.x * vel.x + vel.y * vel.y);
		if (cap > 0.0f && spd > cap) {
			float scale = cap / spd * 0.65f;
			vel.x *= scale;
			vel.y *= scale;
		}
	}
	vel.z = kJumpSpeed;
	return true;
}

template <int S>
void un_duck(void *gm) {
	const Side &s = g_side[S];
	void *pl = player(gm);
	void *mv = move(gm);
	bool on_ground = (field<int>(pl, s.flags) & FL_ONGROUND) != 0;
	bool &ducked = field<bool>(pl, s.ducked);

	Vec3 target = field<Vec3>(mv, kMvOrigin);
	if (ducked) {
		if (!on_ground) target.z -= kDuckOffset;
	} else if (on_ground) {
		target.z += kDuckOffset;
	}

	bool was = ducked;
	ducked = false;
	Trace tr;
	trace_hull(gm, target, target, tr);
	ducked = was;
	if (tr.startsolid) return;

	vfunc<VoidFn>(gm, kFinishUnDuck)(gm);
	field<Vec3>(mv, kMvOrigin) = target;
	vfunc<VoidFn>(gm, kCategorizePosition)(gm);
}

template <int S>
void __fastcall hk_duck(void *gm, void *) {
	const Side &s = g_side[S];
	if (!active(gm)) return orig<VoidFn>(s, kDuck)(gm);

	void *pl = player(gm);
	void *mv = move(gm);
	int buttons = field<int>(mv, kMvButtons);
	int &old = field<int>(mv, kMvOldButtons);
	int pressed = (old ^ buttons) & buttons;
	if (buttons & IN_DUCK)
		old |= IN_DUCK;
	else
		old &= ~IN_DUCK;

	int flags = field<int>(pl, s.flags);
	bool ducking = (flags & FL_DUCKING) != 0;
	if (ducking) {
		field<float>(mv, kMvForward) *= kDuckSpeed;
		field<float>(mv, kMvSide) *= kDuckSpeed;
		field<float>(mv, kMvUp) *= kDuckSpeed;
	}

	bool in_duck = field<bool>(pl, s.ducking);
	if (!(buttons & IN_DUCK) && !in_duck && !ducking) return;
	if (!(buttons & IN_DUCK)) return un_duck<S>(gm);

	if ((pressed & IN_DUCK) && !ducking) {
		set_local<int>(s, pl, s.duck_time, kDuckTimeMs);
		set_local<bool>(s, pl, s.ducking, true);
		in_duck = true;
	}
	if (!in_duck) return;

	int duck_time = field<int>(pl, s.duck_time);
	if (duck_time <= kDuckFinishMs || !(flags & FL_ONGROUND)) {
		vfunc<VoidFn>(gm, kFinishDuck)(gm);
	} else {
		float t = 1.0f - duck_time / static_cast<float>(kDuckTimeMs);
		if (t < 0.0f) t = 0.0f;
		vfunc<EyeFn>(gm, kSetDuckedEyeOffset)(gm, spline(t, 2.5f));
	}
}

template <int S>
void fill_hooks(void **vt) {
	vt[kFriction] = reinterpret_cast<void *>(&hk_friction<S>);
	vt[kAirAccelerate] = reinterpret_cast<void *>(&hk_air_accelerate<S>);
	vt[kAirMove] = reinterpret_cast<void *>(&hk_air_move<S>);
	vt[kAccelerate] = reinterpret_cast<void *>(&hk_accelerate<S>);
	vt[kStayOnGround] = reinterpret_cast<void *>(&hk_stay_on_ground<S>);
	vt[kStartGravity] = reinterpret_cast<void *>(&hk_start_gravity<S>);
	vt[kFinishGravity] = reinterpret_cast<void *>(&hk_finish_gravity<S>);
	vt[kCheckJumpButton] = reinterpret_cast<void *>(&hk_check_jump_button<S>);
	vt[kCheckParameters] = reinterpret_cast<void *>(&hk_check_parameters<S>);
	vt[kDuck] = reinterpret_cast<void *>(&hk_duck<S>);
}

CreateInterfaceFn module_factory(const char *module) {
	auto mod = GetModuleHandleA(module);
	return mod ? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(mod, "CreateInterface")) : nullptr;
}

bool resolve_props(Side &s, void *dll) {
	auto get = [&](const char *prop) {
		return s.server ? netprops::server(dll, "CPortal_Player", prop) : netprops::client(dll, "CPortal_Player", prop);
	};
	s.flags = get("m_fFlags");
	s.local = get("m_Local");
	s.ducked = get("m_bDucked");
	s.ducking = get("m_bDucking");
	s.duck_time = get("m_nDuckTimeMsecs");
	msg("hl1mov: %s props flags=%d local=%d ducked=%d ducking=%d ducktime=%d\n", s.name, s.flags, s.local, s.ducked, s.ducking, s.duck_time);
	return s.flags > 0 && s.local > 0 && s.ducked > 0 && s.ducking > 0 && s.duck_time > 0;
}

template <int S>
bool install(CreateInterfaceFn factory, void *dll) {
	Side &s = g_side[S];
	if (!factory || !dll) {
		msg("hl1mov: %s interfaces missing\n", s.name);
		return false;
	}
	if (!resolve_props(s, dll)) return false;

	s.gm = factory("GameMovement001", nullptr);
	void **portal_vt = rtti::vtable(s.module, ".?AVCPortalGameMovement@@");
	s.base_vt = rtti::vtable(s.module, ".?AVCGameMovement@@");
	if (!s.gm || !portal_vt || !s.base_vt) {
		msg("hl1mov: %s lookup failed gm=%p portal=%p base=%p\n", s.name, s.gm, portal_vt, s.base_vt);
		return false;
	}
	if (!resolve_code(s, portal_vt)) {
		msg("hl1mov: %s could not read movement code\n", s.name);
		return false;
	}

	void **current = *reinterpret_cast<void ***>(s.gm);
	if (current != portal_vt) {
		int same = 0;
		for (int i = 0; i < 80; ++i) same += current[i] == portal_vt[i];
		if (same < 40) {
			msg("hl1mov: %s GameMovement001 vtable %p is not CPortalGameMovement %p\n", s.name, current, portal_vt);
			return false;
		}
		msg("hl1mov: %s movement already hooked by another plugin, chaining\n", s.name);
	}

	int count = 0;
	while (count < kVtableMax && current[count]) ++count;
	if (count <= kSetDuckedEyeOffset) {
		msg("hl1mov: %s movement vtable too short (%d)\n", s.name, count);
		return false;
	}

	s.orig_vt = current;
	s.hook_vt = new void *[count + 1];
	std::memcpy(s.hook_vt + 1, current, sizeof(void *) * count);
	s.hook_vt[0] = portal_vt[-1];
	fill_hooks<S>(s.hook_vt + 1);
	*reinterpret_cast<void ***>(s.gm) = s.hook_vt + 1;
	return true;
}

void uninstall(Side &s) {
	if (s.gm && s.orig_vt && s.hook_vt) {
		void **&vt = *reinterpret_cast<void ***>(s.gm);
		if (vt == s.hook_vt + 1) {
			vt = s.orig_vt;
			delete[] s.hook_vt;
		} else {
			msg("hl1mov: %s movement vtable was replaced after load, leaving it\n", s.name);
		}
	}
	s.hook_vt = nullptr;
	s.orig_vt = nullptr;
	s.gm = nullptr;
}

}

bool init(CreateInterfaceFn engine_factory, CreateInterfaceFn server_factory) {
	if (!cvars::init(engine_factory)) {
		msg("hl1mov: ICvar not found\n");
		return false;
	}

	hl1_movement = cvars::create("hl1_movement", "1", "Half-Life 1 movement.", true, 0, true, 1);
	hl1_edgefriction = cvars::create("hl1_edgefriction", "2", "Friction multiplier near ledges (HL1 edgefriction).", true, 0);
	hl1_bhopcap = cvars::create("hl1_bhopcap", "0", "Cap bunnyhop speed at 1.7x maxspeed like later HL1 patches.", true, 0, true, 1);
	hl1_maxspeed = cvars::create("hl1_maxspeed", "320", "Player max speed.", true, 0);
	hl1_inputspeed = cvars::create("hl1_inputspeed", "400", "Movement key speed (cl_forwardspeed/sidespeed/backspeed in HL1), 0 keeps Portal 2's.", true, 0);
	hl1_stayonground = cvars::create("hl1_stayonground", "0", "Keep Source's ground snapping on slopes and stairs.", true, 0, true, 1);
	hl1_debug = cvars::create("hl1_debug", "0", "Print hook activity once a second.", true, 0, true, 1);

	cl_forwardspeed = cvars::find("cl_forwardspeed");
	cl_backspeed = cvars::find("cl_backspeed");
	cl_sidespeed = cvars::find("cl_sidespeed");

	auto client_factory = module_factory("client.dll");
	void *server_dll = server_factory ? server_factory("ServerGameDLL005", nullptr) : nullptr;
	void *client_dll = client_factory ? client_factory("VClient016", nullptr) : nullptr;

	if (!install<0>(server_factory, server_dll) || !install<1>(client_factory, client_dll)) {
		shutdown();
		return false;
	}

	msg("hl1mov: loaded, hl1_movement %d\n", cvars::get_int(hl1_movement));
	return true;
}

void frame() {
	if (!cvars::get_int(hl1_debug) || ++g_debug_ticks < 60) return;
	g_debug_ticks = 0;
	for (auto &s : g_side) {
		msg("hl1mov: %s air hl1=%d portal=%d jumps=%d gravdir.z=%.2f airspeed=%.1f frametime=%.4f\n", s.name, s.air_hl1, s.air_portal, s.jumps, s.gravity_z, s.air_speed, s.globals ? frametime(s) : -1.0f);
		s.air_hl1 = s.air_portal = s.jumps = 0;
	}
}

void shutdown() {
	uninstall(g_side[0]);
	uninstall(g_side[1]);
	cvars::shutdown();
}

}
