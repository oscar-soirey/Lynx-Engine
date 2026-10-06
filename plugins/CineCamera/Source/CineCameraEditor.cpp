// =============================================================================
// Plugin CineCamera : editor module — the Sequencer (.sequence files)
// -----------------------------------------------------------------------------
// Timeline with lanes : camera cuts, fades, events, one lane per animated
// actor. Click a key to edit it, drag it to move it in time, right click in a
// lane to add a key at the mouse, Del to delete. Scrubbing the time previews
// the sequence in the viewport (actors, camera, fade) ; the actors are put
// back when the preview ends (window closed / unfocused, Save, Play).
// =============================================================================

#include <editor/EditorPluginAPI.h>
#include <Lynx.h>

#include <json/json.hpp>

#include "SequenceData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <list>
#include <sstream>

using Json = nlohmann::json;
namespace sfs = std::filesystem;

namespace
{
	lynx::editor_api::EditorAPI* g_api = nullptr;

	int ResizeCallback(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
		{
			auto* str = static_cast<std::string*>(data->UserData);
			str->resize(static_cast<size_t>(data->BufTextLen));
			data->Buf = str->data();
		}
		return 0;
	}

	bool InputString(const char* label, std::string& value, bool multiline = false)
	{
		if (multiline)
			return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, ImVec2(-FLT_MIN, 70.f),
			                                 ImGuiInputTextFlags_CallbackResize, ResizeCallback, &value);
		return ImGui::InputText(label, value.data(), value.capacity() + 1, ImGuiInputTextFlags_CallbackResize,
		                        ResizeCallback, &value);
	}

	std::string ReadFile(const sfs::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::stringstream s;
		s << in.rdbuf();
		return s.str();
	}

	std::string Utf8(const sfs::path& p)
	{
		const auto u8 = p.u8string();
		return std::string(u8.begin(), u8.end());
	}

	std::vector<std::string> ActorIds(bool cameras_only)
	{
		std::vector<std::string> ids;
		lynx::Engine* engine = lynx::Engine::Get();
		lynx::Level* level = engine ? engine->GetCurrentLevel() : nullptr;
		if (!level)
			return ids;
		for (lynx::Actor* a : level->GetActors())
			if (a && !a->object_id_.empty() && (!cameras_only || a->HasComponent<lynx::CameraComponent>()))
				ids.push_back(a->object_id_);
		std::sort(ids.begin(), ids.end());
		return ids;
	}

	bool ActorCombo(const char* label, std::string& value, bool cameras_only)
	{
		bool changed = false;
		if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str()))
		{
			for (const std::string& id : ActorIds(cameras_only))
				if (ImGui::Selectable(id.c_str(), id == value))
				{
					value = id;
					changed = true;
				}
			ImGui::EndCombo();
		}
		return changed;
	}

	sequence::V3 V3(const lynx::vec3& v) { return { v.x, v.y, v.z }; }

	// =========================================================================

	enum class Lane { Cuts, Fades, Events, Track };

	struct Selection
	{
		Lane lane = Lane::Cuts;
		int track = -1;    // Lane::Track
		int key = -1;
		bool Valid() const { return key >= 0; }
	};

	struct Document
	{
		sfs::path path;
		std::string title;
		sequence::Sequence seq;
		std::string error;
		bool dirty = false;
		float time = 0.f;
		float zoom = 80.f;          // pixels per second
		bool playing = false;       // preview playback
		bool previewing = false;
		Selection selected;
		bool dragging = false;
	};

	std::list<Document> g_docs;
	Document* g_previewed = nullptr;

	void EndPreview()
	{
		if (g_previewed)
			lynx::ExecuteScript("Sequence.endPreview()", "<Sequencer>");
		if (g_previewed)
			g_previewed->previewing = false;
		g_previewed = nullptr;
	}

	void Preview(Document& doc)
	{
		if (g_api->is_playing())
			return;
		if (g_previewed && g_previewed != &doc)
			EndPreview();
		g_previewed = &doc;
		doc.previewing = true;
		const std::string code = "Sequence.preview(" + Json(sequence::Serialize(doc.seq)).dump() + ", " +
		                         std::to_string(doc.time) + ")";
		lynx::ExecuteScript(code.c_str(), "<Sequencer>");
	}

	bool Save(Document& doc)
	{
		doc.seq.Sort();
		std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			g_api->message(("Could not write " + Utf8(doc.path)).c_str());
			return false;
		}
		out << sequence::Serialize(doc.seq);
		doc.dirty = false;
		return true;
	}

	float* KeyTime(Document& doc, const Selection& s)
	{
		auto& q = doc.seq;
		switch (s.lane)
		{
		case Lane::Cuts: return s.key < static_cast<int>(q.cuts.size()) ? &q.cuts[s.key].t : nullptr;
		case Lane::Fades: return s.key < static_cast<int>(q.fades.size()) ? &q.fades[s.key].t : nullptr;
		case Lane::Events: return s.key < static_cast<int>(q.events.size()) ? &q.events[s.key].t : nullptr;
		case Lane::Track:
			if (s.track < 0 || s.track >= static_cast<int>(q.tracks.size())) return nullptr;
			return s.key < static_cast<int>(q.tracks[s.track].keys.size()) ? &q.tracks[s.track].keys[s.key].t : nullptr;
		}
		return nullptr;
	}

	void DeleteKey(Document& doc, const Selection& s)
	{
		auto& q = doc.seq;
		switch (s.lane)
		{
		case Lane::Cuts: if (s.key < static_cast<int>(q.cuts.size())) q.cuts.erase(q.cuts.begin() + s.key); break;
		case Lane::Fades: if (s.key < static_cast<int>(q.fades.size())) q.fades.erase(q.fades.begin() + s.key); break;
		case Lane::Events: if (s.key < static_cast<int>(q.events.size())) q.events.erase(q.events.begin() + s.key); break;
		case Lane::Track:
			if (s.track >= 0 && s.track < static_cast<int>(q.tracks.size()) && s.key < static_cast<int>(q.tracks[s.track].keys.size()))
				q.tracks[s.track].keys.erase(q.tracks[s.track].keys.begin() + s.key);
			break;
		}
		doc.selected = {};
		doc.dirty = true;
	}

	sequence::TransformKey KeyFromActor(const std::string& id, float t)
	{
		sequence::TransformKey key;
		key.t = t;
		lynx::Engine* engine = lynx::Engine::Get();
		lynx::Level* level = engine ? engine->GetCurrentLevel() : nullptr;
		if (lynx::Actor* a = level ? level->GetActorFromID(id.c_str()) : nullptr)
		{
			key.location = V3(a->transform.location);
			key.rotation = V3(a->transform.rotation);
			key.scale = V3(a->transform.scale);
		}
		return key;
	}

	int AddKey(Document& doc, Lane lane, int track, float t)
	{
		auto& q = doc.seq;
		t = std::clamp(t, 0.f, q.length);
		doc.dirty = true;
		switch (lane)
		{
		case Lane::Cuts:
		{
			const auto cams = ActorIds(true);
			q.cuts.push_back({ t, cams.empty() ? std::string() : cams.front(), 0.f });
			return static_cast<int>(q.cuts.size()) - 1;
		}
		case Lane::Fades:
			q.fades.push_back({ t, 0.f, {} });
			return static_cast<int>(q.fades.size()) - 1;
		case Lane::Events:
			q.events.push_back({ t, "Event", "" });
			return static_cast<int>(q.events.size()) - 1;
		case Lane::Track:
			q.tracks[track].keys.push_back(KeyFromActor(q.tracks[track].actor, t));
			return static_cast<int>(q.tracks[track].keys.size()) - 1;
		}
		return -1;
	}

	// ---- Timeline ------------------------------------------------------------

	void DrawTimeline(Document& doc)
	{
		auto& q = doc.seq;
		const float label_w = 170.f;
		const float lane_h = 24.f;
		const float ruler_h = 22.f;

		struct LaneInfo { Lane lane; int track; std::string label; };
		std::vector<LaneInfo> lanes = { { Lane::Cuts, -1, "Camera cuts" }, { Lane::Fades, -1, "Fade" }, { Lane::Events, -1, "Events" } };
		for (int i = 0; i < static_cast<int>(q.tracks.size()); ++i)
			lanes.push_back({ Lane::Track, i, q.tracks[i].actor.empty() ? "(actor ?)" : q.tracks[i].actor });

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = std::max(200.f, ImGui::GetContentRegionAvail().x);
		const float height = ruler_h + lane_h * static_cast<float>(lanes.size()) + 4.f;
		const float x0 = origin.x + label_w;
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImGui::InvisibleButton("##timeline", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 mouse = ImGui::GetMousePos();
		auto time_at = [&](float x) { return std::max(0.f, (x - x0) / doc.zoom); };
		auto x_at = [&](float t) { return x0 + t * doc.zoom; };

		// Background, ruler
		dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(28, 28, 32, 255));
		dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(std::min(x_at(q.length), origin.x + width), origin.y + height), IM_COL32(36, 36, 42, 255));
		const float step = doc.zoom >= 60.f ? 1.f : doc.zoom >= 20.f ? 5.f : 10.f;
		for (float t = 0.f; x_at(t) < origin.x + width; t += step)
		{
			const float x = x_at(t);
			dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), IM_COL32(60, 60, 68, 255));
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%gs", t);
			dl->AddText(ImVec2(x + 3.f, origin.y + 3.f), IM_COL32(170, 170, 180, 255), buf);
		}

		// Lanes
		for (int li = 0; li < static_cast<int>(lanes.size()); ++li)
		{
			const float y = origin.y + ruler_h + lane_h * static_cast<float>(li);
			dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y), IM_COL32(55, 55, 62, 255));
			dl->AddText(ImVec2(origin.x + 6.f, y + 4.f), IM_COL32(220, 220, 225, 255), lanes[li].label.c_str());

			auto draw_key = [&](float t, int key, ImU32 color)
			{
				const float x = x_at(t);
				const float cy = y + lane_h * 0.5f;
				const float r = 6.f;
				const bool sel = doc.selected.Valid() && doc.selected.lane == lanes[li].lane &&
				                 doc.selected.track == lanes[li].track && doc.selected.key == key;
				const ImVec2 pts[4] = { ImVec2(x, cy - r), ImVec2(x + r, cy), ImVec2(x, cy + r), ImVec2(x - r, cy) };
				dl->AddConvexPolyFilled(pts, 4, sel ? IM_COL32(255, 255, 255, 255) : color);

				// Click : select / start a drag
				if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && std::fabs(mouse.x - x) <= r + 2.f &&
				    std::fabs(mouse.y - cy) <= r + 2.f)
				{
					doc.selected = { lanes[li].lane, lanes[li].track, key };
					doc.dragging = true;
				}
			};

			switch (lanes[li].lane)
			{
			case Lane::Cuts:
				for (int k = 0; k < static_cast<int>(q.cuts.size()); ++k)
				{
					const auto& c = q.cuts[k];
					if (c.blend > 0.f)
						dl->AddRectFilled(ImVec2(x_at(c.t), y + 8.f), ImVec2(x_at(c.t + c.blend), y + lane_h - 8.f), IM_COL32(230, 170, 60, 90));
					dl->AddText(ImVec2(x_at(c.t) + 8.f, y + 4.f), IM_COL32(230, 190, 110, 255), c.camera.c_str());
					draw_key(c.t, k, IM_COL32(230, 170, 60, 255));
				}
				break;
			case Lane::Fades:
				for (int k = 0; k < static_cast<int>(q.fades.size()); ++k)
					draw_key(q.fades[k].t, k, IM_COL32(150, 150, 160, 255));
				break;
			case Lane::Events:
				for (int k = 0; k < static_cast<int>(q.events.size()); ++k)
				{
					dl->AddText(ImVec2(x_at(q.events[k].t) + 8.f, y + 4.f), IM_COL32(230, 120, 130, 255), q.events[k].name.c_str());
					draw_key(q.events[k].t, k, IM_COL32(220, 80, 90, 255));
				}
				break;
			case Lane::Track:
				for (int k = 0; k < static_cast<int>(q.tracks[lanes[li].track].keys.size()); ++k)
					draw_key(q.tracks[lanes[li].track].keys[k].t, k, IM_COL32(90, 160, 230, 255));
				break;
			}

			// Right click in the lane : a key at the mouse.
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouse.y >= y && mouse.y < y + lane_h && mouse.x >= x0)
			{
				const int key = AddKey(doc, lanes[li].lane, lanes[li].track, time_at(mouse.x));
				doc.selected = { lanes[li].lane, lanes[li].track, key };
			}
		}

		// Drag a key / scrub the time (ruler or empty place)
		if (doc.dragging)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				if (float* t = KeyTime(doc, doc.selected))
				{
					const float nt = std::clamp(time_at(mouse.x), 0.f, q.length);
					if (nt != *t)
					{
						*t = std::round(nt * 100.f) / 100.f;
						doc.dirty = true;
					}
				}
			}
			else
			{
				doc.dragging = false;
				// Keys sorted again : the selection follows by time.
				if (float* t = KeyTime(doc, doc.selected))
				{
					const float moved = *t;
					q.Sort();
					auto refind = [&](auto& keys) { for (int k = 0; k < static_cast<int>(keys.size()); ++k) if (keys[k].t == moved) return k; return -1; };
					switch (doc.selected.lane)
					{
					case Lane::Cuts: doc.selected.key = refind(q.cuts); break;
					case Lane::Fades: doc.selected.key = refind(q.fades); break;
					case Lane::Events: doc.selected.key = refind(q.events); break;
					case Lane::Track: doc.selected.key = refind(q.tracks[doc.selected.track].keys); break;
					}
				}
			}
		}
		else if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mouse.x >= x0)
		{
			doc.time = std::clamp(time_at(mouse.x), 0.f, q.length);
			doc.playing = false;
			Preview(doc);
		}

		// Zoom : Ctrl + wheel
		if (hovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.f)
			doc.zoom = std::clamp(doc.zoom * (ImGui::GetIO().MouseWheel > 0.f ? 1.2f : 1.f / 1.2f), 5.f, 600.f);

		// Playhead
		const float px = x_at(doc.time);
		dl->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + height), IM_COL32(255, 80, 80, 255), 2.f);
		// End
		dl->AddLine(ImVec2(x_at(q.length), origin.y), ImVec2(x_at(q.length), origin.y + height), IM_COL32(200, 200, 200, 120));
	}

	// ---- Key properties ------------------------------------------------------

	void DrawKey(Document& doc)
	{
		auto& q = doc.seq;
		const Selection s = doc.selected;
		float* t = s.Valid() ? KeyTime(doc, s) : nullptr;
		if (!t)
		{
			ImGui::TextDisabled("Select a key (click). Right click in a lane : new key. Drag the ruler : preview.");
			return;
		}

		bool changed = false;
		ImGui::PushItemWidth(260.f);
		changed |= ImGui::DragFloat("Time (s)", t, 0.01f, 0.f, q.length, "%.2f");

		switch (s.lane)
		{
		case Lane::Cuts:
		{
			auto& c = q.cuts[s.key];
			changed |= ActorCombo("Camera", c.camera, true);
			changed |= ImGui::DragFloat("Blend (s)", &c.blend, 0.01f, 0.f, 30.f, "%.2f");
			break;
		}
		case Lane::Fades:
		{
			auto& f = q.fades[s.key];
			changed |= ImGui::SliderFloat("Fade (0 = scene, 1 = color)", &f.value, 0.f, 1.f);
			changed |= ImGui::ColorEdit3("Color", &f.color.x);
			break;
		}
		case Lane::Events:
		{
			auto& e = q.events[s.key];
			ImGui::TextUnformatted("Name (SequenceEvents.OnSequenceEvent)");
			changed |= InputString("##name", e.name);
			ImGui::TextUnformatted("JavaScript (optional)");
			ImGui::PopItemWidth();
			changed |= InputString("##script", e.script, true);
			ImGui::PushItemWidth(260.f);
			break;
		}
		case Lane::Track:
		{
			auto& track = q.tracks[s.track];
			auto& k = track.keys[s.key];
			changed |= ImGui::DragFloat3("Location", &k.location.x, 0.1f);
			changed |= ImGui::DragFloat3("Rotation", &k.rotation.x, 0.5f);
			changed |= ImGui::DragFloat3("Scale", &k.scale.x, 0.01f);
			const char* modes[] = { "smooth", "linear", "step" };
			int mode = k.interp == "linear" ? 1 : k.interp == "step" ? 2 : 0;
			if (ImGui::Combo("To the next key", &mode, modes, 3))
			{
				k.interp = modes[mode];
				changed = true;
			}
			if (ImGui::Button("Set from the actor"))
			{
				// The preview moved the actor : put it back to read its real place.
				const float time = k.t;
				const std::string interp = k.interp;
				EndPreview();
				k = KeyFromActor(track.actor, time);
				k.interp = interp;
				changed = true;
			}
			break;
		}
		}
		ImGui::PopItemWidth();

		if (ImGui::Button("Delete key") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		                                     ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput))
		{
			DeleteKey(doc, s);
			changed = true;
		}

		if (changed)
		{
			doc.dirty = true;
			Preview(doc);
		}
	}

	// ---- Window --------------------------------------------------------------

	void DrawDocument(bool* open, void* user)
	{
		Document& doc = *static_cast<Document*>(user);
		ImGui::SetNextWindowSize(ImVec2(1100.f, 420.f), ImGuiCond_FirstUseEver);
		const bool visible = ImGui::Begin(doc.title.c_str(), open, doc.dirty ? ImGuiWindowFlags_UnsavedDocument : 0);
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (!visible || !doc.error.empty())
		{
			if (!doc.error.empty())
				ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "%s", doc.error.c_str());
			ImGui::End();
			if (g_previewed == &doc)
				EndPreview();
			return;
		}

		auto& q = doc.seq;

		// Transport
		if (ImGui::Button(doc.playing ? "Pause" : "Play"))
			doc.playing = !doc.playing;
		ImGui::SameLine();
		if (ImGui::Button("Stop"))
		{
			doc.playing = false;
			doc.time = 0.f;
			EndPreview();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.f);
		if (ImGui::SliderFloat("##time", &doc.time, 0.f, q.length, "%.2f s"))
			Preview(doc);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.f);
		if (ImGui::DragFloat("Length", &q.length, 0.05f, 0.1f, 3600.f, "%.1f s"))
			doc.dirty = true;
		ImGui::SameLine();
		if (ImGui::Button("Save") || (focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)))
		{
			EndPreview();
			Save(doc);
		}
		ImGui::SameLine();
		if (ImGui::Button("+ Actor track"))
			ImGui::OpenPopup("##addtrack");
		if (ImGui::BeginPopup("##addtrack"))
		{
			if (lynx::Actor* sel = g_api->get_selected_actor())
				if (!sel->object_id_.empty() && ImGui::MenuItem(("Selected : " + sel->object_id_).c_str()))
				{
					q.tracks.push_back({ sel->object_id_, { KeyFromActor(sel->object_id_, doc.time) } });
					doc.dirty = true;
				}
			ImGui::Separator();
			for (const std::string& id : ActorIds(false))
				if (ImGui::MenuItem(id.c_str()))
				{
					q.tracks.push_back({ id, { KeyFromActor(id, doc.time) } });
					doc.dirty = true;
				}
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		if (doc.selected.lane == Lane::Track && doc.selected.track >= 0 && ImGui::Button("Remove track"))
		{
			q.tracks.erase(q.tracks.begin() + doc.selected.track);
			doc.selected = {};
			doc.dirty = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", Utf8(doc.path.filename()).c_str());

		ImGui::Separator();
		ImGui::BeginChild("##lanes", ImVec2(0.f, -170.f), false, ImGuiWindowFlags_HorizontalScrollbar);
		DrawTimeline(doc);
		ImGui::EndChild();
		ImGui::Separator();
		ImGui::BeginChild("##key");
		DrawKey(doc);
		ImGui::EndChild();

		// Preview playback
		if (doc.playing)
		{
			doc.time += ImGui::GetIO().DeltaTime;
			if (doc.time >= q.length)
			{
				doc.time = q.length;
				doc.playing = false;
			}
			Preview(doc);
		}

		ImGui::End();

		// The preview only lives while the sequencer is used.
		if (g_previewed == &doc && !focused && !doc.playing)
			EndPreview();
	}

	bool OpenSequence(const char* path_utf8, void*)
	{
		const sfs::path path = sfs::path(reinterpret_cast<const char8_t*>(path_utf8));
		for (Document& doc : g_docs)
			if (doc.path == path)
			{
				g_api->open_window(doc.title.c_str());
				return true;
			}

		g_docs.emplace_back();
		Document& doc = g_docs.back();
		doc.path = path;
		doc.title = "Sequencer - " + Utf8(path.filename()) + "##" + path_utf8;
		std::string error;
		const std::string text = ReadFile(path);
		if (!text.empty() && !sequence::Parse(text, doc.seq, error))
			doc.error = "Not a sequence : " + error;

		g_api->add_window("CineCamera", doc.title.c_str(), DrawDocument, &doc, true);
		g_api->open_window(doc.title.c_str());
		return true;
	}

	void BeforeSnapshot(void*)
	{
		for (Document& doc : g_docs)
			doc.playing = false;
		EndPreview();
	}
}


LYNX_EDITOR_PLUGIN_STARTUP(api)
{
	LYNX_EDITOR_PLUGIN_INIT(api);
	g_api = api;

	api->add_file_editor("CineCamera", ".sequence", OpenSequence, nullptr);
	api->add_new_file("CineCamera", "Sequence (.sequence, cinematic)", "NewSequence", ".sequence",
		"{\n  \"length\": 5,\n  \"cuts\": [],\n  \"tracks\": [],\n  \"events\": [],\n  \"fades\": []\n}\n");
	api->add_before_level_snapshot("CineCamera", BeforeSnapshot, nullptr);
}

LYNX_EDITOR_PLUGIN_SHUTDOWN()
{
	EndPreview();
	g_docs.clear();
}
