#include "SpriteVoxelizer.h"

#include "../core/Filesystem.h"
#include "../core/JobSystem.h"
#include "../core/RessourceManager.h"
#include "../core/Voxels.h"
#include "../gameplay/Actor.h"
#include "../gameplay/EngineActors.h"
#include "../gameplay/SpriteComponents.h"

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include "raven/RavenUi.h"
#include <Raven.h>
#include <memory>

#include <stb/stb_image.h>   // the implementation is in EditorMain.cpp

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace lynx::editor::sprite_voxels
{
	// =========================================================================
	// Colors
	// =========================================================================

	namespace
	{
		using Rgb = std::array<float, 3>;
		using Lab = std::array<float, 3>;

		float Linear(float c)
		{
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		Lab ToLab(const Rgb& rgb)
		{
			const float r = Linear(rgb[0]), g = Linear(rgb[1]), b = Linear(rgb[2]);
			// sRGB D65 -> XYZ, normalized by the white point
			float x = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f;
			float y = (0.2126f * r + 0.7152f * g + 0.0722f * b);
			float z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
			auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.f / 116.f; };
			x = f(x);
			y = f(y);
			z = f(z);
			return { 116.f * y - 16.f, 500.f * (x - y), 200.f * (y - z) };
		}

		float LabDistance(const Lab& a, const Lab& b)
		{
			const float dl = a[0] - b[0], da = a[1] - b[1], db = a[2] - b[2];
			return std::sqrt(dl * dl + da * da + db * db);
		}

		uint32_t Key(uint8_t r, uint8_t g, uint8_t b)
		{
			return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
		}

		Rgb FromKey(uint32_t k)
		{
			return { ((k >> 16) & 0xFF) / 255.f, ((k >> 8) & 0xFF) / 255.f, (k & 0xFF) / 255.f };
		}

		struct WeightedColor
		{
			Rgb rgb;
			Lab lab;
			float weight;
		};

		// Median cut (in CIELAB) : at most `count` groups, each one -> its weighted mean color.
		std::vector<Rgb> MedianCut(std::vector<WeightedColor> colors, int count)
		{
			std::vector<Rgb> out;
			if (colors.empty() || count <= 0)
				return out;

			struct Box { size_t begin, end; };
			std::vector<Box> boxes{ { 0, colors.size() } };

			auto extent = [&](const Box& b, int& axis) -> float
			{
				float best = -1.f;
				for (int a = 0; a < 3; ++a)
				{
					float lo = 1e9f, hi = -1e9f;
					for (size_t i = b.begin; i < b.end; ++i)
					{
						lo = std::min(lo, colors[i].lab[a]);
						hi = std::max(hi, colors[i].lab[a]);
					}
					if (hi - lo > best)
					{
						best = hi - lo;
						axis = a;
					}
				}
				return best;
			};

			while (static_cast<int>(boxes.size()) < count)
			{
				// The widest box (with more than one color) is cut at its weighted median.
				int pick = -1, axis = 0;
				float widest = 0.f;
				for (size_t i = 0; i < boxes.size(); ++i)
				{
					if (boxes[i].end - boxes[i].begin < 2)
						continue;
					int a = 0;
					const float e = extent(boxes[i], a);
					if (e > widest)
					{
						widest = e;
						pick = static_cast<int>(i);
						axis = a;
					}
				}
				if (pick < 0 || widest <= 0.f)
					break;

				Box b = boxes[static_cast<size_t>(pick)];
				std::sort(colors.begin() + static_cast<long>(b.begin), colors.begin() + static_cast<long>(b.end),
				          [axis](const WeightedColor& x, const WeightedColor& y) { return x.lab[axis] < y.lab[axis]; });
				float total = 0.f;
				for (size_t i = b.begin; i < b.end; ++i)
					total += colors[i].weight;
				float acc = 0.f;
				size_t mid = b.begin + 1;
				for (size_t i = b.begin; i < b.end - 1; ++i)
				{
					acc += colors[i].weight;
					mid = i + 1;
					if (acc >= total * 0.5f)
						break;
				}
				boxes[static_cast<size_t>(pick)] = { b.begin, mid };
				boxes.push_back({ mid, b.end });
			}

			for (const Box& b : boxes)
			{
				Rgb sum{ 0.f, 0.f, 0.f };
				float w = 0.f;
				for (size_t i = b.begin; i < b.end; ++i)
				{
					for (int c = 0; c < 3; ++c)
						sum[c] += colors[i].rgb[c] * colors[i].weight;
					w += colors[i].weight;
				}
				if (w > 0.f)
					out.push_back({ sum[0] / w, sum[1] / w, sum[2] / w });
			}
			return out;
		}
	}

	float ColorDistance(const std::array<float, 3>& a, const std::array<float, 3>& b)
	{
		return LabDistance(ToLab(a), ToLab(b));
	}


	// =========================================================================
	// Conversion
	// =========================================================================

	void GridSize(const Image& image, const Source& source, const Options& options, int& cols, int& rows)
	{
		const float w = std::max(0.01f, source.width);
		const float h = std::max(0.01f, source.height);
		switch (options.size_mode)
		{
		case SizeMode::PixelPerVoxel:
			cols = static_cast<int>(std::lround(std::fabs(source.u1 - source.u0) * image.width));
			rows = static_cast<int>(std::lround(std::fabs(source.v1 - source.v0) * image.height));
			break;
		case SizeMode::CustomWidth:
			cols = std::max(1, options.custom_width);
			rows = static_cast<int>(std::lround(cols * h / w));
			break;
		default:
			cols = static_cast<int>(std::lround(w));
			rows = static_cast<int>(std::lround(h));
			break;
		}
		cols = std::clamp(cols, 1, 1024);
		rows = std::clamp(rows, 1, 1024);
	}

	Result Convert(const Image& image, const Source& source, const std::vector<PaletteType>& existing, const Options& options)
	{
		Result r;
		if (image.width <= 0 || image.height <= 0 ||
		    image.rgba.size() < static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4)
		{
			r.error = "No image";
			return r;
		}

		GridSize(image, source, options, r.cols, r.rows);
		r.origin_x = static_cast<int>(std::lround(source.center_x - r.cols * 0.5f));
		r.origin_y = static_cast<int>(std::lround(source.center_y - r.rows * 0.5f));
		r.cells.assign(static_cast<size_t>(r.cols) * static_cast<size_t>(r.rows), -1);

		const int alpha_min = static_cast<int>(std::ceil(std::clamp(options.alpha_threshold, 0.f, 1.f) * 255.f));
		auto pixel = [&](int x, int y) -> const uint8_t*
		{
			x = std::clamp(x, 0, image.width - 1);
			y = std::clamp(y, 0, image.height - 1);
			return &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
		};

		// Texture coordinates of a point of the grid (0..1 across the sprite).
		auto to_uv = [&](float fx, float fy, float& u, float& v)
		{
			if (source.flip_x)
				fx = 1.f - fx;
			if (source.flip_y)
				fy = 1.f - fy;
			u = source.u0 + fx * (source.u1 - source.u0);
			v = source.v0 + fy * (source.v1 - source.v0);
		};

		// 1. One color (or empty) per cell. The rows are independent (they
		//    only read the image) : worker threads, a few rows each.
		std::vector<int64_t> cell_color(r.cells.size(), -1);
		lynx::jobs::ParallelFor(0, r.rows, 4, [&](int row)
		{
			for (int col = 0; col < r.cols; ++col)
			{
				int64_t color = -1;
				if (options.sampling == Sampling::Nearest)
				{
					float u, v;
					to_uv((col + 0.5f) / r.cols, (row + 0.5f) / r.rows, u, v);
					const uint8_t* p = pixel(static_cast<int>(std::floor(u * image.width)),
					                         static_cast<int>(std::floor(v * image.height)));
					if (p[3] >= alpha_min)
						color = Key(p[0], p[1], p[2]);
				}
				else
				{
					// Dominant : the most frequent color (bins of 4 bits) of the pixels of the cell.
					float ua, va, ub, vb;
					to_uv(static_cast<float>(col) / r.cols, static_cast<float>(row) / r.rows, ua, va);
					to_uv(static_cast<float>(col + 1) / r.cols, static_cast<float>(row + 1) / r.rows, ub, vb);
					int x0 = static_cast<int>(std::floor(std::min(ua, ub) * image.width));
					int x1 = static_cast<int>(std::ceil(std::max(ua, ub) * image.width));
					int y0 = static_cast<int>(std::floor(std::min(va, vb) * image.height));
					int y1 = static_cast<int>(std::ceil(std::max(va, vb) * image.height));
					x1 = std::max(x1, x0 + 1);
					y1 = std::max(y1, y0 + 1);
					std::unordered_map<uint32_t, std::array<int, 4>> bins;   // count, r, g, b sums
					int total = 0, opaque = 0;
					for (int y = y0; y < y1; ++y)
						for (int x = x0; x < x1; ++x)
						{
							const uint8_t* p = pixel(x, y);
							++total;
							if (p[3] < alpha_min)
								continue;
							++opaque;
							auto& bin = bins[(uint32_t(p[0] >> 4) << 8) | (uint32_t(p[1] >> 4) << 4) | uint32_t(p[2] >> 4)];
							++bin[0];
							bin[1] += p[0];
							bin[2] += p[1];
							bin[3] += p[2];
						}
					if (total > 0 && opaque * 2 >= total)
					{
						const std::array<int, 4>* best = nullptr;
						for (const auto& [k, b] : bins)
							if (!best || b[0] > (*best)[0])
								best = &b;
						if (best && (*best)[0] > 0)
							color = Key(static_cast<uint8_t>((*best)[1] / (*best)[0]), static_cast<uint8_t>((*best)[2] / (*best)[0]),
							            static_cast<uint8_t>((*best)[3] / (*best)[0]));
					}
				}
				cell_color[static_cast<size_t>(row) * r.cols + col] = color;
			}
		});

		// 2. Distinct colors (weight = number of cells)
		std::map<uint32_t, int> weights;
		for (int64_t c : cell_color)
			if (c >= 0)
				++weights[static_cast<uint32_t>(c)];
		if (weights.empty())
		{
			r.error = "The sprite is transparent (nothing above the alpha threshold)";
			return r;
		}

		std::vector<Lab> existing_lab;
		for (const PaletteType& t : existing)
			existing_lab.push_back(ToLab(t.rgb));

		auto nearest = [](const Lab& lab, const std::vector<Lab>& labs, float& distance) -> int
		{
			int best = -1;
			distance = 1e9f;
			for (size_t i = 0; i < labs.size(); ++i)
			{
				const float d = LabDistance(lab, labs[i]);
				if (d < distance)
				{
					distance = d;
					best = static_cast<int>(i);
				}
			}
			return best;
		};

		// 3. Palette : the existing types (except "new only"), plus new types for the
		//    colors far from all of them.
		std::vector<PaletteType> palette;
		std::vector<Lab> palette_lab;
		if (options.palette != PaletteMode::NewOnly)
		{
			palette = existing;
			palette_lab = existing_lab;
		}

		if (options.palette != PaletteMode::ExistingOnly)
		{
			std::vector<WeightedColor> far;
			for (const auto& [k, w] : weights)
			{
				const Rgb rgb = FromKey(k);
				const Lab lab = ToLab(rgb);
				float d = 1e9f;
				if (!palette_lab.empty())
					nearest(lab, palette_lab, d);
				if (palette_lab.empty() || d > options.tolerance)
					far.push_back({ rgb, lab, static_cast<float>(w) });
			}
			const int room = std::max(0, 255 - static_cast<int>(existing.size()));
			const int max_new = std::min(std::max(0, options.max_new_types), room);
			for (const Rgb& c : MedianCut(far, max_new))
			{
				palette.push_back({ 0, c });
				palette_lab.push_back(ToLab(c));
			}
		}

		if (palette.empty())
		{
			r.error = "No voxel type to use (the palette is empty and no new type is allowed)";
			return r;
		}

		// 4. Every color -> the closest type of the palette
		std::unordered_map<uint32_t, int> assign;
		float error_sum = 0.f;
		int error_weight = 0;
		for (const auto& [k, w] : weights)
		{
			float d = 0.f;
			assign[k] = nearest(ToLab(FromKey(k)), palette_lab, d);
			error_sum += d * w;
			error_weight += w;
		}

		// Only the palette entries used are kept.
		std::vector<int> remap(palette.size(), -1);
		for (size_t i = 0; i < r.cells.size(); ++i)
		{
			if (cell_color[i] < 0)
				continue;
			const int p = assign[static_cast<uint32_t>(cell_color[i])];
			if (remap[static_cast<size_t>(p)] < 0)
			{
				remap[static_cast<size_t>(p)] = static_cast<int>(r.palette.size());
				r.palette.push_back(palette[static_cast<size_t>(p)]);
			}
			r.cells[i] = remap[static_cast<size_t>(p)];
			++r.voxel_count;
		}
		for (const PaletteType& t : r.palette)
		{
			if (t.type > 0)
				++r.existing_used;
			else
				++r.new_types;
		}
		r.mean_error = error_weight > 0 ? error_sum / error_weight : 0.f;
		return r;
	}

	bool DecodeImage(const std::vector<uint8_t>& bytes, Image& out, std::string& error)
	{
		int w = 0, h = 0, n = 0;
		stbi_uc* pixels = bytes.empty() ? nullptr
		                                : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
		if (!pixels)
		{
			error = bytes.empty() ? "image not found" : std::string("could not decode the image (") + stbi_failure_reason() + ")";
			return false;
		}
		out.width = w;
		out.height = h;
		out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);
		return true;
	}


	// =========================================================================
	// Editor
	// =========================================================================

	namespace
	{
		struct State
		{
			bool request_open = false;
			std::string actor_id;
			lynx::Actor* actor_ptr = nullptr;
			Source source;
			Image image;
			std::string load_error;
			Options options;
			Result result;
			bool dirty = true;
			std::string message;
		};

		State g;

		// The actor of the dialog / of an undo : by id, or by pointer (checked) without id.
		lynx::Actor* Resolve(const Host& host, const std::string& id, lynx::Actor* ptr)
		{
			if (!id.empty())
				return host.find_actor ? host.find_actor(id) : nullptr;
			return host.is_alive && host.is_alive(ptr) ? ptr : nullptr;
		}

		// The sprite of an actor, where it is drawn (same computation as SpriteComponent::SyncTransform).
		bool SourceOf(lynx::Actor* actor, Source& out, std::string& reason)
		{
			if (!actor)
			{
				reason = "No actor";
				return false;
			}

			const SpriteComponent* sprite = nullptr;
			std::string texture;
			float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
			vec2 size{ 1.f, 1.f };
			bool flip_x = false, flip_y = false;

			if (auto* sa = dynamic_cast<SpriteActor*>(actor))
			{
				sprite = sa->GetSprite();
				texture = sa->texture;
				size = sa->size;
				flip_x = sa->flip_x;
				flip_y = sa->flip_y;
				if (sa->GetSprite())
				{
					const vec4 reg = sa->GetSprite()->region;
					u0 = reg.x; v0 = reg.y; u1 = reg.z; v1 = reg.w;
				}
			}
			else if (auto* st = actor->GetComponent<StaticSpriteComponent>())
			{
				sprite = st;
				texture = st->texture;
				u0 = st->region.x; v0 = st->region.y; u1 = st->region.z; v1 = st->region.w;
			}
			else if (auto* an = actor->GetComponent<AnimationSpriteComponent>())
			{
				sprite = an;
				if (an->texture.empty())
				{
					reason = "Animation driven by a state machine / Anim Graph : not supported (use a single animation)";
					return false;
				}
				texture = an->texture;
				const int frames = std::max(1, an->frame_count);
				const int frame = std::clamp(an->GetFrame(), 1, frames);   // the frame shown now
				u0 = static_cast<float>(frame - 1) / frames;
				u1 = static_cast<float>(frame) / frames;
			}
			else
			{
				reason = "This actor has no sprite";
				return false;
			}

			if (texture.empty())
			{
				reason = "The sprite has no texture";
				return false;
			}

			if (!dynamic_cast<SpriteActor*>(actor) && sprite)
			{
				size = sprite->size;
				flip_x = sprite->flip_x;
				flip_y = sprite->flip_y;
			}

			const transform& t = actor->transform;
			const float ax = std::fabs(t.scale.x), ay = std::fabs(t.scale.y);
			const float fx = t.scale.x < 0.f ? -1.f : 1.f;
			const float fy = t.scale.y < 0.f ? -1.f : 1.f;
			const vec3 offset = sprite ? sprite->offset : vec3(0.f);

			out.name = actor->object_id_.empty() ? std::string("Sprite") : actor->object_id_;
			out.texture = texture;
			out.u0 = u0; out.v0 = v0; out.u1 = u1; out.v1 = v1;
			out.center_x = t.location.x + offset.x * ax * fx;
			out.center_y = t.location.y + offset.y * ay * fy;
			out.width = std::fabs(t.scale.x * size.x);
			out.height = std::fabs(t.scale.y * size.y);
			// A negative size or scale mirrors the image (like the renderer).
			out.flip_x = flip_x != (t.scale.x * size.x < 0.f);
			out.flip_y = flip_y != (t.scale.y * size.y < 0.f);
			if (out.width < 0.01f || out.height < 0.01f)
			{
				reason = "The sprite has no size";
				return false;
			}
			return true;
		}

		std::vector<PaletteType> ExistingPalette()
		{
			std::vector<PaletteType> out;
			for (int t = 1; t <= voxels::GetTypeCount(); ++t)
				if (const voxels::VoxelType* v = voxels::GetType(static_cast<uint8_t>(t)))
					out.push_back({ t, { v->color[0], v->color[1], v->color[2] } });
			return out;
		}

		// The voxels of the result : a checkerboard for the empty cells.
		void PaintPreview(const Result& r, RPainter* p, float w, float h)
		{
			if (r.cols <= 0 || r.rows <= 0)
				return;
			const float cell = std::max(0.25f, std::min(w / r.cols, h / r.rows));
			const float sw = cell * r.cols, sh = cell * r.rows;
			const float ox = (w - sw) * 0.5f, oy = (h - sh) * 0.5f;
			const float check = std::max(4.f, cell);
			for (float y = 0; y < sh; y += check)
				for (float x = 0; x < sw; x += check)
				{
					const bool odd = (static_cast<int>(x / check) + static_cast<int>(y / check)) & 1;
					const float c = odd ? 200.f / 255.f : 235.f / 255.f;
					Rdraw_rect(p, ox + x, oy + y, std::min(check, sw - x), std::min(check, sh - y), 0.f, c, c, c, 1.f);
				}
			for (int row = 0; row < r.rows; ++row)
				for (int col = 0; col < r.cols; ++col)
				{
					const int i = r.cells[static_cast<size_t>(row) * r.cols + col];
					if (i < 0)
						continue;
					const auto& rgb = r.palette[static_cast<size_t>(i)].rgb;
					Rdraw_rect(p, ox + col * cell, oy + row * cell, cell, cell, 0.f, rgb[0], rgb[1], rgb[2], 1.f);
				}
			if (cell >= 6.f)   // the voxel grid, when it is readable
			{
				for (int col = 0; col <= r.cols; ++col)
					Rdraw_line(p, ox + col * cell, oy, ox + col * cell, oy + sh, 1.f, 0.f, 0.f, 0.f, 0.12f);
				for (int row = 0; row <= r.rows; ++row)
					Rdraw_line(p, ox, oy + row * cell, ox + sw, oy + row * cell, 1.f, 0.f, 0.f, 0.f, 0.12f);
			}
			Rdraw_rect_outline(p, ox, oy, sw, sh, 0.f, 1.f, 0.5f, 0.5f, 0.5f, 1.f);
		}

		void Apply(const Host& host)
		{
			const Result& r = g.result;
			lynx::Actor* actor = Resolve(host, g.actor_id, g.actor_ptr);

			// 1. New voxel types
			const int types_before = voxels::GetTypeCount();
			std::vector<int> ids(r.palette.size(), 0);
			int created = 0;
			for (size_t i = 0; i < r.palette.size(); ++i)
			{
				if (r.palette[i].type > 0)
				{
					ids[i] = r.palette[i].type;
					continue;
				}
				voxels::VoxelType t;
				t.name = g.source.name + " " + std::to_string(++created);
				t.color[0] = r.palette[i].rgb[0];
				t.color[1] = r.palette[i].rgb[1];
				t.color[2] = r.palette[i].rgb[2];
				t.color[3] = 1.f;
				t.flags = g.options.new_types_solid ? voxels::GetSolidMask() : 0u;
				ids[i] = voxels::AddType(t);
			}
			if (created > 0)
			{
				voxels::ApplyToScene(host.scene);
				if (host.save_voxel_types)
					host.save_voxel_types();
			}

			// 2. Voxels
			struct Change { int x, y; uint32_t before, after; };
			std::vector<Change> changes;
			HRL_BeginVoxelEdit(host.scene);
			for (int row = 0; row < r.rows; ++row)
				for (int col = 0; col < r.cols; ++col)
				{
					const int i = r.cells[static_cast<size_t>(row) * r.cols + col];
					if (i < 0 || ids[static_cast<size_t>(i)] <= 0)
						continue;
					const int x = r.VoxelX(col), y = r.VoxelY(row);
					const uint32_t before = HRL_GetVoxelType(host.scene, x, y);
					const uint32_t after = static_cast<uint32_t>(ids[static_cast<size_t>(i)]);
					if (before == after || (g.options.only_empty_cells && before != 0))
						continue;
					changes.push_back({ x, y, before, after });
					HRL_SetVoxelType(host.scene, x, y, after);
				}
			HRL_EndVoxelEdit(host.scene);

			// 3. The sprite
			std::function<void()> restore_actor;
			if (actor && g.options.after == After::Hide)
			{
				const std::string id = g.actor_id;
				if (auto* sa = dynamic_cast<SpriteActor*>(actor))
				{
					const bool was = sa->visible;
					sa->visible = false;
					restore_actor = [id, was, ptr = g.actor_ptr, host]()
					{
						if (auto* a = dynamic_cast<SpriteActor*>(Resolve(host, id, ptr)))
							a->visible = was;
					};
				}
				else
				{
					SpriteComponent* c = actor->GetComponent<StaticSpriteComponent>();
					if (!c)
						c = actor->GetComponent<AnimationSpriteComponent>();
					if (c)
					{
						const bool was = c->visible;
						c->visible = false;
						c->Refresh();
						restore_actor = [id, was, ptr = g.actor_ptr, host]()
						{
							lynx::Actor* a = Resolve(host, id, ptr);
							SpriteComponent* s = a ? static_cast<SpriteComponent*>(a->GetComponent<StaticSpriteComponent>()) : nullptr;
							if (!s && a)
								s = a->GetComponent<AnimationSpriteComponent>();
							if (s)
							{
								s->visible = was;
								s->Refresh();
							}
						};
					}
				}
			}
			else if (actor && g.options.after == After::Delete && host.delete_actor)
			{
				restore_actor = host.delete_actor(actor);
			}

			// 4. One undo for everything
			if (host.push_undo)
			{
				const uint32_t scene = host.scene;
				auto save = host.save_voxel_types;
				auto dirty = host.mark_dirty;
				host.push_undo([changes, restore_actor, created, types_before, scene, save, dirty]()
				{
					HRL_BeginVoxelEdit(scene);
					for (auto it = changes.rbegin(); it != changes.rend(); ++it)
						HRL_SetVoxelType(scene, it->x, it->y, it->before);
					HRL_EndVoxelEdit(scene);
					if (restore_actor)
						restore_actor();
					// The new types go too, if nothing was added after them.
					if (created > 0 && voxels::GetTypeCount() == types_before + created)
					{
						for (int i = 0; i < created; ++i)
							voxels::RemoveLastType();
						voxels::ApplyToScene(scene);
						if (save)
							save();
					}
					if (dirty)
						dirty();
				});
			}
			if (host.mark_dirty)
				host.mark_dirty();

			char text[160];
			std::snprintf(text, sizeof(text), "[VOXELS] %s : %d voxels, %d new types", g.source.name.c_str(),
			              static_cast<int>(changes.size()), created);
			std::printf("%s\n", text);
		}

		void Load(lynx::Actor* actor)
		{
			const Options keep = g.options;   // the options stay from one conversion to the next
			g = State{};
			g.options = keep;
			g.actor_id = actor ? actor->object_id_ : std::string();
			g.actor_ptr = actor;
			std::string reason;
			if (!SourceOf(actor, g.source, reason))
			{
				g.load_error = reason;
				return;
			}
			std::string error;
			if (!DecodeImage(fs::ReadBinary(g.source.texture), g.image, error))
				g.load_error = "assets/" + g.source.texture + " : " + error;
		}
	}


	bool CanConvert(lynx::Actor* actor, std::string* reason)
	{
		Source s;
		std::string why;
		const bool ok = SourceOf(actor, s, why);
		if (reason)
			*reason = why;
		return ok;
	}

	Options& GetOptions()
	{
		return g.options;
	}

	void Open(lynx::Actor* actor)
	{
		Load(actor);
		g.request_open = true;
	}

	void MenuItem(lynx::Actor*)
	{
		// The menus of the Raven editor call CanConvert / Open (raven/EditorUi.inl).
	}

	namespace
	{
		Host g_host;
		Rid g_box = R_INVALID_ID;
		Rid g_image_texture = R_INVALID_ID;

		struct Fields
		{
			Rid size, width_row, width, sampling, alpha, alpha_value, palette, tolerance_row, tolerance, tolerance_value;
			Rid new_types, max_new, max_new_value, solid, only_empty, after, sprite, voxels, voxels_label, summary, error;
			Rid swatches, convert;
		};
		Fields f;

		void Recompute()
		{
			const Options& o = g.options;
			ui::SetVisible(f.width_row, o.size_mode == SizeMode::CustomWidth);
			ui::SetVisible(f.tolerance_row, o.palette == PaletteMode::ExistingAndNew);
			ui::SetVisible(f.new_types, o.palette != PaletteMode::ExistingOnly);
			ui::SetText(f.alpha_value, std::to_string(static_cast<int>(std::lround(o.alpha_threshold * 100))) + " %");
			ui::SetText(f.tolerance_value, std::to_string(static_cast<int>(std::lround(o.tolerance))));
			ui::SetText(f.max_new_value, std::to_string(o.max_new_types));

			g.result = Convert(g.image, g.source, ExistingPalette(), o);
			const Result& r = g.result;
			char text[200];
			std::snprintf(text, sizeof(text), "Voxels  (%d x %d)", r.cols, r.rows);
			ui::SetText(f.voxels_label, text);
			Rwidget_invalidate(f.voxels);
			ui::SetText(f.error, r.error);
			ui::SetVisible(f.error, !r.error.empty());
			std::snprintf(text, sizeof(text), "%d voxels at (%d, %d)   %d existing type%s, %d new   color error %.1f",
			              r.voxel_count, r.origin_x, r.origin_y, r.existing_used, r.existing_used == 1 ? "" : "s",
			              r.new_types, r.mean_error);
			ui::SetText(f.summary, r.error.empty() ? text : "");
			ui::SetEnabled(f.convert, r.error.empty() && r.voxel_count > 0);

			// The types used (new ones marked).
			Rwidget_clear_children(f.swatches);
			for (const PaletteType& pt : r.palette)
			{
				Rid sw = Rwidget_create(R_WIDGET_CUSTOM);
				Rwidget_set_size(sw, 20.f, 20.f);
				const voxels::VoxelType* t = pt.type > 0 ? voxels::GetType(static_cast<uint8_t>(pt.type)) : nullptr;
				Rwidget_set_tooltip(sw, t ? t->name.c_str() : "new type (dot)");
				const auto rgb = pt.rgb;
				const bool is_new = pt.type == 0;
				ui::OnPaint(sw, [rgb, is_new](RPainter* p, float w, float h)
				{
					Rdraw_rect(p, 0.f, 0.f, w, h, 3.f, rgb[0], rgb[1], rgb[2], 1.f);
					Rdraw_rect_outline(p, 0.f, 0.f, w, h, 3.f, 1.f, 0.f, 0.f, 0.f, 0.4f);
					if (is_new)
						Rdraw_circle(p, w - 5.f, 5.f, 3.f, 1.f, 1.f, 1.f, 1.f);
				});
				Rwidget_add_child(f.swatches, sw);
			}
		}

		void OnDialogClosed(Rid, int, void*)
		{
			g_box = R_INVALID_ID;
			if (g_image_texture != R_INVALID_ID)
			{
				Rtexture_destroy(g_image_texture);
				g_image_texture = R_INVALID_ID;
			}
		}

	}

	void Draw(const Host& host)
	{
		g_host = host;
		if (!g.request_open)
			return;
		g.request_open = false;

		const Rid window = input::Window();
		if (window == R_INVALID_ID || g_box != R_INVALID_ID)
			return;

		if (!g.load_error.empty())
		{
			ui::Message(window, "Convert to voxels", g.load_error);
			return;
		}

		g_box = ui::Load("dialogs/sprite_voxels.xml");
		if (g_box == R_INVALID_ID)
			return;
		auto find = [](const char* n) { return ui::Find(g_box, n); };
		f.size = find("sv.size");
		f.width_row = find("sv.width.row");
		f.width = find("sv.width");
		f.sampling = find("sv.sampling");
		f.alpha = find("sv.alpha");
		f.alpha_value = find("sv.alpha.value");
		f.palette = find("sv.palette");
		f.tolerance_row = find("sv.tolerance.row");
		f.tolerance = find("sv.tolerance");
		f.tolerance_value = find("sv.tolerance.value");
		f.new_types = find("sv.new_types");
		f.max_new = find("sv.max_new");
		f.max_new_value = find("sv.max_new.value");
		f.solid = find("sv.solid");
		f.only_empty = find("sv.only_empty");
		f.after = find("sv.after");
		f.sprite = find("sv.sprite");
		f.voxels = find("sv.voxels");
		f.voxels_label = find("sv.voxels.label");
		f.summary = find("sv.summary");
		f.error = find("sv.error");
		f.swatches = find("sv.swatches");
		f.convert = find("sv.convert");

		char title[256];
		std::snprintf(title, sizeof(title), "%s   (%s, %d x %d px)", g.source.name.c_str(), g.source.texture.c_str(),
		              g.image.width, g.image.height);
		ui::SetText(find("sv.title"), title);

		// The sprite : its part of the image, as it is drawn (flips included).
		if (!g.image.rgba.empty())
		{
			g_image_texture = Rtexture_create_from_pixels(g.image.rgba.data(), g.image.width, g.image.height);
			Rtexture_set_filter(g_image_texture, R_FILTER_NEAREST);
			float u0 = g.source.u0, u1 = g.source.u1, v0 = g.source.v0, v1 = g.source.v1;
			if (g.source.flip_x)
				std::swap(u0, u1);
			if (g.source.flip_y)
				std::swap(v0, v1);
			Rimage_set_texture(f.sprite, g_image_texture);
			Rimage_set_region(f.sprite, u0, v0, u1, v1);
		}
		ui::OnPaint(f.voxels, [](RPainter* p, float w, float h) { PaintPreview(g.result, p, w, h); });

		Options& o = g.options;
		Rcombo_box_set_selected(f.size, static_cast<int>(o.size_mode));
		Rspin_box_set_value(f.width, o.custom_width);
		Rcombo_box_set_selected(f.sampling, static_cast<int>(o.sampling));
		Rslider_set_value(f.alpha, o.alpha_threshold);
		Rcombo_box_set_selected(f.palette, static_cast<int>(o.palette));
		Rslider_set_value(f.tolerance, o.tolerance);
		Rslider_set_value(f.max_new, static_cast<float>(o.max_new_types));
		Rcheckbox_set_checked(f.solid, o.new_types_solid);
		Rcheckbox_set_checked(f.only_empty, o.only_empty_cells);
		Rcombo_box_set_selected(f.after, static_cast<int>(o.after));

		ui::OnSelect(f.size, [](int i) { if (i >= 0) { g.options.size_mode = static_cast<SizeMode>(i); Recompute(); } });
		ui::OnValue(f.width, [](double v) { g.options.custom_width = std::clamp(static_cast<int>(v), 1, 256); Recompute(); });
		ui::OnSelect(f.sampling, [](int i) { if (i >= 0) { g.options.sampling = static_cast<Sampling>(i); Recompute(); } });
		ui::OnValue(f.alpha, [](double v) { g.options.alpha_threshold = static_cast<float>(v); Recompute(); });
		ui::OnSelect(f.palette, [](int i) { if (i >= 0) { g.options.palette = static_cast<PaletteMode>(i); Recompute(); } });
		ui::OnValue(f.tolerance, [](double v) { g.options.tolerance = static_cast<float>(v); Recompute(); });
		ui::OnValue(f.max_new, [](double v) { g.options.max_new_types = static_cast<int>(std::lround(v)); Recompute(); });
		ui::OnToggle(f.solid, [](bool on) { g.options.new_types_solid = on; });
		ui::OnToggle(f.only_empty, [](bool on) { g.options.only_empty_cells = on; });
		ui::OnSelect(f.after, [](int i) { if (i >= 0) g.options.after = static_cast<After>(i); });
		ui::OnClick(f.convert, []()
		{
			// The actor may have been deleted meanwhile (undo...).
			if (Resolve(g_host, g.actor_id, g.actor_ptr))
				Apply(g_host);
			Rdialog_close(g_box, 1);
		});
		ui::OnClick(find("sv.cancel"), []() { Rdialog_close(g_box, 0); });

		Recompute();
		Rdialog_open(window, g_box, OnDialogClosed, nullptr);
	}
}
