#include "ParticleEditor.h"

#include "../core/Particles.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

namespace fs = std::filesystem;
namespace px = lynx::particles;

namespace lynx::editor::particle_editor
{
	namespace
	{
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;
		constexpr float kDegToRad = 3.14159265358979f / 180.f;

		enum class Module
		{
			System,   // nothing selected : the settings of the system
			Required,
			Spawn,
			Lifetime,
			Location,
			Velocity,
			Rotation,
			Forces,
			ColorOverLife,
			SizeOverLife,
			RotationOverLife,
			Collision,
			Count
		};

		const char* ModuleName(Module m)
		{
			switch (m)
			{
				case Module::Required:         return "Required";
				case Module::Spawn:            return "Spawn";
				case Module::Lifetime:         return "Lifetime";
				case Module::Location:         return "Initial Location";
				case Module::Velocity:         return "Initial Velocity";
				case Module::Rotation:         return "Initial Rotation";
				case Module::Forces:           return "Forces";
				case Module::ColorOverLife:    return "Color Over Life";
				case Module::SizeOverLife:     return "Size Over Life";
				case Module::RotationOverLife: return "Rotation Over Life";
				case Module::Collision:        return "Collision";
				default:                       return "System";
			}
		}

		// Colored bar of the module blocks (like Cascade : one color per kind).
		ImU32 ModuleColor(Module m)
		{
			switch (m)
			{
				case Module::Required:         return IM_COL32(230, 200, 80, 255);
				case Module::Spawn:            return IM_COL32(230, 120, 60, 255);
				case Module::Lifetime:         return IM_COL32(120, 200, 120, 255);
				case Module::Location:         return IM_COL32(90, 170, 230, 255);
				case Module::Velocity:         return IM_COL32(110, 140, 240, 255);
				case Module::Rotation:         return IM_COL32(170, 120, 230, 255);
				case Module::Forces:           return IM_COL32(200, 90, 200, 255);
				case Module::ColorOverLife:    return IM_COL32(240, 90, 110, 255);
				case Module::SizeOverLife:     return IM_COL32(90, 210, 200, 255);
				case Module::RotationOverLife: return IM_COL32(160, 200, 90, 255);
				case Module::Collision:        return IM_COL32(200, 160, 110, 255);
				default:                       return IM_COL32(150, 150, 150, 255);
			}
		}

		// Optional modules : a checkbox on their block, "Add module" / "Remove".
		bool* ModuleFlag(px::EmitterDesc& e, Module m)
		{
			switch (m)
			{
				case Module::Rotation:         return &e.rotation_module;
				case Module::Forces:           return &e.forces_module;
				case Module::ColorOverLife:    return &e.color_module;
				case Module::SizeOverLife:     return &e.size_module;
				case Module::RotationOverLife: return &e.rotation_curve_module;
				case Module::Collision:        return &e.collision;
				default:                       return nullptr;
			}
		}

		std::function<bool(std::string&)> g_asset_field;
		bool g_focused = false;

		// ---------------------------------------------------------------------
		// Preview scene (offscreen : shown with ImGui::Image)
		// ---------------------------------------------------------------------

		struct Preview
		{
			uint32_t scene = kInvalid;
			uint32_t camera = kInvalid;
			uint32_t viewport = kInvalid;
			px::Instance instance;
			int width = 0, height = 0;
			float yaw = -90.f, pitch = 0.f, distance = 14.f;
			float target[3] = { 0.f, 1.5f, 0.f };
			float background[3] = { 0.07f, 0.075f, 0.09f };
			bool grid = true;
			bool paused = false;
			float time_scale = 1.f;
			float applied_bg[3] = { -1.f, -1.f, -1.f };

			void Create()
			{
				if (scene != kInvalid && HRL_IsValidScene(scene))
					return;
				scene = HRL_CreateScene(HRL_FALSE);
				if (scene == kInvalid)
					return;
				camera = HRL_CreateCamera(scene, HRL_PERSPECTIVE);
				HRL_SetCameraPerspectiveFov(camera, 45.f);
				HRL_SetCameraNearPlane(camera, 0.05f);
				HRL_SetCameraFarPlane(camera, 2000.f);
				viewport = HRL_CreateViewport(scene, camera, 0.f, 0.f, 1.f, 1.f);
				HRL_SetSkySphereEnabled(scene, HRL_TRUE);
				applied_bg[0] = -1.f;
			}

			void Destroy()
			{
				instance.Destroy();
				if (scene != kInvalid && HRL_IsValidScene(scene))
					HRL_DeleteScene(scene);   // its camera / viewport too
				scene = camera = viewport = kInvalid;
				width = height = 0;
			}

			void ApplyCamera()
			{
				if (camera == kInvalid)
					return;
				const float p = pitch * kDegToRad, y = yaw * kDegToRad;
				const float f[3] = { std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p) };
				HRL_SetCameraLocation(camera, target[0] - f[0] * distance, target[1] - f[1] * distance,
				                      target[2] - f[2] * distance);
				HRL_SetCameraRotation(camera, pitch, yaw, 0.f);

				if (std::fabs(background[0] - applied_bg[0]) + std::fabs(background[1] - applied_bg[1]) +
				        std::fabs(background[2] - applied_bg[2]) > 1e-4f)
				{
					const float* b = background;
					HRL_SetSkySphereColors(scene, b[0], b[1], b[2], b[0], b[1], b[2], b[0], b[1], b[2]);
					std::copy(b, b + 3, applied_bg);
				}
			}

			void DrawGrid()
			{
				if (!grid || scene == kInvalid)
					return;
				// The level plane (XY, Z = 0) : the voxel world of the game.
				constexpr int kHalf = 10;
				for (int i = -kHalf; i <= kHalf; ++i)
				{
					const float c = i == 0 ? 0.f : 0.22f;
					const float fi = static_cast<float>(i);
					HRL_DrawDebugSegment(scene, fi, -kHalf, 0.f, fi, kHalf, 0.f, i == 0 ? 0.3f : c, i == 0 ? 0.85f : c, c);
					HRL_DrawDebugSegment(scene, -kHalf, fi, 0.f, kHalf, fi, 0.f, i == 0 ? 0.85f : c, i == 0 ? 0.3f : c, c);
				}
			}
		};

		// ---------------------------------------------------------------------
		// One open file
		// ---------------------------------------------------------------------

		struct Editor
		{
			fs::path path;
			std::string title;
			px::SystemDesc desc;
			std::string saved_json;       // content on the disk
			std::string snapshot;         // last state pushed to undo
			std::vector<std::string> undo, redo;
			int emitter = 0;              // selected emitter
			Module module = Module::Required;
			int selected_key = -1;        // curve / gradient key
			bool rebuild = true;
			double last_build = 0.0;
			bool open = true;
			bool confirm_close = false;   // closed with unsaved changes : Save / Discard / Cancel
			bool closed = false;          // really closed (removed from the list)
			bool focus = true;
			int rename = -1;              // emitter being renamed
			char rename_buffer[64] = {};
			Preview preview;
		};

		std::vector<std::unique_ptr<Editor>> g_editors;

		// assets/<...> path of a file (lynx::fs reads assets-relative paths).
		std::string AssetPath(const fs::path& path)
		{
			fs::path p = path.lexically_normal();
			std::vector<fs::path> parts(p.begin(), p.end());
			for (size_t i = parts.size(); i > 0; --i)
				if (parts[i - 1] == "assets")
				{
					fs::path rel;
					for (size_t j = i; j < parts.size(); ++j)
						rel /= parts[j];
					return rel.generic_string();
				}
			return p.filename().generic_string();
		}

		bool Load(Editor& e)
		{
			std::ifstream in(e.path, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			std::string error;
			if (!px::FromJson(text.empty() ? px::NewTemplate() : text, e.desc, error))
			{
				std::cout << "[PARTICLE EDITOR] " << e.path.generic_string() << " : " << error << "\n";
				return false;
			}
			e.saved_json = px::ToJson(e.desc);
			e.snapshot = e.saved_json;
			e.rebuild = true;
			return true;
		}

		bool Save(Editor& e)
		{
			const std::string json = px::ToJson(e.desc);
			std::ofstream out(e.path, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				std::cout << "[PARTICLE EDITOR] could not write " << e.path.generic_string() << "\n";
				return false;
			}
			out << json;
			out.close();
			e.saved_json = json;
			px::NotifyAssetChanged(AssetPath(e.path));   // ParticleActors reload it
			std::cout << "[PARTICLE EDITOR] saved " << e.path.generic_string() << "\n";
			return true;
		}

		bool IsDirty(const Editor& e)
		{
			return px::ToJson(e.desc) != e.saved_json;
		}

		void Restore(Editor& e, const std::string& json)
		{
			std::string error;
			px::FromJson(json, e.desc, error);
			e.snapshot = json;
			e.emitter = std::clamp(e.emitter, 0, std::max(0, static_cast<int>(e.desc.emitters.size()) - 1));
			e.selected_key = -1;
			e.rebuild = true;
		}

		// ---------------------------------------------------------------------
		// Small widgets
		// ---------------------------------------------------------------------

		bool Vec3Field(const char* label, lynx::vec3& v, float speed = 0.05f)
		{
			return ImGui::DragFloat3(label, &v.x, speed);
		}

		bool RangeField(const char* label, px::Range& r, float speed = 0.02f, float min = 0.f, float max = 0.f)
		{
			bool changed = ImGui::DragFloatRange2(label, &r.min, &r.max, speed, min, max, "min %.2f", "max %.2f");
			if (r.max < r.min)
				r.max = r.min;
			return changed;
		}

		template <class E>
		bool EnumCombo(const char* label, E& value, const std::vector<std::pair<E, const char*>>& names)
		{
			const char* current = names.front().second;
			for (const auto& [v, n] : names)
				if (v == value)
					current = n;
			bool changed = false;
			if (ImGui::BeginCombo(label, current))
			{
				for (const auto& [v, n] : names)
					if (ImGui::Selectable(n, v == value))
					{
						value = v;
						changed = true;
					}
				ImGui::EndCombo();
			}
			return changed;
		}

		// Value of a float curve at t (keys in any order, linear between them).
		float Evaluate(const std::vector<px::FloatKey>& keys, float t)
		{
			if (keys.empty())
				return 0.f;
			const px::FloatKey* before = nullptr;
			const px::FloatKey* after = nullptr;
			for (const px::FloatKey& k : keys)
			{
				if (k.time <= t && (!before || k.time > before->time)) before = &k;
				if (k.time >= t && (!after || k.time < after->time)) after = &k;
			}
			if (!before) return after->value;
			if (!after) return before->value;
			if (after->time - before->time < 1e-6f) return before->value;
			const float a = (t - before->time) / (after->time - before->time);
			return before->value + (after->value - before->value) * a;
		}

		lynx::vec4 Evaluate(const std::vector<px::ColorKey>& keys, float t)
		{
			if (keys.empty())
				return lynx::vec4(1.f, 1.f, 1.f, 1.f);
			const px::ColorKey* before = nullptr;
			const px::ColorKey* after = nullptr;
			for (const px::ColorKey& k : keys)
			{
				if (k.time <= t && (!before || k.time > before->time)) before = &k;
				if (k.time >= t && (!after || k.time < after->time)) after = &k;
			}
			if (!before) return after->color;
			if (!after) return before->color;
			if (after->time - before->time < 1e-6f) return before->color;
			const float a = (t - before->time) / (after->time - before->time);
			const lynx::vec4& c0 = before->color;
			const lynx::vec4& c1 = after->color;
			return lynx::vec4(c0.x + (c1.x - c0.x) * a, c0.y + (c1.y - c0.y) * a, c0.z + (c1.z - c0.z) * a,
			                  c0.w + (c1.w - c0.w) * a);
		}

		ImU32 ToU32(const lynx::vec4& c)
		{
			return ImGui::ColorConvertFloat4ToU32(ImVec4(std::clamp(c.x, 0.f, 1.f), std::clamp(c.y, 0.f, 1.f),
			                                             std::clamp(c.z, 0.f, 1.f), std::clamp(c.w, 0.f, 1.f)));
		}

		void Checkerboard(ImDrawList* draw, ImVec2 a, ImVec2 b, float cell)
		{
			draw->AddRectFilled(a, b, IM_COL32(200, 200, 200, 255));
			int row = 0;
			for (float y = a.y; y < b.y; y += cell, ++row)
				for (float x = a.x + ((row & 1) ? cell : 0.f); x < b.x; x += cell * 2.f)
					draw->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, b.x), std::min(y + cell, b.y)),
					                    IM_COL32(150, 150, 150, 255));
		}

		// Gradient bar (RGBA over the life of the particles).
		void DrawGradientStrip(ImDrawList* draw, ImVec2 a, ImVec2 b, const std::vector<px::ColorKey>& keys)
		{
			Checkerboard(draw, a, b, std::max(3.f, (b.y - a.y) * 0.5f));
			constexpr int kSteps = 48;
			for (int i = 0; i < kSteps; ++i)
			{
				const float t0 = static_cast<float>(i) / kSteps, t1 = static_cast<float>(i + 1) / kSteps;
				const ImU32 c0 = ToU32(Evaluate(keys, t0)), c1 = ToU32(Evaluate(keys, t1));
				draw->AddRectFilledMultiColor(ImVec2(a.x + (b.x - a.x) * t0, a.y), ImVec2(a.x + (b.x - a.x) * t1, b.y),
				                              c0, c1, c1, c0);
			}
			draw->AddRect(a, b, IM_COL32(20, 20, 20, 255));
		}

		// ---------------------------------------------------------------------
		// Gradient editor (Color Over Life)
		// ---------------------------------------------------------------------

		bool GradientEditor(std::vector<px::ColorKey>& keys, int& selected)
		{
			bool changed = false;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const float width = std::max(120.f, ImGui::GetContentRegionAvail().x);
			const float bar_h = ImGui::GetFrameHeight() * 1.4f;
			const float marker = ImGui::GetFontSize() * 0.55f;
			const ImVec2 a = ImGui::GetCursorScreenPos();
			const ImVec2 b(a.x + width, a.y + bar_h);
			DrawGradientStrip(draw, a, b, keys);

			ImGui::InvisibleButton("##gradient_bar", ImVec2(width, bar_h + marker * 2.2f));
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const float mouse_t = std::clamp((mouse.x - a.x) / width, 0.f, 1.f);

			// Markers under the bar.
			int hovered_key = -1;
			for (int i = 0; i < static_cast<int>(keys.size()); ++i)
			{
				const float x = a.x + width * std::clamp(keys[i].time, 0.f, 1.f);
				const ImVec2 tip(x, b.y + 1.f);
				const ImVec2 l(x - marker, b.y + marker * 1.8f), r(x + marker, b.y + marker * 1.8f);
				if (hovered && std::fabs(mouse.x - x) <= marker && mouse.y >= b.y)
					hovered_key = i;
				lynx::vec4 c = keys[i].color;
				c.w = 1.f;
				draw->AddTriangleFilled(tip, r, l, ToU32(c));
				draw->AddTriangle(tip, r, l, i == selected ? IM_COL32(255, 200, 60, 255)
				                                           : (i == hovered_key ? IM_COL32(255, 255, 255, 255)
				                                                               : IM_COL32(20, 20, 20, 255)),
				                  i == selected ? 2.f : 1.f);
			}

			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_key >= 0)
				selected = hovered_key;
			// Drag the selected marker along the bar.
			if (ImGui::IsItemActive() && selected >= 0 && selected < static_cast<int>(keys.size()) &&
			    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f))
			{
				keys[selected].time = mouse_t;
				changed = true;
			}
			// Double-click : a new key with the color of the gradient there.
			if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered_key < 0)
			{
				keys.push_back({ mouse_t, Evaluate(keys, mouse_t) });
				selected = static_cast<int>(keys.size()) - 1;
				changed = true;
			}
			// Right click : delete (one key at least).
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered_key >= 0 && keys.size() > 1)
			{
				keys.erase(keys.begin() + hovered_key);
				selected = -1;
				changed = true;
			}

			ImGui::TextDisabled("Double-click : add a key.  Drag a marker.  Right click : delete.");
			if (selected >= 0 && selected < static_cast<int>(keys.size()))
			{
				ImGui::PushID(selected);
				changed |= ImGui::ColorEdit4("Color", &keys[selected].color.x, ImGuiColorEditFlags_Float |
				                                                              ImGuiColorEditFlags_AlphaBar |
				                                                              ImGuiColorEditFlags_HDR);
				changed |= ImGui::SliderFloat("Time", &keys[selected].time, 0.f, 1.f, "%.3f");
				ImGui::PopID();
			}
			return changed;
		}

		// ---------------------------------------------------------------------
		// Curve editor (Size / Rotation Over Life)
		// ---------------------------------------------------------------------

		bool CurveEditor(const char* id, std::vector<px::FloatKey>& keys, int& selected, float default_max)
		{
			bool changed = false;
			ImGui::PushID(id);
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const float width = std::max(140.f, ImGui::GetContentRegionAvail().x);
			const float height = std::max(120.f, ImGui::GetFontSize() * 9.f);
			const ImVec2 a = ImGui::GetCursorScreenPos();
			const ImVec2 b(a.x + width, a.y + height);
			const float pad = ImGui::GetFontSize() * 0.6f;

			// Value range : the keys, with 0 and default_max, padded.
			float lo = 0.f, hi = default_max;
			for (const px::FloatKey& k : keys)
			{
				lo = std::min(lo, k.value);
				hi = std::max(hi, k.value);
			}
			const float span = std::max(1e-3f, hi - lo);
			lo -= span * 0.1f;
			hi += span * 0.1f;

			auto to_screen = [&](float t, float v)
			{
				return ImVec2(a.x + pad + (width - pad * 2.f) * t,
				              b.y - pad - (height - pad * 2.f) * ((v - lo) / (hi - lo)));
			};
			auto from_screen = [&](ImVec2 p, float& t, float& v)
			{
				t = std::clamp((p.x - a.x - pad) / (width - pad * 2.f), 0.f, 1.f);
				v = lo + (b.y - pad - p.y) / (height - pad * 2.f) * (hi - lo);
			};

			draw->AddRectFilled(a, b, IM_COL32(24, 25, 30, 255), 3.f);
			for (int i = 0; i <= 4; ++i)
			{
				const float t = i * 0.25f;
				draw->AddLine(to_screen(t, lo), to_screen(t, hi), IM_COL32(50, 52, 60, 255));
			}
			if (lo < 0.f && hi > 0.f)
				draw->AddLine(to_screen(0.f, 0.f), to_screen(1.f, 0.f), IM_COL32(90, 90, 100, 255));
			char label[32];
			std::snprintf(label, sizeof(label), "%.2f", hi);
			draw->AddText(ImVec2(a.x + 4.f, a.y + 2.f), IM_COL32(130, 130, 140, 255), label);
			std::snprintf(label, sizeof(label), "%.2f", lo);
			draw->AddText(ImVec2(a.x + 4.f, b.y - ImGui::GetFontSize() - 2.f), IM_COL32(130, 130, 140, 255), label);

			// The curve
			constexpr int kSteps = 64;
			ImVec2 previous = to_screen(0.f, Evaluate(keys, 0.f));
			for (int i = 1; i <= kSteps; ++i)
			{
				const float t = static_cast<float>(i) / kSteps;
				const ImVec2 p = to_screen(t, Evaluate(keys, t));
				draw->AddLine(previous, p, IM_COL32(90, 200, 230, 255), 2.f);
				previous = p;
			}

			ImGui::InvisibleButton("##curve", ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const float radius = ImGui::GetFontSize() * 0.32f;

			int hovered_key = -1;
			for (int i = 0; i < static_cast<int>(keys.size()); ++i)
			{
				const ImVec2 p = to_screen(std::clamp(keys[i].time, 0.f, 1.f), keys[i].value);
				const float dx = mouse.x - p.x, dy = mouse.y - p.y;
				if (hovered && dx * dx + dy * dy <= (radius * 2.f) * (radius * 2.f))
					hovered_key = i;
			}
			for (int i = 0; i < static_cast<int>(keys.size()); ++i)
			{
				const ImVec2 p = to_screen(std::clamp(keys[i].time, 0.f, 1.f), keys[i].value);
				draw->AddCircleFilled(p, radius, i == selected ? IM_COL32(255, 200, 60, 255)
				                                               : (i == hovered_key ? IM_COL32(255, 255, 255, 255)
				                                                                   : IM_COL32(200, 200, 210, 255)));
			}

			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_key >= 0)
				selected = hovered_key;
			if (ImGui::IsItemActive() && selected >= 0 && selected < static_cast<int>(keys.size()) &&
			    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f))
			{
				from_screen(mouse, keys[selected].time, keys[selected].value);
				changed = true;
			}
			if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered_key < 0)
			{
				px::FloatKey k;
				from_screen(mouse, k.time, k.value);
				keys.push_back(k);
				selected = static_cast<int>(keys.size()) - 1;
				changed = true;
			}
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered_key >= 0 && keys.size() > 1)
			{
				keys.erase(keys.begin() + hovered_key);
				selected = -1;
				changed = true;
			}

			ImGui::TextDisabled("Double-click : add a key.  Drag a key.  Right click : delete.");
			if (selected >= 0 && selected < static_cast<int>(keys.size()))
			{
				changed |= ImGui::SliderFloat("Time", &keys[selected].time, 0.f, 1.f, "%.3f");
				changed |= ImGui::DragFloat("Value", &keys[selected].value, 0.01f);
			}
			ImGui::PopID();
			return changed;
		}

		// ---------------------------------------------------------------------
		// Details
		// ---------------------------------------------------------------------

		const std::vector<std::pair<px::Shape, const char*>> kShapes = {
			{ px::Shape::Point, "Point" }, { px::Shape::Sphere, "Sphere" }, { px::Shape::Box, "Box" },
			{ px::Shape::Cylinder, "Cylinder" }, { px::Shape::Cone, "Cone" },
		};
		const std::vector<std::pair<px::Blend, const char*>> kBlends = {
			{ px::Blend::Additive, "Additive (glow)" }, { px::Blend::Alpha, "Alpha (smoke)" },
			{ px::Blend::Multiply, "Multiply (dark)" },
		};
		const std::vector<std::pair<px::Render, const char*>> kRenders = {
			{ px::Render::Billboard, "Billboard" }, { px::Render::Stretched, "Stretched (along the speed)" },
		};
		const std::vector<std::pair<px::Space, const char*>> kSpaces = {
			{ px::Space::World, "World (trails behind a moving actor)" },
			{ px::Space::Local, "Local (moves with the actor)" },
		};

		void DrawDetails(Editor& e)
		{
			if (e.module == Module::System || e.desc.emitters.empty())
			{
				ImGui::SeparatorText("System");
				ImGui::DragFloat("Duration (s)", &e.desc.duration, 0.05f, 0.05f, 600.f, "%.2f");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("One cycle : bursts are timed in it ; a one-shot effect (Particles.spawn) lasts this long.");
				ImGui::Checkbox("Looping", &e.desc.looping);
				ImGui::TextDisabled("%d emitter(s). Click a module of a column below.",
				                    static_cast<int>(e.desc.emitters.size()));
				return;
			}

			px::EmitterDesc& em = e.desc.emitters[static_cast<size_t>(e.emitter)];
			ImGui::SeparatorText((em.name + "  >  " + ModuleName(e.module)).c_str());
			ImGui::PushID(e.emitter);
			ImGui::PushItemWidth(-ImGui::GetFontSize() * 8.f);

			if (bool* flag = ModuleFlag(em, e.module))
				ImGui::Checkbox("Module enabled", flag);

			switch (e.module)
			{
			case Module::Required:
			{
				ImGui::TextUnformatted("Texture");
				std::string texture = em.texture;
				bool texture_changed = false;
				if (g_asset_field)
					texture_changed = g_asset_field(texture);
				else
				{
					char buffer[260];
					std::snprintf(buffer, sizeof(buffer), "%s", texture.c_str());
					if (ImGui::InputTextWithHint("##texture", "assets path (.png) ; empty : square", buffer, sizeof(buffer)))
					{
						texture = buffer;
						texture_changed = true;
					}
				}
				if (texture_changed)
					em.texture = texture;
				EnumCombo("Blend", em.blend, kBlends);
				EnumCombo("Render", em.render, kRenders);
				if (em.render == px::Render::Stretched)
					ImGui::DragFloat("Stretch", &em.stretch, 0.005f, 0.f, 10.f);
				EnumCombo("Space", em.space, kSpaces);
				ImGui::DragFloat2("Size", &em.size.x, 0.01f, 0.f, 100.f);
				ImGui::DragInt("Max particles", &em.max_particles, 5.f, 1, 100000);
				break;
			}
			case Module::Spawn:
			{
				ImGui::DragFloat("Rate (/s)", &em.spawn_rate, 0.5f, 0.f, 100000.f);
				ImGui::SeparatorText("Bursts");
				int remove = -1;
				for (int i = 0; i < static_cast<int>(em.bursts.size()); ++i)
				{
					ImGui::PushID(i);
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
					ImGui::DragFloat("##time", &em.bursts[i].time, 0.01f, 0.f, 600.f, "at %.2f s");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
					ImGui::DragInt("##count", &em.bursts[i].count, 1.f, 0, 100000, "%d particles");
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
						remove = i;
					ImGui::PopID();
				}
				if (remove >= 0)
					em.bursts.erase(em.bursts.begin() + remove);
				if (ImGui::Button("+ Burst"))
					em.bursts.push_back({ 0.f, 20 });
				break;
			}
			case Module::Lifetime:
				RangeField("Lifetime (s)", em.lifetime, 0.01f, 0.01f, 600.f);
				break;
			case Module::Location:
				EnumCombo("Shape", em.shape, kShapes);
				if (em.shape == px::Shape::Sphere || em.shape == px::Shape::Cylinder || em.shape == px::Shape::Cone)
					ImGui::DragFloat("Radius", &em.shape_radius, 0.02f, 0.f, 1000.f);
				if (em.shape == px::Shape::Box)
					Vec3Field("Box size", em.shape_size);
				if (em.shape == px::Shape::Cone)
					ImGui::DragFloat("Angle (deg)", &em.shape_angle, 0.5f, 0.f, 180.f);
				Vec3Field("Offset", em.offset);
				Vec3Field("Rotation", em.shape_rotation, 0.5f);
				break;
			case Module::Velocity:
				Vec3Field("Min", em.velocity_min);
				Vec3Field("Max", em.velocity_max);
				RangeField("Speed (shape)", em.speed, 0.05f, -1000.f, 1000.f);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Outwards from the shape (sphere) / along the cone, added to Min..Max.");
				break;
			case Module::Rotation:
				Vec3Field("Rotation min", em.rotation_min, 0.5f);
				Vec3Field("Rotation max", em.rotation_max, 0.5f);
				Vec3Field("Angular velocity min", em.angular_min, 0.5f);
				Vec3Field("Angular velocity max", em.angular_max, 0.5f);
				ImGui::TextDisabled("Degrees ; Z turns the sprite in 2D.");
				break;
			case Module::Forces:
				Vec3Field("Gravity", em.gravity);
				ImGui::DragFloat("Drag", &em.drag, 0.01f, 0.f, 50.f);
				Vec3Field("Force", em.force);
				ImGui::SeparatorText("Noise (turbulence)");
				ImGui::DragFloat("Strength", &em.noise_strength, 0.02f, 0.f, 100.f);
				ImGui::DragFloat("Frequency", &em.noise_frequency, 0.01f, 0.f, 50.f);
				ImGui::DragFloat("Scroll speed", &em.noise_scroll, 0.01f, 0.f, 50.f);
				break;
			case Module::ColorOverLife:
				GradientEditor(em.color, e.selected_key);
				break;
			case Module::SizeOverLife:
				ImGui::TextDisabled("Multiplies the Size of Required, over the life (0 -> 1).");
				CurveEditor("size", em.size_curve, e.selected_key, 1.f);
				break;
			case Module::RotationOverLife:
				ImGui::TextDisabled("Rotation in degrees over the life (0 -> 1).");
				CurveEditor("rotation", em.rotation_curve, e.selected_key, 180.f);
				break;
			case Module::Collision:
				ImGui::SliderFloat("Restitution", &em.restitution, 0.f, 1.f);
				ImGui::SliderFloat("Friction", &em.friction, 0.f, 1.f);
				ImGui::TextDisabled("Collides with the scene of the game (voxels, meshes).");
				break;
			default:
				break;
			}

			ImGui::PopItemWidth();
			ImGui::PopID();
		}

		// ---------------------------------------------------------------------
		// Emitter columns (Cascade)
		// ---------------------------------------------------------------------

		void DrawEmitters(Editor& e)
		{
			const float column_w = ImGui::GetFontSize() * 12.f;
			const float row_h = ImGui::GetFrameHeight();
			int remove = -1, duplicate = -1, move_from = -1, move_to = -1;

			for (int i = 0; i < static_cast<int>(e.desc.emitters.size()); ++i)
			{
				px::EmitterDesc& em = e.desc.emitters[static_cast<size_t>(i)];
				ImGui::PushID(i);
				if (i > 0)
					ImGui::SameLine();
				const bool column_selected = e.emitter == i && e.module != Module::System;
				ImGui::PushStyleColor(ImGuiCol_Border, column_selected ? ImVec4(0.96f, 0.56f, 0.2f, 1.f)
				                                                     : ImGui::GetStyleColorVec4(ImGuiCol_Border));
				ImGui::BeginChild("##column", ImVec2(column_w, 0.f), ImGuiChildFlags_Borders);
				ImGui::PopStyleColor();

				// Header : enabled, name (double-click : rename), menu.
				ImGui::Checkbox("##enabled", &em.enabled);
				ImGui::SameLine();
				if (e.rename == i)
				{
					ImGui::SetNextItemWidth(-1.f);
					ImGui::SetKeyboardFocusHere();
					if (ImGui::InputText("##rename", e.rename_buffer, sizeof(e.rename_buffer),
					                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
					{
						if (e.rename_buffer[0])
							em.name = e.rename_buffer;
						e.rename = -1;
					}
					if (ImGui::IsItemDeactivated())
						e.rename = -1;
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Text, em.enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
					                                                : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					if (ImGui::Selectable(em.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
					{
						e.emitter = i;
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							e.rename = i;
							std::snprintf(e.rename_buffer, sizeof(e.rename_buffer), "%s", em.name.c_str());
						}
					}
					ImGui::PopStyleColor();
				}
				if (ImGui::BeginPopupContextItem("##emitter_menu"))
				{
					if (ImGui::MenuItem("Rename"))
					{
						e.rename = i;
						std::snprintf(e.rename_buffer, sizeof(e.rename_buffer), "%s", em.name.c_str());
					}
					if (ImGui::MenuItem("Duplicate")) duplicate = i;
					if (ImGui::MenuItem("Move left", nullptr, false, i > 0)) { move_from = i; move_to = i - 1; }
					if (ImGui::MenuItem("Move right", nullptr, false, i + 1 < static_cast<int>(e.desc.emitters.size())))
					{
						move_from = i;
						move_to = i + 1;
					}
					if (ImGui::BeginMenu("Add module"))
					{
						bool any = false;
						for (int m = static_cast<int>(Module::Required); m < static_cast<int>(Module::Count); ++m)
							if (bool* flag = ModuleFlag(em, static_cast<Module>(m)); flag && !*flag)
							{
								any = true;
								if (ImGui::MenuItem(ModuleName(static_cast<Module>(m))))
								{
									*flag = true;
									e.emitter = i;
									e.module = static_cast<Module>(m);
								}
							}
						if (!any)
							ImGui::TextDisabled("Every module is there.");
						ImGui::EndMenu();
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Delete")) remove = i;
					ImGui::EndPopup();
				}
				ImGui::Separator();

				// Module blocks
				ImDrawList* draw = ImGui::GetWindowDrawList();
				for (int m = static_cast<int>(Module::Required); m < static_cast<int>(Module::Count); ++m)
				{
					const Module mod = static_cast<Module>(m);
					bool* flag = ModuleFlag(em, mod);
					if (flag && !*flag)
						continue;   // removed : "Add module" in the header menu
					ImGui::PushID(m);
					const bool selected = e.emitter == i && e.module == mod;
					const ImVec2 p = ImGui::GetCursorScreenPos();
					if (ImGui::Selectable("##module", selected, 0, ImVec2(0.f, row_h)))
					{
						e.emitter = i;
						e.module = mod;
						e.selected_key = -1;
					}
					if (flag && ImGui::BeginPopupContextItem("##module_menu"))
					{
						if (ImGui::MenuItem("Remove module"))
						{
							*flag = false;
							if (selected)
								e.module = Module::Required;
						}
						ImGui::EndPopup();
					}
					const float w = ImGui::GetItemRectSize().x;
					draw->AddRectFilled(p, ImVec2(p.x + 4.f, p.y + row_h), ModuleColor(mod));
					draw->AddText(ImVec2(p.x + 10.f, p.y + (row_h - ImGui::GetFontSize()) * 0.5f),
					              ImGui::GetColorU32(ImGuiCol_Text), ModuleName(mod));
					// Little previews : the gradient, the size curve.
					if (mod == Module::ColorOverLife)
					{
						const float sw = w * 0.32f;
						DrawGradientStrip(draw, ImVec2(p.x + w - sw - 4.f, p.y + row_h * 0.25f),
						                  ImVec2(p.x + w - 4.f, p.y + row_h * 0.75f), em.color);
					}
					ImGui::PopID();
				}
				ImGui::EndChild();
				// Click on the empty part of a column : that emitter.
				ImGui::PopID();
			}

			// "+ Emitter" column
			if (!e.desc.emitters.empty())
				ImGui::SameLine();
			if (ImGui::Button("+ Emitter", ImVec2(column_w * 0.6f, row_h * 2.f)))
			{
				px::EmitterDesc em;
				em.name = "Emitter " + std::to_string(e.desc.emitters.size() + 1);
				e.desc.emitters.push_back(em);
				e.emitter = static_cast<int>(e.desc.emitters.size()) - 1;
				e.module = Module::Required;
			}

			if (duplicate >= 0)
			{
				px::EmitterDesc copy = e.desc.emitters[static_cast<size_t>(duplicate)];
				copy.name += " copy";
				e.desc.emitters.insert(e.desc.emitters.begin() + duplicate + 1, copy);
				e.emitter = duplicate + 1;
			}
			if (move_from >= 0)
			{
				std::swap(e.desc.emitters[static_cast<size_t>(move_from)], e.desc.emitters[static_cast<size_t>(move_to)]);
				e.emitter = move_to;
			}
			if (remove >= 0)
			{
				e.desc.emitters.erase(e.desc.emitters.begin() + remove);
				e.emitter = std::clamp(e.emitter, 0, std::max(0, static_cast<int>(e.desc.emitters.size()) - 1));
				if (e.desc.emitters.empty())
					e.module = Module::System;
			}
		}

		// ---------------------------------------------------------------------
		// Preview panel
		// ---------------------------------------------------------------------

		void DrawPreview(Editor& e, ImVec2 size)
		{
			Preview& pv = e.preview;
			pv.Create();
			if (pv.scene == kInvalid)
			{
				ImGui::TextDisabled("Preview unavailable (renderer).");
				return;
			}

			const ImGuiIO& io = ImGui::GetIO();
			const int w = std::max(16, static_cast<int>(size.x * io.DisplayFramebufferScale.x));
			const int h = std::max(16, static_cast<int>(size.y * io.DisplayFramebufferScale.y));
			if (w != pv.width || h != pv.height)
			{
				pv.width = w;
				pv.height = h;
				HRL_ResizeSceneTexture(pv.scene, w, h);
			}

			// Rebuild the system when the asset changed (not on every frame of a drag).
			const double now = ImGui::GetTime();
			if (e.rebuild && (!ImGui::IsAnyItemActive() || now - e.last_build > 0.25))
			{
				e.rebuild = false;
				e.last_build = now;
				pv.instance.Create(e.desc, pv.scene);
				pv.instance.SetTimeScale(pv.time_scale);
				if (pv.paused)
					pv.instance.Pause();
			}

			pv.ApplyCamera();
			pv.DrawGrid();

			const unsigned int texture = HRL_GL_GetSceneTextureGL_ID(pv.scene);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			if (texture != 0)
				ImGui::Image((ImTextureID)(intptr_t)texture, size, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));
			else
				ImGui::Dummy(size);

			// Camera : right drag orbit, middle drag pan, wheel zoom, F : frame.
			ImGui::SetCursorScreenPos(pos);
			ImGui::InvisibleButton("##preview_input", size, ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.f))
			{
				pv.yaw += io.MouseDelta.x * 0.3f;
				pv.pitch = std::clamp(pv.pitch - io.MouseDelta.y * 0.3f, -89.f, 89.f);
			}
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f))
			{
				const float k = pv.distance * 0.0015f;
				const float y = pv.yaw * kDegToRad;
				// right = (-sin yaw, 0, cos yaw) ; up ~ +Y
				pv.target[0] -= -std::sin(y) * io.MouseDelta.x * k;
				pv.target[2] -= std::cos(y) * io.MouseDelta.x * k;
				pv.target[1] += io.MouseDelta.y * k;
			}
			if (ImGui::IsItemHovered() && io.MouseWheel != 0.f)
				pv.distance = std::clamp(pv.distance * std::pow(0.88f, io.MouseWheel), 0.5f, 500.f);

			// Overlay : help
			ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 6.f, pos.y + 4.f), IM_COL32(200, 200, 210, 160),
			                                    "Right drag : orbit   Middle drag : pan   Wheel : zoom");
		}

		void Draw(Editor& e, ImGuiID dock_id)
		{
			const bool dirty = IsDirty(e);
			const std::string window = (dirty ? "* " : "") + e.title + "###particles_" + e.path.generic_string();
			if (dock_id != 0)
				ImGui::SetNextWindowDockID(dock_id, ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(1100.f, 720.f), ImGuiCond_FirstUseEver);
			if (e.focus)
			{
				ImGui::SetNextWindowFocus();
				e.focus = false;
			}
			const bool visible = ImGui::Begin(window.c_str(), &e.open, dirty ? ImGuiWindowFlags_UnsavedDocument : 0);

			// Closed : with unsaved changes, ask first.
			if (!e.open)
			{
				e.open = true;
				if (dirty)
				{
					e.confirm_close = true;
					ImGui::OpenPopup("Unsaved particle system");
				}
				else
					e.closed = true;
			}
			if (ImGui::BeginPopupModal("Unsaved particle system", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("%s has unsaved changes.", e.title.c_str());
				if (ImGui::Button("Save"))
				{
					Save(e);
					e.closed = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Discard"))
				{
					e.closed = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
				{
					e.confirm_close = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (!visible)
			{
				ImGui::End();
				return;
			}
			const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			g_focused = g_focused || focused;
			Preview& pv = e.preview;

			// ---- Shortcuts
			const ImGuiIO& io = ImGui::GetIO();
			if (focused && !io.WantTextInput)
			{
				if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
					Save(e);
				if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !e.undo.empty())
				{
					e.redo.push_back(px::ToJson(e.desc));
					Restore(e, e.undo.back());
					e.undo.pop_back();
				}
				if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false) && !e.redo.empty())
				{
					e.undo.push_back(px::ToJson(e.desc));
					Restore(e, e.redo.back());
					e.redo.pop_back();
				}
				if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
					pv.instance.Restart();
			}

			// ---- Toolbar
			if (ImGui::Button("Save"))
				Save(e);
			ImGui::SameLine();
			if (ImGui::Button("Restart"))
				pv.instance.Restart();
			ImGui::SameLine();
			if (ImGui::Button(pv.paused ? "Play" : "Pause"))
			{
				pv.paused = !pv.paused;
				if (pv.paused) pv.instance.Pause(); else pv.instance.Play();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
			if (ImGui::SliderFloat("##speed", &pv.time_scale, 0.f, 3.f, "speed x%.2f"))
				pv.instance.SetTimeScale(pv.time_scale);
			ImGui::SameLine();
			ImGui::Checkbox("Loop", &e.desc.looping);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
			ImGui::DragFloat("##duration", &e.desc.duration, 0.05f, 0.05f, 600.f, "%.2f s");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Duration of a cycle (bursts, one-shot effects).");
			ImGui::SameLine();
			if (ImGui::Button("System"))
				e.module = Module::System;
			ImGui::SameLine();
			ImGui::Checkbox("Grid", &pv.grid);
			ImGui::SameLine();
			ImGui::ColorEdit3("##background", pv.background, ImGuiColorEditFlags_NoInputs);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Background of the preview");
			ImGui::SameLine();
			if (ImGui::Button("Frame"))
			{
				pv.yaw = -90.f;
				pv.pitch = 0.f;
				pv.distance = 14.f;
				pv.target[0] = 0.f; pv.target[1] = 1.5f; pv.target[2] = 0.f;
			}

			// ---- Layout : preview | details, then the emitters
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const float top_h = std::max(120.f, avail.y * 0.58f);
			const float details_w = std::max(ImGui::GetFontSize() * 18.f, avail.x * 0.36f);

			ImGui::BeginChild("##preview", ImVec2(avail.x - details_w - ImGui::GetStyle().ItemSpacing.x, top_h),
			                  ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			DrawPreview(e, ImGui::GetContentRegionAvail());
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::BeginChild("##details", ImVec2(0.f, top_h), ImGuiChildFlags_Borders);
			DrawDetails(e);
			ImGui::EndChild();

			ImGui::BeginChild("##emitters", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			DrawEmitters(e);
			ImGui::EndChild();

			ImGui::End();

			// ---- Changes : undo snapshot (once the edit is done) and preview rebuild
			const std::string json = px::ToJson(e.desc);
			if (json != e.snapshot)
			{
				e.rebuild = true;
				if (!ImGui::IsAnyItemActive())
				{
					e.undo.push_back(e.snapshot);
					if (e.undo.size() > 200)
						e.undo.erase(e.undo.begin());
					e.redo.clear();
					e.snapshot = json;
				}
			}
		}
	}

	// =========================================================================

	bool CanOpen(const fs::path& path)
	{
		std::string ext = path.extension().string();
		for (char& c : ext)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return ext == ".vfx";
	}

	void Open(const fs::path& path)
	{
		for (auto& e : g_editors)
			if (e->path == path)
			{
				e->focus = true;
				return;
			}
		auto e = std::make_unique<Editor>();
		e->path = path;
		e->title = path.filename().string();
		Load(*e);
		g_editors.push_back(std::move(e));
	}

	void DrawAll(ImGuiID dock_id)
	{
		g_focused = false;
		for (auto& e : g_editors)
			Draw(*e, dock_id);

		// Closed windows (after Save / Discard when they had changes).
		for (auto it = g_editors.begin(); it != g_editors.end();)
		{
			if ((*it)->closed)
			{
				(*it)->preview.Destroy();
				it = g_editors.erase(it);
			}
			else
				++it;
		}
	}

	bool HasFocus()
	{
		return g_focused;
	}

	void PathMoved(const fs::path& from, const fs::path& to)
	{
		for (auto& e : g_editors)
		{
			std::error_code ec;
			if (e->path == from)
				e->path = to;
			else if (e->path.generic_string().rfind(from.generic_string() + "/", 0) == 0)
				e->path = to / e->path.lexically_relative(from);
			e->title = e->path.filename().string();
			(void)ec;
		}
	}

	void PathDeleted(const fs::path& target)
	{
		for (auto& e : g_editors)
			if (e->path == target || e->path.generic_string().rfind(target.generic_string() + "/", 0) == 0)
				e->closed = true;
	}

	bool HasUnsavedChanges()
	{
		for (const auto& e : g_editors)
			if (IsDirty(*e))
				return true;
		return false;
	}

	void SaveAll()
	{
		for (auto& e : g_editors)
			if (IsDirty(*e))
				Save(*e);
	}

	std::string NewFileTemplate()
	{
		return px::NewTemplate();
	}

	void SetAssetFieldDrawer(std::function<bool(std::string& value)> drawer)
	{
		g_asset_field = std::move(drawer);
	}

	void Shutdown()
	{
		for (auto& e : g_editors)
			e->preview.Destroy();
		g_editors.clear();
	}
}
