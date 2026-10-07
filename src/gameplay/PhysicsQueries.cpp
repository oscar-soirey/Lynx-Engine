#include "PhysicsQueries.h"

#include "Actor.h"
#include "BoxColliderComponent.h"
#include "Private/ECS.h"
#include "../core/Engine.h"
#include "../core/Voxels.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace lynx::physics
{
	namespace
	{
		constexpr float kInf = std::numeric_limits<float>::infinity();
		// A trace never walks more voxels than this.
		constexpr int kMaxVoxelSteps = 200000;

		bool g_debug_all = false;

		// ---- Debug drawings ------------------------------------------------

		struct DebugShape
		{
			enum Kind { Segment, Circle, Box, Point } kind = Segment;
			float a[4] = {};     // segment : ax ay bx by / circle : cx cy r / box : cx cy w h / point : x y
			float color[3] = {};
			float time_left = 0.f;
		};

		std::vector<DebugShape> g_shapes;

		void Draw(const DebugShape& s)
		{
			const uint32_t scene = Engine::GetScene();
			const float z = 0.f;
			const float r = s.color[0], g = s.color[1], b = s.color[2];
			switch (s.kind)
			{
			case DebugShape::Segment:
				HRL_DrawDebugSegment(scene, s.a[0], s.a[1], z, s.a[2], s.a[3], z, r, g, b);
				break;
			case DebugShape::Circle:
				HRL_DrawDebugCircle(scene, HRL_DEBUG_HOLLOW, s.a[0], s.a[1], z, s.a[2], 32, r, g, b);
				break;
			case DebugShape::Box:
			{
				const float hw = s.a[2] * 0.5f, hh = s.a[3] * 0.5f;
				const float xs[] = { s.a[0] - hw, s.a[0] + hw, s.a[0] + hw, s.a[0] - hw };
				const float ys[] = { s.a[1] - hh, s.a[1] - hh, s.a[1] + hh, s.a[1] + hh };
				const float zs[] = { z, z, z, z };
				HRL_DrawDebugPolygon(scene, HRL_DEBUG_HOLLOW, xs, ys, zs, 4, r, g, b);
				break;
			}
			case DebugShape::Point:
				HRL_DrawDebugPoint(scene, s.a[0], s.a[1], z, 8.f, r, g, b);
				break;
			}
		}

		void AddShape(const QueryParams& p, DebugShape::Kind kind, std::initializer_list<float> a, bool red)
		{
			if (!p.debug_draw && !g_debug_all)
				return;
			DebugShape s;
			s.kind = kind;
			int i = 0;
			for (float v : a)
				if (i < 4)
					s.a[i++] = v;
			s.color[0] = red ? 1.f : 0.2f;
			s.color[1] = red ? 0.2f : 1.f;
			s.color[2] = 0.2f;
			s.time_left = p.debug_duration;
			Draw(s);
			if (s.time_left > 0.f)
				g_shapes.push_back(s);
		}

		// ---- Colliders -----------------------------------------------------

		bool Ignored(const QueryParams& p, const Actor* a)
		{
			return std::find(p.ignore.begin(), p.ignore.end(), a) != p.ignore.end();
		}

		std::vector<ColliderComponent*> Candidates(const QueryParams& p)
		{
			std::vector<ColliderComponent*> all;
			if (!p.actors)
				return all;
			ecs::CollectComponents(all);
			std::vector<ColliderComponent*> out;
			out.reserve(all.size());
			for (ColliderComponent* c : all)
			{
				Actor* owner = c ? c->GetOwner() : nullptr;
				if (!owner || Ignored(p, owner))
					continue;
				if (c->trigger && !p.triggers)
					continue;
				if ((c->layer & p.layer_mask) == 0u)
					continue;
				out.push_back(c);
			}
			return out;
		}

		// Segment (o + d * t, t in [0, 1]) against a box. Returns t, or -1.
		float SegmentBox(const vec2& o, const vec2& d, float cx, float cy, float w, float h, vec2& normal, bool& inside)
		{
			const float minx = cx - w * 0.5f, maxx = cx + w * 0.5f;
			const float miny = cy - h * 0.5f, maxy = cy + h * 0.5f;

			inside = o.x > minx && o.x < maxx && o.y > miny && o.y < maxy;
			if (inside)
				return 0.f;

			float tmin = 0.f, tmax = 1.f;
			vec2 n;
			const float origin[2] = { o.x, o.y };
			const float dir[2] = { d.x, d.y };
			const float lo[2] = { minx, miny };
			const float hi[2] = { maxx, maxy };

			for (int axis = 0; axis < 2; ++axis)
			{
				if (std::fabs(dir[axis]) < 1e-9f)
				{
					if (origin[axis] < lo[axis] || origin[axis] > hi[axis])
						return -1.f;
					continue;
				}
				float t1 = (lo[axis] - origin[axis]) / dir[axis];
				float t2 = (hi[axis] - origin[axis]) / dir[axis];
				float sign = -1.f;   // entering through the low side : normal -axis
				if (t1 > t2)
				{
					std::swap(t1, t2);
					sign = 1.f;
				}
				if (t1 > tmin)
				{
					tmin = t1;
					n = axis == 0 ? vec2(sign, 0.f) : vec2(0.f, sign);
				}
				tmax = std::min(tmax, t2);
				if (tmin > tmax)
					return -1.f;
			}
			normal = n;
			return tmin;
		}

		// ---- Voxels ----------------------------------------------------------

		uint32_t VoxelMask(const QueryParams& p)
		{
			if (p.voxel_flags != 0u)
				return p.voxel_flags;
			return HRL_VOXEL_COLLISION_LEFT | HRL_VOXEL_COLLISION_RIGHT |
			       HRL_VOXEL_COLLISION_TOP | HRL_VOXEL_COLLISION_BOTTOM;
		}

		bool VoxelBlocks(const QueryParams& p, uint32_t mask, int x, int y, uint8_t& type)
		{
			type = voxels::GetTypeAt(x, y);
			if (type == 0)
				return false;
			const uint32_t flags = voxels::GetFlags(type);
			if ((flags & mask) == 0u)
				return false;
			// voxel_filter.types / ignore_types / flags also apply to the traces.
			return p.voxel_filter.Accepts(type, flags, false);
		}

		// First voxel along the trace (DDA in voxel space). t in [0, 1].
		bool TraceVoxels(const QueryParams& p, const vec2& from, const vec2& to, HitResult& out)
		{
			if (!p.voxels)
				return false;

			const uint32_t scene = Engine::GetScene();
			float x0, y0, x1, y1;
			if (HRL_WorldToVoxelCoordinates(scene, from.x, from.y, &x0, &y0) != HRL_TRUE ||
			    HRL_WorldToVoxelCoordinates(scene, to.x, to.y, &x1, &y1) != HRL_TRUE)
				return false;

			const uint32_t mask = VoxelMask(p);
			const float dx = x1 - x0, dy = y1 - y0;

			int cx = static_cast<int>(std::floor(x0));
			int cy = static_cast<int>(std::floor(y0));
			const int ex = static_cast<int>(std::floor(x1));
			const int ey = static_cast<int>(std::floor(y1));

			uint8_t type = 0;
			if (VoxelBlocks(p, mask, cx, cy, type))
			{
				out.hit = true;
				out.initial_overlap = true;
				out.fraction = 0.f;
				out.voxel = true;
				out.voxel_x = cx;
				out.voxel_y = cy;
				out.voxel_type = type;
				const float len = std::sqrt(dx * dx + dy * dy);
				out.normal = len > 0.f ? vec2(-dx / len, -dy / len) : vec2(0.f, 1.f);
				return true;
			}

			const int step_x = dx > 0.f ? 1 : -1;
			const int step_y = dy > 0.f ? 1 : -1;
			const float delta_x = dx != 0.f ? 1.f / std::fabs(dx) : kInf;
			const float delta_y = dy != 0.f ? 1.f / std::fabs(dy) : kInf;
			float tmax_x = dx != 0.f ? (dx > 0.f ? (cx + 1 - x0) : (x0 - cx)) * delta_x : kInf;
			float tmax_y = dy != 0.f ? (dy > 0.f ? (cy + 1 - y0) : (y0 - cy)) * delta_y : kInf;

			for (int i = 0; i < kMaxVoxelSteps; ++i)
			{
				if (cx == ex && cy == ey)
					break;

				float t;
				vec2 n;
				if (tmax_x < tmax_y)
				{
					t = tmax_x;
					cx += step_x;
					tmax_x += delta_x;
					n = vec2(static_cast<float>(-step_x), 0.f);
				}
				else
				{
					t = tmax_y;
					cy += step_y;
					tmax_y += delta_y;
					n = vec2(0.f, static_cast<float>(-step_y));
				}

				if (t > 1.f)
					break;

				if (VoxelBlocks(p, mask, cx, cy, type))
				{
					out.hit = true;
					out.fraction = std::max(0.f, t);
					out.normal = n;
					out.voxel = true;
					out.voxel_x = cx;
					out.voxel_y = cy;
					out.voxel_type = type;
					return true;
				}
			}
			return false;
		}

		void Finish(HitResult& h, const vec2& from, const vec2& to)
		{
			const vec2 d = to - from;
			h.point = from + d * h.fraction;
			h.distance = d.length() * h.fraction;
		}

		// Every collider hit along the trace.
		std::vector<HitResult> TraceColliders(const QueryParams& p, const vec2& from, const vec2& to)
		{
			std::vector<HitResult> hits;
			const vec2 d = to - from;
			const float len = d.length();

			for (ColliderComponent* c : Candidates(p))
			{
				float cx, cy, w, h;
				c->GetWorldBox(cx, cy, w, h);
				vec2 n;
				bool inside = false;
				const float t = SegmentBox(from, d, cx, cy, w, h, n, inside);
				if (t < 0.f)
					continue;

				HitResult r;
				r.hit = true;
				r.initial_overlap = inside;
				r.fraction = t;
				r.normal = inside ? (len > 0.f ? vec2(-d.x / len, -d.y / len) : vec2(0.f, 1.f)) : n;
				r.actor = c->GetOwner();
				r.collider = c;
				hits.push_back(r);
			}
			return hits;
		}

		void DrawTrace(const QueryParams& p, const vec2& from, const vec2& to, const HitResult* hit)
		{
			if (!p.debug_draw && !g_debug_all)
				return;
			if (!hit || !hit->hit)
			{
				AddShape(p, DebugShape::Segment, { from.x, from.y, to.x, to.y }, false);
				return;
			}
			AddShape(p, DebugShape::Segment, { from.x, from.y, hit->point.x, hit->point.y }, false);
			AddShape(p, DebugShape::Segment, { hit->point.x, hit->point.y, to.x, to.y }, true);
			AddShape(p, DebugShape::Point, { hit->point.x, hit->point.y }, true);
			AddShape(p, DebugShape::Segment,
			         { hit->point.x, hit->point.y, hit->point.x + hit->normal.x, hit->point.y + hit->normal.y }, true);
		}

		bool CircleTouchesBox(float px, float py, float r, float cx, float cy, float w, float h)
		{
			const float qx = std::clamp(px, cx - w * 0.5f, cx + w * 0.5f);
			const float qy = std::clamp(py, cy - h * 0.5f, cy + h * 0.5f);
			return (qx - px) * (qx - px) + (qy - py) * (qy - py) <= r * r;
		}

		voxels::VoxelFilter OverlapFilter(const QueryParams& p)
		{
			voxels::VoxelFilter f = p.voxel_filter;
			if (f.flags == 0u)
				f.flags = p.voxel_flags;
			return f;
		}
	}


	HitResult Raycast(const vec2& from, const vec2& to, const QueryParams& params)
	{
		HitResult best;
		if (!HRL_IsValidScene(Engine::GetScene()))
			return best;

		HitResult v;
		if (TraceVoxels(params, from, to, v))
			best = v;

		for (const HitResult& h : TraceColliders(params, from, to))
		{
			if (!best.hit || h.fraction < best.fraction)
				best = h;
		}

		if (best.hit)
			Finish(best, from, to);
		DrawTrace(params, from, to, &best);
		return best;
	}

	std::vector<HitResult> RaycastAll(const vec2& from, const vec2& to, const QueryParams& params)
	{
		std::vector<HitResult> out;
		if (!HRL_IsValidScene(Engine::GetScene()))
			return out;

		out = TraceColliders(params, from, to);
		HitResult v;
		if (TraceVoxels(params, from, to, v))
			out.push_back(v);

		std::sort(out.begin(), out.end(), [](const HitResult& a, const HitResult& b) { return a.fraction < b.fraction; });

		// Stop at the first blocking hit (included).
		for (size_t i = 0; i < out.size(); ++i)
		{
			const bool blocking = out[i].voxel || (out[i].collider && !out[i].collider->trigger);
			if (blocking)
			{
				out.resize(i + 1);
				break;
			}
		}

		for (HitResult& h : out)
			Finish(h, from, to);

		DrawTrace(params, from, to, out.empty() ? nullptr : &out.back());
		return out;
	}

	bool LineOfSight(const vec2& from, const vec2& to, const QueryParams& params)
	{
		return !Raycast(from, to, params).hit;
	}

	OverlapResult OverlapBox(const vec2& center, const vec2& size, const QueryParams& params)
	{
		OverlapResult o;
		if (!HRL_IsValidScene(Engine::GetScene()))
			return o;

		const float w = std::fabs(size.x), h = std::fabs(size.y);
		for (ColliderComponent* c : Candidates(params))
		{
			float cx, cy, cw, ch;
			c->GetWorldBox(cx, cy, cw, ch);
			if (std::fabs(cx - center.x) * 2.f <= w + cw && std::fabs(cy - center.y) * 2.f <= h + ch)
			{
				o.colliders.push_back(c);
				if (std::find(o.actors.begin(), o.actors.end(), c->GetOwner()) == o.actors.end())
					o.actors.push_back(c->GetOwner());
			}
		}

		if (params.voxels)
			o.voxels = voxels::CountRect(center, size, OverlapFilter(params));

		AddShape(params, DebugShape::Box, { center.x, center.y, w, h }, o.Any());
		return o;
	}

	OverlapResult OverlapCircle(const vec2& center, float radius, const QueryParams& params)
	{
		OverlapResult o;
		if (!HRL_IsValidScene(Engine::GetScene()))
			return o;

		radius = std::fabs(radius);
		for (ColliderComponent* c : Candidates(params))
		{
			float cx, cy, cw, ch;
			c->GetWorldBox(cx, cy, cw, ch);
			if (CircleTouchesBox(center.x, center.y, radius, cx, cy, cw, ch))
			{
				o.colliders.push_back(c);
				if (std::find(o.actors.begin(), o.actors.end(), c->GetOwner()) == o.actors.end())
					o.actors.push_back(c->GetOwner());
			}
		}

		if (params.voxels)
			o.voxels = voxels::CountCircle(center, radius, OverlapFilter(params));

		AddShape(params, DebugShape::Circle, { center.x, center.y, radius }, o.Any());
		return o;
	}

	void SetDebugDrawAll(bool enabled)
	{
		g_debug_all = enabled;
	}

	bool IsDebugDrawAll()
	{
		return g_debug_all;
	}

	void TickDebug(float dt)
	{
		if (g_shapes.empty())
			return;
		if (!HRL_IsValidScene(Engine::GetScene()))
		{
			g_shapes.clear();
			return;
		}
		for (DebugShape& s : g_shapes)
		{
			s.time_left -= dt;
			if (s.time_left > 0.f)
				Draw(s);
		}
		g_shapes.erase(std::remove_if(g_shapes.begin(), g_shapes.end(),
		                              [](const DebugShape& s) { return s.time_left <= 0.f; }),
		               g_shapes.end());
	}
}
