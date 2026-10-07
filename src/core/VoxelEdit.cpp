#include "VoxelEdit.h"

#include "Engine.h"
#include "Voxels.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace lynx::voxels
{
	namespace
	{
		// A huge shape does not edit millions of voxels by mistake.
		constexpr long long kMaxCells = 1000000;

		uint32_t Scene()
		{
			return Engine::GetScene();
		}

		bool ToVoxel(const vec2& world, float& vx, float& vy)
		{
			return HRL_WorldToVoxelCoordinates(Scene(), world.x, world.y, &vx, &vy) == HRL_TRUE;
		}

		// World units -> voxels (physical voxel size).
		float VoxelsPerUnit()
		{
			float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
			if (!ToVoxel(vec2(0.f, 0.f), x0, y0) || !ToVoxel(vec2(1.f, 0.f), x1, y1))
				return 1.f;
			const float d = std::fabs(x1 - x0);
			return d > 1e-6f ? d : 1.f;
		}

		enum class Shape { Cell, Circle, Rect, Line };

		struct ShapeDesc
		{
			Shape kind = Shape::Cell;
			// voxel space
			float ax = 0.f, ay = 0.f;     // cell / center / from
			float bx = 0.f, by = 0.f;     // rect half size / line to
			float radius = 0.f;           // circle radius / line half thickness
		};

		float SegmentDistanceSq(float px, float py, const ShapeDesc& s)
		{
			const float dx = s.bx - s.ax;
			const float dy = s.by - s.ay;
			const float len2 = dx * dx + dy * dy;
			float t = len2 > 0.f ? ((px - s.ax) * dx + (py - s.ay) * dy) / len2 : 0.f;
			t = std::clamp(t, 0.f, 1.f);
			const float cx = s.ax + dx * t - px;
			const float cy = s.ay + dy * t - py;
			return cx * cx + cy * cy;
		}

		// Calls fn(x, y) for each cell of the shape (voxel coordinates).
		template <typename Fn>
		void ForEachCell(const ShapeDesc& s, Fn&& fn)
		{
			if (s.kind == Shape::Cell)
			{
				fn(static_cast<int>(std::floor(s.ax)), static_cast<int>(std::floor(s.ay)));
				return;
			}

			float minx, miny, maxx, maxy;
			switch (s.kind)
			{
			case Shape::Circle:
				minx = s.ax - s.radius; maxx = s.ax + s.radius;
				miny = s.ay - s.radius; maxy = s.ay + s.radius;
				break;
			case Shape::Rect:
				minx = s.ax - s.bx; maxx = s.ax + s.bx;
				miny = s.ay - s.by; maxy = s.ay + s.by;
				break;
			default:   // Line
				minx = std::min(s.ax, s.bx) - s.radius; maxx = std::max(s.ax, s.bx) + s.radius;
				miny = std::min(s.ay, s.by) - s.radius; maxy = std::max(s.ay, s.by) + s.radius;
				break;
			}

			const int x0 = static_cast<int>(std::floor(minx));
			const int y0 = static_cast<int>(std::floor(miny));
			const int x1 = static_cast<int>(std::floor(maxx));
			const int y1 = static_cast<int>(std::floor(maxy));

			if (static_cast<long long>(x1 - x0 + 1) * static_cast<long long>(y1 - y0 + 1) > kMaxCells)
				return;

			// The cell(s) that contain the shape's points are always in.
			const int px = static_cast<int>(std::floor(s.ax));
			const int py = static_cast<int>(std::floor(s.ay));
			const int qx = static_cast<int>(std::floor(s.bx));
			const int qy = static_cast<int>(std::floor(s.by));

			const float r2 = s.radius * s.radius;

			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					const float cx = x + 0.5f;
					const float cy = y + 0.5f;
					bool in = false;

					switch (s.kind)
					{
					case Shape::Circle:
						in = (cx - s.ax) * (cx - s.ax) + (cy - s.ay) * (cy - s.ay) <= r2 || (x == px && y == py);
						break;
					case Shape::Rect:
						in = cx >= minx && cx <= maxx && cy >= miny && cy <= maxy;
						// A rect smaller than a cell : the cell of its center.
						in = in || (x == px && y == py);
						break;
					default:
						in = SegmentDistanceSq(cx, cy, s) <= r2 || (x == px && y == py) || (x == qx && y == qy);
						break;
					}

					if (in)
						fn(x, y);
				}
			}
		}

		void Record(VoxelEditResult& r, int x, int y, uint8_t before, bool keep)
		{
			const vec2 w = CellToWorld(x, y);

			if (r.count == 0)
			{
				r.bounds_min = w;
				r.bounds_max = w;
			}
			else
			{
				r.bounds_min.x = std::min(r.bounds_min.x, w.x); r.bounds_min.y = std::min(r.bounds_min.y, w.y);
				r.bounds_max.x = std::max(r.bounds_max.x, w.x); r.bounds_max.y = std::max(r.bounds_max.y, w.y);
			}

			++r.count;

			auto it = std::lower_bound(r.per_type.begin(), r.per_type.end(), before,
			                           [](const std::pair<uint8_t, int>& p, uint8_t t) { return p.first < t; });
			if (it != r.per_type.end() && it->first == before)
				++it->second;
			else
				r.per_type.insert(it, { before, 1 });

			if (keep)
			{
				VoxelCell c;
				c.x = x;
				c.y = y;
				c.type = before;
				c.world = w;
				r.cells.push_back(c);
			}
		}

		bool ValidWorld()
		{
			return HRL_IsValidScene(Scene()) == HRL_TRUE;
		}

		// ---- The three operations ------------------------------------------

		VoxelEditResult DoDestroy(const ShapeDesc& s, const VoxelFilter& filter)
		{
			VoxelEditResult r;
			if (!ValidWorld())
				return r;

			const uint32_t scene = Scene();
			std::vector<VoxelEvent> events;
			bool editing = false;

			ForEachCell(s, [&](int x, int y)
			{
				const uint8_t t = static_cast<uint8_t>(HRL_GetVoxelType(scene, x, y));
				if (t == 0 || !filter.Accepts(t, GetFlags(t), IsIndestructible(t)))
					return;

				if (!editing)
				{
					HRL_BeginVoxelEdit(scene);
					editing = true;
				}

				HRL_SetVoxelType(scene, x, y, 0u);
				Record(r, x, y, t, filter.keep_cells);
				if (filter.fire_events)
					QueueDestroyedEvent(events, t, x, y);
			});

			if (editing)
				HRL_EndVoxelEdit(scene);

			FireDestroyedEvents(events);
			return r;
		}

		VoxelEditResult DoFill(const ShapeDesc& s, uint8_t type, const VoxelFillOptions& options)
		{
			VoxelEditResult r;
			if (type == 0 || !ValidWorld() || static_cast<int>(type) > GetTypeCount())
				return r;

			const uint32_t scene = Scene();
			bool editing = false;

			ForEachCell(s, [&](int x, int y)
			{
				const uint8_t t = static_cast<uint8_t>(HRL_GetVoxelType(scene, x, y));
				if (t == type)
					return;
				if (t != 0 && (!options.replace || !options.filter.Accepts(t, GetFlags(t), IsIndestructible(t))))
					return;

				if (!editing)
				{
					HRL_BeginVoxelEdit(scene);
					editing = true;
				}

				HRL_SetVoxelType(scene, x, y, type);
				Record(r, x, y, t, options.filter.keep_cells);
			});

			if (editing)
				HRL_EndVoxelEdit(scene);

			return r;
		}

		VoxelEditResult DoCount(const ShapeDesc& s, const VoxelFilter& filter)
		{
			VoxelEditResult r;
			if (!ValidWorld())
				return r;

			const uint32_t scene = Scene();
			ForEachCell(s, [&](int x, int y)
			{
				const uint8_t t = static_cast<uint8_t>(HRL_GetVoxelType(scene, x, y));
				// Counting : the indestructible types are counted too.
				if (t != 0 && filter.Accepts(t, GetFlags(t), false))
					Record(r, x, y, t, filter.keep_cells);
			});
			return r;
		}

		// ---- World shapes -> voxel shapes ------------------------------------

		bool CellShape(const vec2& world, ShapeDesc& s)
		{
			s.kind = Shape::Cell;
			return ToVoxel(world, s.ax, s.ay);
		}

		bool CircleShape(const vec2& center, float radius, ShapeDesc& s)
		{
			s.kind = Shape::Circle;
			s.radius = std::max(0.f, radius) * VoxelsPerUnit();
			return ToVoxel(center, s.ax, s.ay);
		}

		bool RectShape(const vec2& center, const vec2& size, ShapeDesc& s)
		{
			s.kind = Shape::Rect;
			const float k = VoxelsPerUnit();
			s.bx = std::fabs(size.x) * 0.5f * k;
			s.by = std::fabs(size.y) * 0.5f * k;
			return ToVoxel(center, s.ax, s.ay);
		}

		bool LineShape(const vec2& from, const vec2& to, float thickness, ShapeDesc& s)
		{
			s.kind = Shape::Line;
			// At least half a voxel : a thin line is still continuous.
			s.radius = std::max(0.5f, std::fabs(thickness) * 0.5f * VoxelsPerUnit());
			return ToVoxel(from, s.ax, s.ay) && ToVoxel(to, s.bx, s.by);
		}

		ShapeDesc CellAt(int x, int y)
		{
			ShapeDesc s;
			s.kind = Shape::Cell;
			s.ax = x + 0.5f;
			s.ay = y + 0.5f;
			return s;
		}

		std::string Lower(std::string s)
		{
			for (char& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}
	}


	// ---- Types / coordinates --------------------------------------------------

	uint8_t FindType(const char* name)
	{
		if (!name || !*name)
			return 0;
		const std::string key = Lower(name);
		const int n = GetTypeCount();
		for (int i = 1; i <= n && i < 256; ++i)
		{
			const VoxelType* t = GetType(static_cast<uint8_t>(i));
			if (t && Lower(t->name) == key)
				return static_cast<uint8_t>(i);
		}
		return 0;
	}

	bool WorldToCell(const vec2& world, int& voxel_x, int& voxel_y)
	{
		float vx = 0.f, vy = 0.f;
		if (!ToVoxel(world, vx, vy))
			return false;
		voxel_x = static_cast<int>(std::floor(vx));
		voxel_y = static_cast<int>(std::floor(vy));
		return true;
	}

	vec2 CellToWorld(int voxel_x, int voxel_y)
	{
		float wx = 0.f, wy = 0.f;
		if (HRL_VoxelToWorldCoordinates(Scene(), voxel_x + 0.5f, voxel_y + 0.5f, &wx, &wy) != HRL_TRUE)
			return vec2(voxel_x + 0.5f, voxel_y + 0.5f);
		return vec2(wx, wy);
	}

	uint8_t GetTypeAtWorld(const vec2& world)
	{
		int x = 0, y = 0;
		return WorldToCell(world, x, y) ? GetTypeAt(x, y) : 0;
	}


	// ---- Destruction ------------------------------------------------------------

	VoxelEditResult DestroyCell(int voxel_x, int voxel_y, const VoxelFilter& filter)
	{
		return DoDestroy(CellAt(voxel_x, voxel_y), filter);
	}

	VoxelEditResult DestroyAt(const vec2& world, const VoxelFilter& filter)
	{
		ShapeDesc s;
		return CellShape(world, s) ? DoDestroy(s, filter) : VoxelEditResult{};
	}

	VoxelEditResult DestroyCircle(const vec2& center, float radius, const VoxelFilter& filter)
	{
		ShapeDesc s;
		return CircleShape(center, radius, s) ? DoDestroy(s, filter) : VoxelEditResult{};
	}

	VoxelEditResult DestroyRect(const vec2& center, const vec2& size, const VoxelFilter& filter)
	{
		ShapeDesc s;
		return RectShape(center, size, s) ? DoDestroy(s, filter) : VoxelEditResult{};
	}

	VoxelEditResult DestroyLine(const vec2& from, const vec2& to, float thickness, const VoxelFilter& filter)
	{
		ShapeDesc s;
		return LineShape(from, to, thickness, s) ? DoDestroy(s, filter) : VoxelEditResult{};
	}


	// ---- Construction -----------------------------------------------------------

	VoxelEditResult FillCell(int voxel_x, int voxel_y, uint8_t type, const VoxelFillOptions& options)
	{
		return DoFill(CellAt(voxel_x, voxel_y), type, options);
	}

	VoxelEditResult FillAt(const vec2& world, uint8_t type, const VoxelFillOptions& options)
	{
		ShapeDesc s;
		return CellShape(world, s) ? DoFill(s, type, options) : VoxelEditResult{};
	}

	VoxelEditResult FillCircle(const vec2& center, float radius, uint8_t type, const VoxelFillOptions& options)
	{
		ShapeDesc s;
		return CircleShape(center, radius, s) ? DoFill(s, type, options) : VoxelEditResult{};
	}

	VoxelEditResult FillRect(const vec2& center, const vec2& size, uint8_t type, const VoxelFillOptions& options)
	{
		ShapeDesc s;
		return RectShape(center, size, s) ? DoFill(s, type, options) : VoxelEditResult{};
	}

	VoxelEditResult FillLine(const vec2& from, const vec2& to, float thickness, uint8_t type, const VoxelFillOptions& options)
	{
		ShapeDesc s;
		return LineShape(from, to, thickness, s) ? DoFill(s, type, options) : VoxelEditResult{};
	}


	// ---- Reading ----------------------------------------------------------------

	VoxelEditResult CountCircle(const vec2& center, float radius, const VoxelFilter& filter)
	{
		ShapeDesc s;
		return CircleShape(center, radius, s) ? DoCount(s, filter) : VoxelEditResult{};
	}

	VoxelEditResult CountRect(const vec2& center, const vec2& size, const VoxelFilter& filter)
	{
		ShapeDesc s;
		return RectShape(center, size, s) ? DoCount(s, filter) : VoxelEditResult{};
	}
}
