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
constexpr int kFullWalkMove = 30;
constexpr int kStartGravity = 33;
constexpr int kFinishGravity = 34;
constexpr int kTryPlayerMove = 39;
constexpr int kCheckWater = 48;
constexpr int kCheckParameters = 51;
constexpr int kCheckFalling = 53;
constexpr int kDuck = 55;
constexpr int kFinishUnDuck = 57;
constexpr int kFinishDuck = 58;
constexpr int kSetDuckedEyeOffset = 64;
constexpr int kSetGroundEntity = 69;
constexpr int kCategorizePosition = 50;

constexpr int kPlayer = 4;
constexpr int kMove = 8;
constexpr int kGravityDir = 1932;

constexpr int kMvViewAngles = 12;
constexpr int kMvButtons = 36;
constexpr int kMvOldButtons = 40;
constexpr int kMvForward = 44;
constexpr int kMvSide = 48;
constexpr int kMvUp = 52;
constexpr int kMvMaxSpeed = 56;
constexpr int kMvClientMaxSpeed = 60;
constexpr int kMvVelocity = 64;
constexpr int kMvOrigin = 156;

constexpr int kGlobalsFrametime = 16;
constexpr int kGlobalsMaxClients = 20;
constexpr int kGlobalsTickInterval = 28;
constexpr int kConVarName = 12;
constexpr int kConVarFloat = 44;

constexpr float kGravity = 800.0f;
constexpr float kAccelerate = 10.0f;
constexpr float kAirAccelerate = 10.0f;
constexpr float kFriction = 4.0f;
constexpr float kStopSpeed = 100.0f;
constexpr float kStepSize = 18.0f;
constexpr float kAirSpeedCap = 30.0f;
constexpr float kGroundMaxUpSpeed = 180.0f;
constexpr float kJumpSpeed = 268.3281573f;
constexpr float kBunnyJumpFactor = 1.7f;
constexpr float kDuckOffset = 18.0f;
constexpr int kDuckTimeMs = 1000;
constexpr int kDuckFinishMs = 600;
constexpr float kDuckSpeed = 0.333f;
constexpr int kMaxSubsteps = 20;
constexpr float kYawSnap = 60.0f;

using VoidFn = void(__thiscall *)(void *);
using BoolFn = bool(__thiscall *)(void *);
using EyeFn = void(__thiscall *)(void *, float);
using TraceFn = void(__thiscall *)(void *, const Vec3 &, const Vec3 &, unsigned int, int, Trace &);
using TryMoveFn = int(__thiscall *)(void *, Vec3 *, Trace *);
using SetGroundFn = void(__thiscall *)(void *, Trace *);
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
	int water_level;
	int base_velocity;
	int tractor_beam;
	int water_jump_time;
	int ent_gravity;
	void **globals;
	ConVar **gravity_var;
	float yaw_from;
	float yaw_to;
	float yaw_progress;
	int yaw_span;
	int yaw_ticks;
	bool yaw_init;
	int jump_queue;
	int ground_frames;
	int edge_frames;
	int air_standing;
	int air_ducked;
	float air_wish_standing;
	float air_wish_ducked;
	int prev_buttons;
	int hl1_moves;
	int portal_moves;
	int jumps;
	float speed;
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
ConVar *hl1_debug;
ConVar *hl1_fps;
ConVar *hl1_jumpbuffer;

ConVar *cl_forwardspeed;
ConVar *cl_backspeed;
ConVar *cl_sidespeed;

int g_debug_ticks;

Vec3 operator+(const Vec3 &a, const Vec3 &b) {
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(const Vec3 &a, const Vec3 &b) {
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(const Vec3 &a, float f) {
	return {a.x * f, a.y * f, a.z * f};
}

float dot(const Vec3 &a, const Vec3 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length(const Vec3 &v) {
	return std::sqrt(dot(v, v));
}

float angle_diff(float from, float to) {
	float d = std::fmod(to - from, 360.0f);
	if (d > 180.0f) d -= 360.0f;
	if (d < -180.0f) d += 360.0f;
	return d;
}

float normalize(Vec3 &v) {
	float len = length(v);
	if (len > 0.0f) v = v * (1.0f / len);
	return len;
}

void angle_vectors(const Vec3 &angles, Vec3 &forward, Vec3 &right) {
	constexpr float kDegToRad = 3.14159265358979f / 180.0f;
	float sp = std::sin(angles.x * kDegToRad), cp = std::cos(angles.x * kDegToRad);
	float sy = std::sin(angles.y * kDegToRad), cy = std::cos(angles.y * kDegToRad);
	float sr = std::sin(angles.z * kDegToRad), cr = std::cos(angles.z * kDegToRad);
	forward = {cp * cy, cp * sy, -sp};
	right = {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
}

bool readable(const void *p, size_t len) {
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
	if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
	auto end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	return reinterpret_cast<uintptr_t>(p) + len <= end;
}

struct Code {
	std::vector<int> load_disps;
	std::vector<void **> abs_vars;
};

Code scan_code(const uint8_t *fn, int len) {
	Code c;
	for (int i = 0; i + 6 <= len; ++i) {
		const uint8_t *p = fn + i;
		if (p[0] == 0xF3 && p[1] == 0x0F && p[2] == 0x10 && i + 8 <= len) {
			uint8_t modrm = p[3];
			if ((modrm >> 6) == 2 && (modrm & 7) != 4) {
				c.load_disps.push_back(*reinterpret_cast<const int *>(p + 4));
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
	if (!readable(finish, 200)) return false;

	Code fc = scan_code(finish, 200);
	if (fc.load_disps.size() >= 2) {
		s.water_jump_time = fc.load_disps[0];
		s.ent_gravity = fc.load_disps[1];
	}
	for (auto var : fc.abs_vars) {
		if (!readable(var, 4)) continue;
		void *ptr = *var;
		if (is_convar(ptr, "sv_gravity"))
			s.gravity_var = reinterpret_cast<ConVar **>(var);
		else if (is_globals(ptr))
			s.globals = var;
	}

	msg("hl1mov: %s code waterjump=%d gravity=%d globals=%p sv_gravity=%p\n", s.name, s.water_jump_time, s.ent_gravity, s.globals, s.gravity_var);
	return s.water_jump_time > 0 && s.ent_gravity > 0 && s.globals && s.gravity_var;
}

template <typename T>
T orig(const Side &s, int slot) {
	return reinterpret_cast<T>(s.orig_vt[slot]);
}

void *player(void *gm) {
	return field<void *>(gm, kPlayer);
}

void *move(void *gm) {
	return field<void *>(gm, kMove);
}

Vec3 &velocity(void *gm) {
	return field<Vec3>(move(gm), kMvVelocity);
}

Vec3 &origin(void *gm) {
	return field<Vec3>(move(gm), kMvOrigin);
}

float frametime(const Side &s) {
	return field<float>(*s.globals, kGlobalsFrametime);
}

bool active(void *gm) {
	if (!cvars::get_int(hl1_movement)) return false;
	if (!player(gm) || !move(gm)) return false;
	return field<Vec3>(gm, kGravityDir).z < -0.99f;
}

bool on_ground(const Side &s, void *gm) {
	return (field<int>(player(gm), s.flags) & FL_ONGROUND) != 0;
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

void set_ground(void *gm, Trace *tr) {
	vfunc<SetGroundFn>(gm, kSetGroundEntity)(gm, tr);
}

void fly_move(void *gm) {
	vfunc<TryMoveFn>(gm, kTryPlayerMove)(gm, nullptr, nullptr);
}

float spline(float value, float scale) {
	value *= scale;
	float sq = value * value;
	return 3.0f * sq - 2.0f * sq * value;
}

void gravity_difference(const Side &s, void *gm) {
	float ent = field<float>(player(gm), s.ent_gravity);
	if (ent == 0.0f) ent = 1.0f;
	float game = field<float>(*s.gravity_var, kConVarFloat);
	velocity(gm).z -= (kGravity - game) * ent * 0.5f * frametime(s);
}

void add_correct_gravity(const Side &s, void *gm) {
	orig<VoidFn>(s, kStartGravity)(gm);
	gravity_difference(s, gm);
}

void fixup_gravity_velocity(const Side &s, void *gm) {
	if (field<float>(player(gm), s.water_jump_time) != 0.0f) return;
	orig<VoidFn>(s, kFinishGravity)(gm);
	gravity_difference(s, gm);
}

void categorize_position(const Side &s, void *gm) {
	vfunc<BoolFn>(gm, kCheckWater)(gm);
	Vec3 &org = origin(gm);
	if (velocity(gm).z > kGroundMaxUpSpeed) {
		set_ground(gm, nullptr);
		return;
	}
	Vec3 point{org.x, org.y, org.z - 2.0f};
	Trace tr;
	trace_hull(gm, org, point, tr);
	if (tr.normal.z < 0.7f) {
		set_ground(gm, nullptr);
		return;
	}
	Vec3 vel = velocity(gm);
	set_ground(gm, &tr);
	velocity(gm) = vel;
	if (field<uint8_t>(player(gm), s.water_level) < 2 && !tr.startsolid && !tr.allsolid) org = tr.endpos;
}

void wish(void *gm, Vec3 &wishdir, float &wishspeed) {
	void *mv = move(gm);
	Vec3 forward, right;
	angle_vectors(field<Vec3>(mv, kMvViewAngles), forward, right);
	forward.z = 0.0f;
	right.z = 0.0f;
	normalize(forward);
	normalize(right);
	float fmove = field<float>(mv, kMvForward);
	float smove = field<float>(mv, kMvSide);
	wishdir = {forward.x * fmove + right.x * smove, forward.y * fmove + right.y * smove, 0.0f};
	wishspeed = normalize(wishdir);
	float maxspeed = field<float>(mv, kMvMaxSpeed);
	if (wishspeed > maxspeed) wishspeed = maxspeed;
}

void accelerate(const Side &s, void *gm, const Vec3 &wishdir, float wishspeed, float accel) {
	if (field<float>(player(gm), s.water_jump_time) != 0.0f) return;
	Vec3 &vel = velocity(gm);
	float addspeed = wishspeed - dot(vel, wishdir);
	if (addspeed <= 0.0f) return;
	float accelspeed = accel * frametime(s) * wishspeed;
	if (accelspeed > addspeed) accelspeed = addspeed;
	vel = vel + wishdir * accelspeed;
}

void air_accelerate(const Side &s, void *gm, const Vec3 &wishdir, float wishspeed, float accel) {
	if (field<float>(player(gm), s.water_jump_time) != 0.0f) return;
	Vec3 &vel = velocity(gm);
	float wishspd = wishspeed > kAirSpeedCap ? kAirSpeedCap : wishspeed;
	float addspeed = wishspd - dot(vel, wishdir);
	if (addspeed <= 0.0f) return;
	float accelspeed = accel * wishspeed * frametime(s);
	if (accelspeed > addspeed) accelspeed = addspeed;
	vel = vel + wishdir * accelspeed;
}

void friction(Side &s, void *gm) {
	++s.ground_frames;
	if (field<float>(player(gm), s.water_jump_time) != 0.0f) return;
	Vec3 &vel = velocity(gm);
	float speed = length(vel);
	if (speed < 0.1f) return;

	const Vec3 &org = origin(gm);
	float half = field<bool>(player(gm), s.ducked) ? kDuckOffset : kDuckOffset * 2.0f;
	Vec3 start{org.x + vel.x / speed * 16.0f, org.y + vel.y / speed * 16.0f, org.z};
	Vec3 stop{start.x, start.y, org.z - half - 34.0f};
	Trace tr;
	trace_hull(gm, start, stop, tr);

	float fric = kFriction;
	bool edge = tr.fraction == 1.0f && !tr.startsolid && !tr.allsolid;
	if (edge) {
		fric *= cvars::get_float(hl1_edgefriction);
		++s.edge_frames;
	}

	float control = speed < kStopSpeed ? kStopSpeed : speed;
	float newspeed = speed - control * fric * frametime(s);
	if (newspeed < 0.0f) newspeed = 0.0f;
	vel = vel * (newspeed / speed);
}

void walk_move(const Side &s, void *gm, const Vec3 &basevel) {
	Vec3 wishdir;
	float wishspeed;
	wish(gm, wishdir, wishspeed);

	Vec3 &vel = velocity(gm);
	vel.z = 0.0f;
	accelerate(s, gm, wishdir, wishspeed, kAccelerate);
	vel.z = 0.0f;
	vel = vel + basevel;

	if (length(vel) < 1.0f) {
		vel = {0.0f, 0.0f, 0.0f};
		return;
	}

	Vec3 &org = origin(gm);
	float ft = frametime(s);
	Vec3 dest{org.x + vel.x * ft, org.y + vel.y * ft, org.z};
	Trace tr;
	trace_hull(gm, org, dest, tr);
	if (tr.fraction == 1.0f) {
		org = tr.endpos;
		return;
	}
	if (field<float>(player(gm), s.water_jump_time) != 0.0f) return;

	Vec3 original = org;
	Vec3 originalvel = vel;

	fly_move(gm);
	Vec3 down = org;
	Vec3 downvel = vel;

	org = original;
	vel = originalvel;

	dest = org;
	dest.z += kStepSize;
	trace_hull(gm, org, dest, tr);
	if (!tr.startsolid && !tr.allsolid) org = tr.endpos;

	fly_move(gm);

	dest = org;
	dest.z -= kStepSize;
	trace_hull(gm, org, dest, tr);
	bool use_down = tr.normal.z < 0.7f;
	if (!use_down) {
		if (!tr.startsolid && !tr.allsolid) org = tr.endpos;
		Vec3 up = org;
		float downdist = (down.x - original.x) * (down.x - original.x) + (down.y - original.y) * (down.y - original.y);
		float updist = (up.x - original.x) * (up.x - original.x) + (up.y - original.y) * (up.y - original.y);
		use_down = downdist > updist;
	}
	if (use_down) {
		org = down;
		vel = downvel;
	} else {
		vel.z = downvel.z;
	}
}

void air_move(Side &s, void *gm, const Vec3 &basevel) {
	Vec3 wishdir;
	float wishspeed;
	wish(gm, wishdir, wishspeed);
	if (field<int>(player(gm), s.flags) & FL_DUCKING) {
		++s.air_ducked;
		s.air_wish_ducked += wishspeed;
	} else {
		++s.air_standing;
		s.air_wish_standing += wishspeed;
	}
	air_accelerate(s, gm, wishdir, wishspeed, kAirAccelerate);
	velocity(gm) = velocity(gm) + basevel;
	fly_move(gm);
}

template <int S>
void jump(void *gm, bool buffered) {
	Side &s = g_side[S];
	void *mv = move(gm);
	int &old = field<int>(mv, kMvOldButtons);

	if (!on_ground(s, gm)) {
		if (!buffered) old |= IN_JUMP;
		return;
	}
	if (!buffered && (old & IN_JUMP)) return;
	s.jump_queue = 0;

	set_ground(gm, nullptr);

	Vec3 &vel = velocity(gm);
	if (cvars::get_int(hl1_bhopcap)) {
		float cap = kBunnyJumpFactor * field<float>(mv, kMvMaxSpeed);
		float spd = length(vel);
		if (cap > 0.0f && spd > cap) vel = vel * (cap / spd * 0.65f);
	}
	vel.z = kJumpSpeed;
	fixup_gravity_velocity(s, gm);
	old |= IN_JUMP;
	++s.jumps;
}

template <int S>
void hl1_step(void *gm);

template <int S>
void __fastcall hk_full_walk_move(void *gm, void *) {
	Side &s = g_side[S];
	auto call = orig<VoidFn>(s, kFullWalkMove);
	void *pl = player(gm);
	bool portal = !active(gm) || field<float>(pl, s.water_jump_time) != 0.0f || field<uint8_t>(pl, s.water_level) >= 2 ||
		(s.tractor_beam > 0 && field<uint32_t>(pl, s.tractor_beam) != 0xFFFFFFFFu);
	if (portal) {
		++s.portal_moves;
		return call(gm);
	}

	++s.hl1_moves;
	void *mv = move(gm);
	float &globals_ft = field<float>(*s.globals, kGlobalsFrametime);
	float tick_ft = globals_ft;
	float &yaw = field<Vec3>(mv, kMvViewAngles).y;
	float cmd_yaw = yaw;

	int buttons = field<int>(mv, kMvButtons);
	if ((buttons & IN_JUMP) && !(s.prev_buttons & IN_JUMP)) s.jump_queue = cvars::get_int(hl1_jumpbuffer) + 1;
	s.prev_buttons = buttons;

	int steps = 1;
	float fps = cvars::get_float(hl1_fps);
	if (fps > 0.0f && tick_ft > 0.0f) {
		steps = static_cast<int>(fps * tick_ft + 0.5f);
		if (steps < 1) steps = 1;
		if (steps > kMaxSubsteps) steps = kMaxSubsteps;
	}

	if (!s.yaw_init || angle_diff(s.yaw_to, cmd_yaw) != 0.0f) {
		float cur = s.yaw_from + angle_diff(s.yaw_from, s.yaw_to) * s.yaw_progress;
		if (!s.yaw_init || std::fabs(angle_diff(cur, cmd_yaw)) > kYawSnap) cur = cmd_yaw;
		s.yaw_span = s.yaw_ticks < 1 ? 1 : (s.yaw_ticks > 2 ? 2 : s.yaw_ticks);
		s.yaw_from = cur;
		s.yaw_to = cmd_yaw;
		s.yaw_progress = 0.0f;
		s.yaw_ticks = 0;
		s.yaw_init = true;
	}
	++s.yaw_ticks;

	globals_ft = tick_ft / steps;
	for (int i = 0; i < steps; ++i) {
		if (steps > 1) {
			s.yaw_progress += 1.0f / (s.yaw_span * steps);
			if (s.yaw_progress > 1.0f) s.yaw_progress = 1.0f;
			yaw = s.yaw_from + angle_diff(s.yaw_from, s.yaw_to) * s.yaw_progress;
		} else {
			s.yaw_progress = 1.0f;
		}
		hl1_step<S>(gm);
	}
	globals_ft = tick_ft;
	yaw = cmd_yaw;
	if (s.jump_queue > 0) --s.jump_queue;

	vfunc<VoidFn>(gm, kCheckFalling)(gm);

	const Vec3 &vel = velocity(gm);
	s.speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
}

template <int S>
void hl1_step(void *gm) {
	Side &s = g_side[S];
	void *pl = player(gm);
	void *mv = move(gm);

	add_correct_gravity(s, gm);

	if (s.jump_queue > 0 && on_ground(s, gm))
		jump<S>(gm, true);
	else if (field<int>(mv, kMvButtons) & IN_JUMP)
		jump<S>(gm, false);
	else
		field<int>(mv, kMvOldButtons) &= ~IN_JUMP;

	if (on_ground(s, gm)) {
		velocity(gm).z = 0.0f;
		friction(s, gm);
	}

	Vec3 basevel = field<Vec3>(pl, s.base_velocity);
	if (on_ground(s, gm))
		walk_move(s, gm, basevel);
	else
		air_move(s, gm, basevel);

	categorize_position(s, gm);
	velocity(gm) = velocity(gm) - basevel;

	if (field<uint8_t>(pl, s.water_level) < 2) fixup_gravity_velocity(s, gm);
	if (on_ground(s, gm)) velocity(gm).z = 0.0f;
}

template <int S>
void __fastcall hk_check_parameters(void *gm, void *) {
	auto call = orig<VoidFn>(g_side[S], kCheckParameters);
	if (!active(gm)) return call(gm);

	void *mv = move(gm);
	float &fwd = field<float>(mv, kMvForward);
	float &side = field<float>(mv, kMvSide);
	float &up = field<float>(mv, kMvUp);
	float &ms = field<float>(mv, kMvMaxSpeed);

	float input = cvars::get_float(hl1_inputspeed);
	if (input > 0.0f) {
		float fs = cvars::get_float(fwd >= 0.0f ? cl_forwardspeed : cl_backspeed);
		float ss = cvars::get_float(cl_sidespeed);
		if (fs > 0.0f) fwd *= input / fs;
		if (ss > 0.0f) side *= input / ss;
	}

	float maxspeed = cvars::get_float(hl1_maxspeed);
	if (maxspeed <= 0.0f) maxspeed = ms;
	float spd = std::sqrt(fwd * fwd + side * side + up * up);
	if (spd > maxspeed) {
		float ratio = maxspeed / spd;
		fwd *= ratio;
		side *= ratio;
		up *= ratio;
	}

	float hl1_fwd = fwd, hl1_side = side, hl1_up = up;
	call(gm);
	bool frozen = fwd == 0.0f && side == 0.0f && up == 0.0f;
	if (!frozen) {
		fwd = hl1_fwd;
		side = hl1_side;
		up = hl1_up;
	}
	ms = maxspeed;
}

template <int S>
void un_duck(void *gm) {
	const Side &s = g_side[S];
	void *pl = player(gm);
	bool ground = on_ground(s, gm);
	bool &ducked = field<bool>(pl, s.ducked);

	Vec3 target = origin(gm);
	if (ducked) {
		if (!ground) target.z -= kDuckOffset;
	} else if (ground) {
		target.z += kDuckOffset;
	}

	bool was = ducked;
	ducked = false;
	Trace tr;
	trace_hull(gm, target, target, tr);
	ducked = was;
	if (tr.startsolid) return;

	vfunc<VoidFn>(gm, kFinishUnDuck)(gm);
	origin(gm) = target;
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
	vt[kFullWalkMove] = reinterpret_cast<void *>(&hk_full_walk_move<S>);
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
	s.water_level = get("m_nWaterLevel");
	s.base_velocity = get("m_vecBaseVelocity");
	s.tractor_beam = get("m_hTractorBeam");
	msg("hl1mov: %s props flags=%d local=%d ducked=%d ducking=%d ducktime=%d water=%d basevel=%d tbeam=%d\n", s.name, s.flags, s.local, s.ducked, s.ducking, s.duck_time, s.water_level, s.base_velocity, s.tractor_beam);
	return s.flags > 0 && s.local > 0 && s.ducked > 0 && s.ducking > 0 && s.duck_time > 0 && s.water_level > 0 && s.base_velocity > 0;
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
	if (count <= kSetGroundEntity) {
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
	hl1_debug = cvars::create("hl1_debug", "0", "Print movement stats once a second.", true, 0, true, 1);
	hl1_jumpbuffer = cvars::create("hl1_jumpbuffer", "2", "Ticks a jump press is remembered before landing. Portal 2 only delivers input every 2 ticks. 0 is strict HL1.", true, 0, true, 8);
	hl1_fps = cvars::create("hl1_fps", "100", "HL1 frame rate to simulate: each tick is split into frames with interpolated view yaw. 0 runs one frame per tick.", true, 0, true, 1000);

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
		msg("hl1mov: %s moves hl1=%d portal=%d jumps=%d ground=%d edge=%d speed=%.1f frametime=%.4f\n", s.name, s.hl1_moves, s.portal_moves, s.jumps, s.ground_frames, s.edge_frames, s.speed, s.globals ? frametime(s) : -1.0f);
		msg("hl1mov: %s air standing=%d wish=%.0f ducked=%d wish=%.0f\n", s.name, s.air_standing, s.air_standing ? s.air_wish_standing / s.air_standing : 0.0f, s.air_ducked, s.air_ducked ? s.air_wish_ducked / s.air_ducked : 0.0f);
		s.hl1_moves = s.portal_moves = s.jumps = s.ground_frames = s.edge_frames = 0;
		s.air_standing = s.air_ducked = 0;
		s.air_wish_standing = s.air_wish_ducked = 0.0f;
	}
}

void shutdown() {
	uninstall(g_side[0]);
	uninstall(g_side[1]);
	cvars::shutdown();
}

}
