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

#include <imgui/imgui.h>

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

		ImU32 Col(const std::array<float, 3>& c)
		{
			return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], 1.f));
		}

		void DrawPreview(const Result& r, float max_size)
		{
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			if (r.cols <= 0 || r.rows <= 0)
			{
				ImGui::Dummy(ImVec2(max_size, max_size));
				return;
			}
			const float cell = std::max(0.25f, std::min(max_size / r.cols, max_size / r.rows));
			const ImVec2 size(cell * r.cols, cell * r.rows);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			// checkerboard : empty cells
			const float check = std::max(4.f, cell);
			for (float y = 0; y < size.y; y += check)
				for (float x = 0; x < size.x; x += check)
				{
					const bool odd = (static_cast<int>(x / check) + static_cast<int>(y / check)) & 1;
					dl->AddRectFilled(ImVec2(origin.x + x, origin.y + y),
					                  ImVec2(origin.x + std::min(size.x, x + check), origin.y + std::min(size.y, y + check)),
					                  odd ? IM_COL32(200, 200, 200, 255) : IM_COL32(235, 235, 235, 255));
				}
			for (int row = 0; row < r.rows; ++row)
				for (int col = 0; col < r.cols; ++col)
				{
					const int i = r.cells[static_cast<size_t>(row) * r.cols + col];
					if (i < 0)
						continue;
					const ImVec2 a(origin.x + col * cell, origin.y + row * cell);
					dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), Col(r.palette[static_cast<size_t>(i)].rgb));
				}
			if (cell >= 6.f)   // the voxel grid, when it is readable
			{
				for (int col = 0; col <= r.cols; ++col)
					dl->AddLine(ImVec2(origin.x + col * cell, origin.y), ImVec2(origin.x + col * cell, origin.y + size.y), IM_COL32(0, 0, 0, 30));
				for (int row = 0; row <= r.rows; ++row)
					dl->AddLine(ImVec2(origin.x, origin.y + row * cell), ImVec2(origin.x + size.x, origin.y + row * cell), IM_COL32(0, 0, 0, 30));
			}
			dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), ImGui::GetColorU32(ImGuiCol_Border));
			ImGui::Dummy(ImVec2(std::max(size.x, 1.f), std::max(size.y, 1.f)));
		}

		void DrawSourceImage(float max_size)
		{
			const HRL_id tex = lynx::RessourceTex(g.source.texture.c_str());
			const unsigned int gl = tex != HRL_INVALID_ID ? HRL_GL_GetTextureGL_ID(tex) : 0u;
			const float pw = std::fabs(g.source.u1 - g.source.u0) * g.image.width;
			const float ph = std::fabs(g.source.v1 - g.source.v0) * g.image.height;
			const float scale = std::min(max_size / std::max(1.f, pw), max_size / std::max(1.f, ph));
			const ImVec2 size(std::max(1.f, pw * scale), std::max(1.f, ph * scale));
			if (gl == 0)
			{
				ImGui::Dummy(size);
				return;
			}
			// HRL textures are bottom-up : v flipped. The sprite flips too.
			float u0 = g.source.u0, u1 = g.source.u1, v0 = 1.f - g.source.v0, v1 = 1.f - g.source.v1;
			if (g.source.flip_x)
				std::swap(u0, u1);
			if (g.source.flip_y)
				std::swap(v0, v1);
			ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(gl)), size, ImVec2(u0, v0), ImVec2(u1, v1));
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

	void MenuItem(lynx::Actor* actor)
	{
		std::string reason;
		const bool ok = CanConvert(actor, &reason);
		if (ImGui::MenuItem("Convert to voxels...", nullptr, false, ok))
			Open(actor);
		if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", reason.c_str());
	}

	void Draw(const Host& host)
	{
		const char* title = "Convert to voxels";
		if (g.request_open)
		{
			ImGui::OpenPopup(title);
			g.request_open = false;
		}

		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 44.f, 0.f), ImGuiCond_Appearing);
		if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		// The actor may have been deleted meanwhile (undo...).
		if (!Resolve(host, g.actor_id, g.actor_ptr))
			g.load_error = "The actor does not exist any more";

		if (!g.load_error.empty())
		{
			ImGui::TextWrapped("%s", g.load_error.c_str());
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		Options& o = g.options;
		bool changed = false;
		const float label_w = ImGui::GetFontSize() * 9.f;
		auto label = [&](const char* text)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(text);
			ImGui::SameLine(label_w);
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
		};

		ImGui::Text("%s   (%s, %d x %d px)", g.source.name.c_str(), g.source.texture.c_str(), g.image.width, g.image.height);
		ImGui::Separator();

		// Two columns : the options, the previews.
		const float preview = ImGui::GetFontSize() * 13.f;
		const bool table = ImGui::BeginTable("##layout", 2, ImGuiTableFlags_SizingFixedFit);
		if (table)
		{
			ImGui::TableSetupColumn("options", ImGuiTableColumnFlags_WidthFixed, label_w + ImGui::GetFontSize() * 14.5f);
			ImGui::TableSetupColumn("previews", ImGuiTableColumnFlags_WidthFixed, preview + ImGui::GetFontSize());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
		}
		{
			// Size
			const char* sizes[] = { "Same size as the sprite", "1 voxel = 1 pixel", "Custom width" };
			int size_mode = static_cast<int>(o.size_mode);
			label("Size");
			if (ImGui::Combo("##size", &size_mode, sizes, 3))
			{
				o.size_mode = static_cast<SizeMode>(size_mode);
				changed = true;
			}
			if (o.size_mode == SizeMode::CustomWidth)
			{
				label("Width (voxels)");
				changed |= ImGui::SliderInt("##width", &o.custom_width, 1, 256);
			}

			const char* samplings[] = { "Nearest pixel", "Dominant color" };
			int sampling = static_cast<int>(o.sampling);
			label("Sampling");
			if (ImGui::Combo("##sampling", &sampling, samplings, 2))
			{
				o.sampling = static_cast<Sampling>(sampling);
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Nearest : the pixel at the center of each voxel (pixel art).\n"
				                  "Dominant : the most frequent color of the pixels of each voxel\n"
				                  "(better when the image is reduced a lot).");

			label("Alpha threshold");
			changed |= ImGui::SliderFloat("##alpha", &o.alpha_threshold, 0.01f, 1.f, "%.2f");

			ImGui::SeparatorText("Colors");
			const char* palettes[] = { "Palette + new types", "Palette only", "New types only" };
			int palette = static_cast<int>(o.palette);
			label("Voxel types");
			if (ImGui::Combo("##palette", &palette, palettes, 3))
			{
				o.palette = static_cast<PaletteMode>(palette);
				changed = true;
			}
			if (o.palette == PaletteMode::ExistingAndNew)
			{
				label("Tolerance");
				changed |= ImGui::SliderFloat("##tolerance", &o.tolerance, 1.f, 50.f, "%.0f");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("A color takes an existing type when it is this close (CIELAB distance).\n"
					                  "Lower : more new types, closer to the image.");
			}
			if (o.palette != PaletteMode::ExistingOnly)
			{
				label("Max new types");
				changed |= ImGui::SliderInt("##maxnew", &o.max_new_types, 1, 64);
				ImGui::Checkbox("New types are solid", &o.new_types_solid);
			}

			ImGui::SeparatorText("Placement");
			ImGui::Checkbox("Only fill empty voxels", &o.only_empty_cells);
			const char* afters[] = { "Keep the sprite", "Hide the sprite", "Delete the sprite" };
			int after = static_cast<int>(o.after);
			label("Then");
			if (ImGui::Combo("##after", &after, afters, 3))
				o.after = static_cast<After>(after);
		}

		if (changed)
			g.dirty = true;
		if (g.dirty)
		{
			g.result = Convert(g.image, g.source, ExistingPalette(), o);
			g.dirty = false;
		}
		const Result& r = g.result;

		// Previews : the sprite, the voxels
		if (table)
			ImGui::TableSetColumnIndex(1);
		{
			ImGui::TextDisabled("Sprite");
			DrawSourceImage(preview * 0.7f);
			ImGui::TextDisabled("Voxels  (%d x %d)", r.cols, r.rows);
			DrawPreview(r, preview);
		}
		if (table)
			ImGui::EndTable();

		ImGui::Separator();
		if (!r.error.empty())
		{
			ImGui::TextColored(ImVec4(0.80f, 0.38f, 0.f, 1.f), "%s", r.error.c_str());
		}
		else
		{
			ImGui::Text("%d voxels at (%d, %d)   %d existing type%s, %d new   color error %.1f",
			            r.voxel_count, r.origin_x, r.origin_y, r.existing_used, r.existing_used == 1 ? "" : "s",
			            r.new_types, r.mean_error);
			// The types used (new ones marked)
			const float sw = ImGui::GetFrameHeight();
			int shown = 0;
			for (const PaletteType& p : r.palette)
			{
				if (shown++ > 0)
				{
					if (ImGui::GetItemRectMax().x + sw * 1.2f < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
						ImGui::SameLine(0.f, 3.f);
				}
				ImGui::ColorButton("##t", ImVec4(p.rgb[0], p.rgb[1], p.rgb[2], 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(sw, sw));
				if (p.type == 0)
					ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(ImGui::GetItemRectMax().x - 4.f, ImGui::GetItemRectMin().y + 4.f), 3.f,
					                                             ImGui::GetColorU32(ImGuiCol_Text));
				if (ImGui::IsItemHovered())
				{
					const voxels::VoxelType* t = p.type > 0 ? voxels::GetType(static_cast<uint8_t>(p.type)) : nullptr;
					ImGui::SetTooltip("%s", t ? t->name.c_str() : "new type (dot)");
				}
			}
		}

		ImGui::Spacing();
		ImGui::BeginDisabled(!r.error.empty() || r.voxel_count == 0);
		if (ImGui::Button("Convert"))
		{
			Apply(host);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::SameLine();
		ImGui::TextDisabled("Ctrl+Z undoes it");
		ImGui::EndPopup();
	}
}
