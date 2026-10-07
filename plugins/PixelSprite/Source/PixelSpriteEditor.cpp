// =============================================================================
// Plugin PixelSprite : editor module
// -----------------------------------------------------------------------------
// Pixel art editor of the .lsprite files (double-click in the Content Browser,
// New file > Pixel sprite). The sprite stays a .lsprite (palette + indices) :
// it is never exported as an image, and can be edited again at any time.
// Saving reloads its texture in the editor (every sprite using it updates).
//
//   Tools : pencil (B), eraser (E), fill (G), line (L), rectangle (R),
//           filled rectangle (U), picker (I, or Alt), selection / move (M)
//   Left click : primary color, right click : secondary color, X : swap
//   Mirror X, onion skin, grid, zoom (wheel), pan (middle button / Space)
//   Frames : add, duplicate, delete, move, fps, loop, preview
//   Palette : shared by the frames ; editing a color recolors the sprite
//   Ctrl+Z / Ctrl+Y, Ctrl+S, Ctrl+A / Ctrl+C / Ctrl+V / Del, [ ] : frames
// =============================================================================

#include <editor/EditorPluginAPI.h>
#include <Lynx.h>

#include "LSprite.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <list>
#include <sstream>

namespace sfs = std::filesystem;

namespace
{
	lynx::editor_api::EditorAPI* g_api = nullptr;

	enum class Tool { Pencil, Eraser, Fill, Line, Rect, FilledRect, Picker, Select, Count };

	const char* ToolName(Tool t)
	{
		switch (t)
		{
		case Tool::Pencil: return "Pencil (B)";
		case Tool::Eraser: return "Eraser (E)";
		case Tool::Fill: return "Fill (G)";
		case Tool::Line: return "Line (L)";
		case Tool::Rect: return "Rectangle (R)";
		case Tool::FilledRect: return "Filled rectangle (U)";
		case Tool::Picker: return "Color picker (I / Alt)";
		case Tool::Select: return "Select / move (M)";
		default: return "";
		}
	}

	const char* ToolShort(Tool t)
	{
		switch (t)
		{
		case Tool::Pencil: return "Pen";
		case Tool::Eraser: return "Erase";
		case Tool::Fill: return "Fill";
		case Tool::Line: return "Line";
		case Tool::Rect: return "Rect";
		case Tool::FilledRect: return "Box";
		case Tool::Picker: return "Pick";
		case Tool::Select: return "Select";
		default: return "";
		}
	}

	ImU32 ToImColor(uint32_t c, float alpha = 1.f)
	{
		return IM_COL32(lsprite::R(c), lsprite::G(c), lsprite::B(c), static_cast<int>(lsprite::A(c) * alpha));
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

	struct Rect
	{
		int x0 = 0, y0 = 0, x1 = -1, y1 = -1;   // inclusive
		bool Valid() const { return x1 >= x0 && y1 >= y0; }
		bool Contains(int x, int y) const { return Valid() && x >= x0 && x <= x1 && y >= y0 && y <= y1; }
		int W() const { return x1 - x0 + 1; }
		int H() const { return y1 - y0 + 1; }
	};

	Rect Normalized(int ax, int ay, int bx, int by)
	{
		return { std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by) };
	}

	// Copy / paste between the sprites of the editor.
	struct Clip
	{
		int w = 0, h = 0;
		std::vector<uint32_t> colors;   // RGBA : pasted in another palette
	};
	Clip g_clipboard;

	struct Document
	{
		sfs::path path;
		std::string title;
		lsprite::Sprite sprite;
		std::string error;
		bool dirty = false;

		// History (snapshots of the whole sprite, small).
		std::vector<std::string> undo, redo;

		int frame = 0;
		Tool tool = Tool::Pencil;
		int primary = 1, secondary = 0;
		bool mirror_x = false;
		bool onion = true;
		bool grid = true;

		float zoom = 0.f;           // 0 : fit at the first draw
		ImVec2 pan{ 0.f, 0.f };     // offset of the sprite in the canvas

		// Stroke in progress
		bool drawing = false;
		int draw_color = 1;
		int last_x = 0, last_y = 0;
		int start_x = 0, start_y = 0;
		bool shape_preview = false;

		// Selection : rectangle, and its lifted pixels while moving.
		Rect selection;
		bool moving = false;
		std::vector<uint8_t> floating;
		Rect floating_rect;
		int move_dx = 0, move_dy = 0;
		int grab_x = 0, grab_y = 0;

		// Preview
		bool preview_playing = true;
		float preview_time = 0.f;

		// Resize popup
		int new_w = 16, new_h = 16;

		// Edited color (palette)
		int editing_color = -1;
	};

	std::list<Document> g_docs;

	// ---- History -------------------------------------------------------------

	void PushUndo(Document& doc)
	{
		doc.undo.push_back(lsprite::Serialize(doc.sprite));
		if (doc.undo.size() > 200)
			doc.undo.erase(doc.undo.begin());
		doc.redo.clear();
		doc.dirty = true;
	}

	void Restore(Document& doc, const std::string& text)
	{
		std::string error;
		lsprite::Parse(text, doc.sprite, error);
		doc.frame = std::clamp(doc.frame, 0, doc.sprite.FrameCount() - 1);
		doc.primary = std::min(doc.primary, static_cast<int>(doc.sprite.palette.size()) - 1);
		doc.secondary = std::min(doc.secondary, static_cast<int>(doc.sprite.palette.size()) - 1);
		doc.selection = {};
		doc.moving = false;
		doc.dirty = true;
	}

	void Undo(Document& doc)
	{
		if (doc.undo.empty())
			return;
		doc.redo.push_back(lsprite::Serialize(doc.sprite));
		const std::string s = std::move(doc.undo.back());
		doc.undo.pop_back();
		Restore(doc, s);
	}

	void Redo(Document& doc)
	{
		if (doc.redo.empty())
			return;
		doc.undo.push_back(lsprite::Serialize(doc.sprite));
		const std::string s = std::move(doc.redo.back());
		doc.redo.pop_back();
		Restore(doc, s);
	}

	// ---- Save ----------------------------------------------------------------

	std::string AssetPath(const Document& doc)
	{
		std::error_code ec;
		const sfs::path root(reinterpret_cast<const char8_t*>(g_api->assets_root()));
		const sfs::path rel = sfs::relative(doc.path, root, ec);
		if (ec || rel.empty())
			return {};
		const auto u8 = rel.generic_u8string();
		return std::string(u8.begin(), u8.end());
	}

	void CommitFloating(Document& doc);

	bool Save(Document& doc)
	{
		CommitFloating(doc);
		std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			g_api->message(("Could not write " + Utf8(doc.path)).c_str());
			return false;
		}
		out << lsprite::Serialize(doc.sprite);
		out.close();
		doc.dirty = false;

		// The texture of the sprite (if the game already uses it) : rebuilt.
		const std::string asset = AssetPath(doc);
		if (!asset.empty())
			lynx::ReloadRessourceTexture(asset.c_str());
		return true;
	}

	// ---- Pixels --------------------------------------------------------------

	void Plot(Document& doc, int x, int y, int color)
	{
		lsprite::Sprite& s = doc.sprite;
		// A selection limits the drawing to it.
		if (doc.selection.Valid() && !doc.moving && !doc.selection.Contains(x, y))
			return;
		s.Set(doc.frame, x, y, static_cast<uint8_t>(color));
		if (doc.mirror_x)
		{
			const int mx = s.width - 1 - x;
			if (!doc.selection.Valid() || doc.selection.Contains(mx, y))
				s.Set(doc.frame, mx, y, static_cast<uint8_t>(color));
		}
	}

	template <typename Fn>
	void ForLine(int x0, int y0, int x1, int y1, Fn fn)
	{
		const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
		const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
		int err = dx + dy;
		for (;;)
		{
			fn(x0, y0);
			if (x0 == x1 && y0 == y1)
				break;
			const int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}
	}

	template <typename Fn>
	void ForRect(const Rect& r, bool filled, Fn fn)
	{
		for (int y = r.y0; y <= r.y1; ++y)
			for (int x = r.x0; x <= r.x1; ++x)
				if (filled || x == r.x0 || x == r.x1 || y == r.y0 || y == r.y1)
					fn(x, y);
	}

	void Fill(Document& doc, int x, int y, int color)
	{
		lsprite::Sprite& s = doc.sprite;
		const int target = s.Get(doc.frame, x, y);
		if (target == color || !s.Inside(x, y))
			return;
		std::vector<std::pair<int, int>> stack{ { x, y } };
		while (!stack.empty())
		{
			auto [px, py] = stack.back();
			stack.pop_back();
			if (!s.Inside(px, py) || s.Get(doc.frame, px, py) != target)
				continue;
			if (doc.selection.Valid() && !doc.selection.Contains(px, py))
				continue;
			s.Set(doc.frame, px, py, static_cast<uint8_t>(color));
			stack.push_back({ px + 1, py });
			stack.push_back({ px - 1, py });
			stack.push_back({ px, py + 1 });
			stack.push_back({ px, py - 1 });
		}
	}

	/** Palette index of a RGBA color (added to the palette when missing). */
	int ColorIndex(lsprite::Sprite& s, uint32_t color)
	{
		if (lsprite::A(color) == 0)
			return 0;
		for (size_t i = 1; i < s.palette.size(); ++i)
			if (s.palette[i] == color)
				return static_cast<int>(i);
		if (static_cast<int>(s.palette.size()) >= lsprite::kMaxColors)
			return 1;
		s.palette.push_back(color);
		return static_cast<int>(s.palette.size()) - 1;
	}

	// ---- Selection ------------------------------------------------------------

	void LiftSelection(Document& doc)
	{
		if (!doc.selection.Valid() || doc.moving)
			return;
		PushUndo(doc);
		const Rect r = doc.selection;
		doc.floating.assign(static_cast<size_t>(r.W()) * r.H(), 0);
		for (int y = r.y0; y <= r.y1; ++y)
			for (int x = r.x0; x <= r.x1; ++x)
			{
				doc.floating[static_cast<size_t>(y - r.y0) * r.W() + (x - r.x0)] = doc.sprite.Get(doc.frame, x, y);
				doc.sprite.Set(doc.frame, x, y, 0);
			}
		doc.floating_rect = r;
		doc.move_dx = doc.move_dy = 0;
		doc.moving = true;
	}

	void CommitFloating(Document& doc)
	{
		if (!doc.moving)
			return;
		const Rect& r = doc.floating_rect;
		for (int y = 0; y < r.H(); ++y)
			for (int x = 0; x < r.W(); ++x)
			{
				const uint8_t p = doc.floating[static_cast<size_t>(y) * r.W() + x];
				if (p != 0)
					doc.sprite.Set(doc.frame, r.x0 + doc.move_dx + x, r.y0 + doc.move_dy + y, p);
			}
		doc.selection = { r.x0 + doc.move_dx, r.y0 + doc.move_dy, r.x1 + doc.move_dx, r.y1 + doc.move_dy };
		doc.moving = false;
		doc.floating.clear();
		doc.dirty = true;
	}

	void ClearSelection(Document& doc)
	{
		CommitFloating(doc);
		doc.selection = {};
	}

	void CopySelection(Document& doc)
	{
		CommitFloating(doc);
		Rect r = doc.selection.Valid() ? doc.selection : Rect{ 0, 0, doc.sprite.width - 1, doc.sprite.height - 1 };
		g_clipboard.w = r.W();
		g_clipboard.h = r.H();
		g_clipboard.colors.assign(static_cast<size_t>(r.W()) * r.H(), 0);
		for (int y = r.y0; y <= r.y1; ++y)
			for (int x = r.x0; x <= r.x1; ++x)
			{
				const uint8_t p = doc.sprite.Get(doc.frame, x, y);
				g_clipboard.colors[static_cast<size_t>(y - r.y0) * r.W() + (x - r.x0)] =
					p == 0 ? 0u : doc.sprite.palette[p];
			}
	}

	void Paste(Document& doc)
	{
		if (g_clipboard.w <= 0)
			return;
		ClearSelection(doc);
		PushUndo(doc);
		const int w = std::min(g_clipboard.w, doc.sprite.width);
		const int h = std::min(g_clipboard.h, doc.sprite.height);
		doc.floating.assign(static_cast<size_t>(w) * h, 0);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				doc.floating[static_cast<size_t>(y) * w + x] =
					static_cast<uint8_t>(ColorIndex(doc.sprite, g_clipboard.colors[static_cast<size_t>(y) * g_clipboard.w + x]));
		doc.floating_rect = { 0, 0, w - 1, h - 1 };
		doc.selection = doc.floating_rect;
		doc.move_dx = doc.move_dy = 0;
		doc.moving = true;
		doc.tool = Tool::Select;
	}

	void DeleteSelection(Document& doc)
	{
		if (doc.moving)
		{
			doc.floating.clear();
			doc.moving = false;
			doc.selection = {};
			doc.dirty = true;
			return;
		}
		if (!doc.selection.Valid())
			return;
		PushUndo(doc);
		ForRect(doc.selection, true, [&](int x, int y) { doc.sprite.Set(doc.frame, x, y, 0); });
	}

	// ---- Frames ----------------------------------------------------------------

	bool CanAddFrame(const Document& doc)
	{
		return doc.sprite.FrameCount() < lsprite::kMaxFrames &&
		       doc.sprite.width * (doc.sprite.FrameCount() + 1) <= lsprite::kMaxStripWidth;
	}

	void SetFrame(Document& doc, int frame)
	{
		CommitFloating(doc);
		doc.frame = std::clamp(frame, 0, doc.sprite.FrameCount() - 1);
	}

	// =========================================================================
	// Drawing of a sprite frame with ImDrawList (no texture : the sprite is
	// never turned into an image in the editor either).
	// =========================================================================

	void DrawFrame(ImDrawList* dl, const lsprite::Sprite& s, int frame, ImVec2 origin, float scale, float alpha = 1.f)
	{
		if (frame < 0 || frame >= s.FrameCount())
			return;
		const auto& pixels = s.frames[static_cast<size_t>(frame)];
		for (int y = 0; y < s.height; ++y)
		{
			// Runs of the same color : one rectangle (fewer draw calls).
			int x = 0;
			while (x < s.width)
			{
				const uint8_t p = pixels[static_cast<size_t>(y) * s.width + x];
				int run = 1;
				while (x + run < s.width && pixels[static_cast<size_t>(y) * s.width + x + run] == p)
					++run;
				if (p != 0 && p < s.palette.size())
					dl->AddRectFilled(ImVec2(origin.x + x * scale, origin.y + y * scale),
					                  ImVec2(origin.x + (x + run) * scale, origin.y + (y + 1) * scale),
					                  ToImColor(s.palette[p], alpha));
				x += run;
			}
		}
	}

	void DrawChecker(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell)
	{
		dl->AddRectFilled(a, b, IM_COL32(58, 58, 64, 255));
		cell = std::max(cell, 4.f);
		dl->PushClipRect(a, b, true);
		int row = 0;
		for (float y = a.y; y < b.y; y += cell, ++row)
			for (float x = a.x + ((row & 1) ? cell : 0.f); x < b.x; x += cell * 2.f)
				dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, b.x), std::min(y + cell, b.y)), IM_COL32(74, 74, 80, 255));
		dl->PopClipRect();
	}

	// =========================================================================
	// Panels
	// =========================================================================

	void DrawTools(Document& doc)
	{
		const float w = ImGui::GetContentRegionAvail().x;
		for (int i = 0; i < static_cast<int>(Tool::Count); ++i)
		{
			const Tool t = static_cast<Tool>(i);
			const bool active = doc.tool == t;
			if (active)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (ImGui::Button(ToolShort(t), ImVec2(w, 0.f)))
			{
				if (t != Tool::Select)
					CommitFloating(doc);
				doc.tool = t;
			}
			if (active)
				ImGui::PopStyleColor();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", ToolName(t));
		}

		ImGui::Separator();
		ImGui::Checkbox("Mirror", &doc.mirror_x);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draws on both halves (horizontal symmetry)");
		ImGui::Checkbox("Onion", &doc.onion);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the previous frame under this one");
		ImGui::Checkbox("Grid", &doc.grid);

		// Primary / secondary colors.
		ImGui::Separator();
		const float sw = std::min(w, ImGui::GetFrameHeight() * 1.6f);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		auto swatch = [&](ImVec2 a, int index)
		{
			const ImVec2 b(a.x + sw, a.y + sw);
			DrawChecker(dl, a, b, sw * 0.25f);
			if (index > 0 && index < static_cast<int>(doc.sprite.palette.size()))
				dl->AddRectFilled(a, b, ToImColor(doc.sprite.palette[index]));
			dl->AddRect(a, b, IM_COL32(220, 220, 230, 255));
		};
		swatch(ImVec2(p.x + sw * 0.45f, p.y + sw * 0.45f), doc.secondary);
		swatch(p, doc.primary);
		ImGui::Dummy(ImVec2(sw * 1.5f, sw * 1.5f));
		if (ImGui::IsItemClicked())
			std::swap(doc.primary, doc.secondary);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Left click / right click colors (X or click : swap)");
	}

	void DrawPalette(Document& doc)
	{
		lsprite::Sprite& s = doc.sprite;
		const float cell = ImGui::GetFrameHeight();
		const int per_row = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 2.f) / (cell + 2.f)));
		ImDrawList* dl = ImGui::GetWindowDrawList();

		for (int i = 0; i < static_cast<int>(s.palette.size()); ++i)
		{
			if (i % per_row != 0)
				ImGui::SameLine(0.f, 2.f);
			ImGui::PushID(i);
			const ImVec2 a = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##c", ImVec2(cell, cell));
			const ImVec2 b(a.x + cell, a.y + cell);
			DrawChecker(dl, a, b, cell * 0.25f);
			if (i > 0)
				dl->AddRectFilled(a, b, ToImColor(s.palette[i]));
			else
				dl->AddLine(ImVec2(a.x + 2, b.y - 2), ImVec2(b.x - 2, a.y + 2), IM_COL32(230, 80, 80, 255), 2.f);
			if (i == doc.primary)
				dl->AddRect(a, b, IM_COL32(255, 255, 255, 255), 0.f, 0, 2.f);
			else if (i == doc.secondary)
				dl->AddRect(a, b, IM_COL32(150, 150, 255, 255), 0.f, 0, 2.f);

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				doc.primary = i;
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				doc.secondary = i;
			if (i > 0 && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				doc.editing_color = i;
				ImGui::OpenPopup("##editcolor");
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(i == 0 ? "0 : transparent" : "%d : #%08X\nLeft / right click : primary / secondary\nDouble click : edit", i, s.palette[i]);
			ImGui::PopID();
		}

		if (ImGui::BeginPopup("##editcolor"))
		{
			const int i = doc.editing_color;
			if (i > 0 && i < static_cast<int>(s.palette.size()))
			{
				float c[4] = { lsprite::R(s.palette[i]) / 255.f, lsprite::G(s.palette[i]) / 255.f,
				               lsprite::B(s.palette[i]) / 255.f, lsprite::A(s.palette[i]) / 255.f };
				ImGui::TextDisabled("Color %d : every pixel of this color changes", i);
				if (ImGui::IsWindowAppearing())
					PushUndo(doc);
				if (ImGui::ColorPicker4("##picker", c, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex))
				{
					s.palette[i] = lsprite::Rgba(static_cast<int>(std::lround(c[0] * 255)), static_cast<int>(std::lround(c[1] * 255)),
					                             static_cast<int>(std::lround(c[2] * 255)), static_cast<int>(std::lround(c[3] * 255)));
					doc.dirty = true;
				}
			}
			ImGui::EndPopup();
		}

		ImGui::Spacing();
		ImGui::BeginDisabled(static_cast<int>(s.palette.size()) >= lsprite::kMaxColors);
		if (ImGui::SmallButton("+ Color"))
		{
			PushUndo(doc);
			const uint32_t base = doc.primary > 0 ? s.palette[doc.primary] : 0xffffffffu;
			s.palette.push_back(base);
			doc.primary = static_cast<int>(s.palette.size()) - 1;
			doc.editing_color = doc.primary;
			ImGui::OpenPopup("##editcolor");
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(doc.primary <= 0);
		if (ImGui::SmallButton("- Color"))
		{
			PushUndo(doc);
			s.RemoveColor(doc.primary);
			doc.primary = std::min(doc.primary, static_cast<int>(s.palette.size()) - 1);
			doc.secondary = std::min(doc.secondary, static_cast<int>(s.palette.size()) - 1);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Removes the primary color (its pixels become transparent)");
		ImGui::SameLine();
		if (ImGui::SmallButton("Edit"))
		{
			if (doc.primary > 0)
			{
				doc.editing_color = doc.primary;
				ImGui::OpenPopup("##editcolor");
			}
		}
	}

	void DrawPreview(Document& doc, float size)
	{
		lsprite::Sprite& s = doc.sprite;
		if (doc.preview_playing && s.FrameCount() > 1)
			doc.preview_time += ImGui::GetIO().DeltaTime;
		const float period = 1.f / std::max(0.1f, s.fps);
		int frame = static_cast<int>(doc.preview_time / period);
		if (s.loop)
			frame %= std::max(1, s.FrameCount());
		else
			frame = std::min(frame, s.FrameCount() - 1);

		const float scale = std::max(1.f, std::floor(size / static_cast<float>(std::max(s.width, s.height))));
		const ImVec2 a = ImGui::GetCursorScreenPos();
		const ImVec2 dim(s.width * scale, s.height * scale);
		ImGui::Dummy(dim);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		DrawChecker(dl, a, ImVec2(a.x + dim.x, a.y + dim.y), 6.f);
		DrawFrame(dl, s, frame, a, scale);
		if (ImGui::IsItemClicked())
		{
			doc.preview_playing = !doc.preview_playing;
			doc.preview_time = 0.f;
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Preview (%s) : click to %s", doc.preview_playing ? "playing" : "paused",
			                  doc.preview_playing ? "pause" : "play");
	}

	void DrawFrames(Document& doc)
	{
		lsprite::Sprite& s = doc.sprite;
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Frame %d / %d", doc.frame + 1, s.FrameCount());
		ImGui::SameLine();
		ImGui::BeginDisabled(!CanAddFrame(doc));
		if (ImGui::Button("+ Frame"))
		{
			CommitFloating(doc);
			PushUndo(doc);
			s.frames.insert(s.frames.begin() + doc.frame + 1, std::vector<uint8_t>(static_cast<size_t>(s.width) * s.height, 0));
			++doc.frame;
		}
		ImGui::SameLine();
		if (ImGui::Button("Duplicate"))
		{
			CommitFloating(doc);
			PushUndo(doc);
			s.frames.insert(s.frames.begin() + doc.frame + 1, s.frames[static_cast<size_t>(doc.frame)]);
			++doc.frame;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(s.FrameCount() <= 1);
		if (ImGui::Button("Delete"))
		{
			CommitFloating(doc);
			PushUndo(doc);
			s.frames.erase(s.frames.begin() + doc.frame);
			doc.frame = std::min(doc.frame, s.FrameCount() - 1);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(doc.frame <= 0);
		if (ImGui::ArrowButton("##left", ImGuiDir_Left))
		{
			CommitFloating(doc);
			PushUndo(doc);
			std::swap(s.frames[static_cast<size_t>(doc.frame)], s.frames[static_cast<size_t>(doc.frame - 1)]);
			--doc.frame;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Move the frame left");
		ImGui::SameLine();
		ImGui::BeginDisabled(doc.frame >= s.FrameCount() - 1);
		if (ImGui::ArrowButton("##right", ImGuiDir_Right))
		{
			CommitFloating(doc);
			PushUndo(doc);
			std::swap(s.frames[static_cast<size_t>(doc.frame)], s.frames[static_cast<size_t>(doc.frame + 1)]);
			++doc.frame;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Move the frame right");

		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
		float fps = s.fps;
		const bool fps_changed = ImGui::DragFloat("fps", &fps, 0.1f, 0.1f, 60.f, "%.1f");
		if (ImGui::IsItemActivated())
			PushUndo(doc);   // one undo step for the whole drag
		if (fps_changed)
		{
			s.fps = std::clamp(fps, 0.1f, 60.f);
			doc.dirty = true;
		}
		ImGui::SameLine();
		bool loop = s.loop;
		if (ImGui::Checkbox("Loop", &loop))
		{
			PushUndo(doc);
			s.loop = loop;
		}

		// Thumbnails.
		const float thumb = ImGui::GetFrameHeight() * 2.4f;
		ImGui::BeginChild("##thumbs", ImVec2(0.f, thumb + ImGui::GetStyle().ScrollbarSize + 8.f), false,
		                  ImGuiWindowFlags_HorizontalScrollbar);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float scale = std::max(0.25f, (thumb - 4.f) / static_cast<float>(std::max(s.width, s.height)));
		for (int f = 0; f < s.FrameCount(); ++f)
		{
			if (f > 0)
				ImGui::SameLine(0.f, 4.f);
			ImGui::PushID(f);
			const ImVec2 a = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##t", ImVec2(thumb, thumb));
			const ImVec2 b(a.x + thumb, a.y + thumb);
			DrawChecker(dl, a, b, 4.f);
			DrawFrame(dl, s, f, ImVec2(a.x + 2.f, a.y + 2.f), scale);
			dl->AddRect(a, b, f == doc.frame ? IM_COL32(255, 200, 80, 255) : IM_COL32(90, 90, 100, 255), 0.f, 0,
			            f == doc.frame ? 2.f : 1.f);
			char n[8];
			std::snprintf(n, sizeof(n), "%d", f + 1);
			dl->AddText(ImVec2(a.x + 3.f, a.y + 1.f), IM_COL32(255, 255, 255, 200), n);
			if (ImGui::IsItemClicked())
				SetFrame(doc, f);
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	// =========================================================================
	// Canvas
	// =========================================================================

	void DrawCanvas(Document& doc, bool shortcuts)
	{
		lsprite::Sprite& s = doc.sprite;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 origin_screen = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##canvas", ImVec2(std::max(avail.x, 32.f), std::max(avail.y, 32.f)),
		                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
		                       ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		ImGuiIO& io = ImGui::GetIO();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 cmin = origin_screen;
		const ImVec2 cmax(origin_screen.x + avail.x, origin_screen.y + avail.y);

		// Zoom to fit the first time.
		if (doc.zoom <= 0.f)
		{
			doc.zoom = std::max(1.f, std::floor(std::min(avail.x / s.width, avail.y / s.height) * 0.85f));
			doc.pan = ImVec2((avail.x - s.width * doc.zoom) * 0.5f, (avail.y - s.height * doc.zoom) * 0.5f);
		}

		// Zoom at the mouse (wheel), pan (middle button, or Space + drag).
		if (hovered && io.MouseWheel != 0.f)
		{
			const float old = doc.zoom;
			doc.zoom = std::clamp(io.MouseWheel > 0.f ? old * 1.25f : old / 1.25f, 1.f, 96.f);
			const ImVec2 m(io.MousePos.x - origin_screen.x, io.MousePos.y - origin_screen.y);
			doc.pan.x = m.x - (m.x - doc.pan.x) * doc.zoom / old;
			doc.pan.y = m.y - (m.y - doc.pan.y) * doc.zoom / old;
		}
		const bool space = shortcuts && ImGui::IsKeyDown(ImGuiKey_Space);
		if ((active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)) ||
		    (active && space && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)))
		{
			doc.pan.x += io.MouseDelta.x;
			doc.pan.y += io.MouseDelta.y;
		}

		const float z = doc.zoom;
		const ImVec2 o(std::floor(origin_screen.x + doc.pan.x), std::floor(origin_screen.y + doc.pan.y));
		const ImVec2 end(o.x + s.width * z, o.y + s.height * z);

		dl->PushClipRect(cmin, cmax, true);
		dl->AddRectFilled(cmin, cmax, IM_COL32(32, 32, 36, 255));
		DrawChecker(dl, o, end, std::max(z, 8.f));

		// Onion skin : the previous frame, faded.
		if (doc.onion && s.FrameCount() > 1)
		{
			const int previous = doc.frame > 0 ? doc.frame - 1 : (s.loop ? s.FrameCount() - 1 : -1);
			if (previous >= 0 && previous != doc.frame)
				DrawFrame(dl, s, previous, o, z, 0.25f);
		}
		DrawFrame(dl, s, doc.frame, o, z);

		// Floating selection (being moved).
		if (doc.moving)
		{
			const Rect& r = doc.floating_rect;
			for (int y = 0; y < r.H(); ++y)
				for (int x = 0; x < r.W(); ++x)
				{
					const uint8_t p = doc.floating[static_cast<size_t>(y) * r.W() + x];
					if (p == 0 || p >= s.palette.size())
						continue;
					const float px = o.x + (r.x0 + doc.move_dx + x) * z;
					const float py = o.y + (r.y0 + doc.move_dy + y) * z;
					dl->AddRectFilled(ImVec2(px, py), ImVec2(px + z, py + z), ToImColor(s.palette[p]));
				}
		}

		// Grid
		if (doc.grid && z >= 6.f)
		{
			const ImU32 line = IM_COL32(0, 0, 0, 50);
			for (int x = 0; x <= s.width; ++x)
				dl->AddLine(ImVec2(o.x + x * z, o.y), ImVec2(o.x + x * z, end.y), line);
			for (int y = 0; y <= s.height; ++y)
				dl->AddLine(ImVec2(o.x, o.y + y * z), ImVec2(end.x, o.y + y * z), line);
		}
		if (doc.mirror_x)
			dl->AddLine(ImVec2(o.x + s.width * z * 0.5f, o.y - 6.f), ImVec2(o.x + s.width * z * 0.5f, end.y + 6.f),
			            IM_COL32(80, 200, 255, 160), 1.5f);
		dl->AddRect(ImVec2(o.x - 1, o.y - 1), ImVec2(end.x + 1, end.y + 1), IM_COL32(120, 120, 130, 255));

		// Mouse -> pixel
		const int mx = static_cast<int>(std::floor((io.MousePos.x - o.x) / z));
		const int my = static_cast<int>(std::floor((io.MousePos.y - o.y) / z));
		const bool inside = s.Inside(mx, my);

		const bool left_click = active && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !space;
		const bool right_click = active && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
		const bool left_down = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !space;
		const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		const bool released = doc.drawing && !left_down && !right_down;

		Tool tool = doc.tool;
		if (io.KeyAlt && tool != Tool::Select)
			tool = Tool::Picker;

		auto stroke_color = [&](bool right) { return tool == Tool::Eraser ? 0 : (right ? doc.secondary : doc.primary); };

		if ((left_click || right_click) && hovered)
		{
			const bool right = right_click;
			switch (tool)
			{
			case Tool::Picker:
				if (inside)
					(right ? doc.secondary : doc.primary) = s.Get(doc.frame, mx, my);
				break;
			case Tool::Fill:
				if (inside)
				{
					PushUndo(doc);
					Fill(doc, mx, my, right ? doc.secondary : doc.primary);
				}
				break;
			case Tool::Select:
				if (right)
				{
					ClearSelection(doc);
					break;
				}
				if (doc.selection.Contains(mx, my) || (doc.moving && Rect{ doc.floating_rect.x0 + doc.move_dx,
				        doc.floating_rect.y0 + doc.move_dy, doc.floating_rect.x1 + doc.move_dx,
				        doc.floating_rect.y1 + doc.move_dy }.Contains(mx, my)))
				{
					LiftSelection(doc);
					doc.grab_x = mx - doc.move_dx;
					doc.grab_y = my - doc.move_dy;
					doc.drawing = true;
					doc.start_x = -1;   // moving
				}
				else
				{
					ClearSelection(doc);
					doc.drawing = true;
					doc.start_x = std::clamp(mx, 0, s.width - 1);
					doc.start_y = std::clamp(my, 0, s.height - 1);
				}
				break;
			default:
				CommitFloating(doc);
				PushUndo(doc);
				doc.drawing = true;
				doc.draw_color = stroke_color(right);
				doc.start_x = doc.last_x = mx;
				doc.start_y = doc.last_y = my;
				if (tool == Tool::Pencil || tool == Tool::Eraser)
					Plot(doc, mx, my, doc.draw_color);
				break;
			}
		}

		if (doc.drawing && (left_down || right_down))
		{
			switch (tool)
			{
			case Tool::Pencil:
			case Tool::Eraser:
				if (mx != doc.last_x || my != doc.last_y)
				{
					ForLine(doc.last_x, doc.last_y, mx, my, [&](int x, int y) { Plot(doc, x, y, doc.draw_color); });
					doc.last_x = mx;
					doc.last_y = my;
				}
				break;
			case Tool::Select:
				if (doc.start_x < 0)
				{
					doc.move_dx = mx - doc.grab_x;
					doc.move_dy = my - doc.grab_y;
				}
				else
					doc.selection = Normalized(doc.start_x, doc.start_y,
					                           std::clamp(mx, 0, s.width - 1), std::clamp(my, 0, s.height - 1));
				break;
			default:
				break;
			}
		}

		// Shape preview (line / rectangles) : drawn over the canvas until released.
		const bool shape = tool == Tool::Line || tool == Tool::Rect || tool == Tool::FilledRect;
		if (doc.drawing && shape)
		{
			const int color = doc.draw_color;
			auto preview = [&](int x, int y)
			{
				if (!s.Inside(x, y))
					return;
				const ImVec2 a(o.x + x * z, o.y + y * z);
				if (color == 0)
					dl->AddRect(a, ImVec2(a.x + z, a.y + z), IM_COL32(255, 80, 80, 200));
				else
					dl->AddRectFilled(a, ImVec2(a.x + z, a.y + z), ToImColor(s.palette[static_cast<size_t>(color)]));
			};
			if (tool == Tool::Line)
				ForLine(doc.start_x, doc.start_y, mx, my, preview);
			else
				ForRect(Normalized(doc.start_x, doc.start_y, mx, my), tool == Tool::FilledRect, preview);
		}

		if (released)
		{
			if (tool == Tool::Line)
				ForLine(doc.start_x, doc.start_y, mx, my, [&](int x, int y) { Plot(doc, x, y, doc.draw_color); });
			else if (tool == Tool::Rect || tool == Tool::FilledRect)
				ForRect(Normalized(doc.start_x, doc.start_y, mx, my), tool == Tool::FilledRect,
				        [&](int x, int y) { Plot(doc, x, y, doc.draw_color); });
			doc.drawing = false;
		}

		// Selection rectangle (marching ants : two colors).
		Rect shown = doc.selection;
		if (doc.moving)
			shown = { doc.floating_rect.x0 + doc.move_dx, doc.floating_rect.y0 + doc.move_dy,
			          doc.floating_rect.x1 + doc.move_dx, doc.floating_rect.y1 + doc.move_dy };
		if (shown.Valid())
		{
			const ImVec2 a(o.x + shown.x0 * z, o.y + shown.y0 * z);
			const ImVec2 b(o.x + (shown.x1 + 1) * z, o.y + (shown.y1 + 1) * z);
			dl->AddRect(a, b, IM_COL32(0, 0, 0, 255), 0.f, 0, 3.f);
			const bool phase = std::fmod(ImGui::GetTime(), 0.8) < 0.4;
			dl->AddRect(a, b, phase ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 200, 60, 255), 0.f, 0, 1.5f);
		}

		// Hovered pixel
		if (hovered && inside && !space)
		{
			const ImVec2 a(o.x + mx * z, o.y + my * z);
			dl->AddRect(a, ImVec2(a.x + z, a.y + z), IM_COL32(255, 255, 255, 180));
		}
		dl->PopClipRect();

		// Status line
		char status[128];
		std::snprintf(status, sizeof(status), "%d x %d   zoom %.0fx   %s", s.width, s.height, z,
		              inside ? (std::to_string(mx) + ", " + std::to_string(my)).c_str() : "");
		dl->AddText(ImVec2(cmin.x + 6.f, cmax.y - ImGui::GetTextLineHeight() - 4.f), IM_COL32(200, 200, 210, 200), status);
	}

	// =========================================================================
	// Window
	// =========================================================================

	void Shortcuts(Document& doc)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.KeyCtrl)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Z, true)) { CommitFloating(doc); if (io.KeyShift) Redo(doc); else Undo(doc); }
			else if (ImGui::IsKeyPressed(ImGuiKey_Y, true)) { CommitFloating(doc); Redo(doc); }
			else if (ImGui::IsKeyPressed(ImGuiKey_A, false))
			{
				CommitFloating(doc);
				doc.selection = { 0, 0, doc.sprite.width - 1, doc.sprite.height - 1 };
				doc.tool = Tool::Select;
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelection(doc);
			else if (ImGui::IsKeyPressed(ImGuiKey_X, false)) { CopySelection(doc); DeleteSelection(doc); }
			else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) Paste(doc);
			return;
		}
		struct Key { ImGuiKey key; Tool tool; };
		const Key keys[] = {
			{ ImGuiKey_B, Tool::Pencil }, { ImGuiKey_E, Tool::Eraser }, { ImGuiKey_G, Tool::Fill },
			{ ImGuiKey_L, Tool::Line }, { ImGuiKey_R, Tool::Rect }, { ImGuiKey_U, Tool::FilledRect },
			{ ImGuiKey_I, Tool::Picker }, { ImGuiKey_M, Tool::Select },
		};
		for (const Key& k : keys)
			if (ImGui::IsKeyPressed(k.key, false))
			{
				if (k.tool != Tool::Select)
					CommitFloating(doc);
				doc.tool = k.tool;
			}
		if (ImGui::IsKeyPressed(ImGuiKey_X, false))
			std::swap(doc.primary, doc.secondary);
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
			DeleteSelection(doc);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			ClearSelection(doc);
		if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true))
			SetFrame(doc, doc.frame - 1);
		if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true))
			SetFrame(doc, doc.frame + 1);
	}

	void DrawDocument(bool* open, void* user)
	{
		Document& doc = *static_cast<Document*>(user);
		ImGui::SetNextWindowSize(ImVec2(1100.f, 720.f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(doc.title.c_str(), open, doc.dirty ? ImGuiWindowFlags_UnsavedDocument : 0))
		{
			ImGui::End();
			return;
		}
		if (!doc.error.empty())
		{
			ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "%s", doc.error.c_str());
			ImGui::End();
			return;
		}

		ImGuiIO& io = ImGui::GetIO();
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const bool shortcuts = focused && !io.WantTextInput;
		if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
			Save(doc);
		if (shortcuts)
			Shortcuts(doc);

		// ---- Top bar --------------------------------------------------------
		if (ImGui::Button("Save"))
			Save(doc);
		ImGui::SameLine();
		ImGui::BeginDisabled(doc.undo.empty());
		if (ImGui::Button("Undo")) { CommitFloating(doc); Undo(doc); }
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(doc.redo.empty());
		if (ImGui::Button("Redo")) { CommitFloating(doc); Redo(doc); }
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Canvas size..."))
		{
			doc.new_w = doc.sprite.width;
			doc.new_h = doc.sprite.height;
			ImGui::OpenPopup("##resize");
		}
		ImGui::SameLine();
		if (ImGui::Button("Fit"))
			doc.zoom = 0.f;
		ImGui::SameLine();
		if (ImGui::Button("Flip H"))
		{
			CommitFloating(doc);
			PushUndo(doc);
			auto& px = doc.sprite.frames[static_cast<size_t>(doc.frame)];
			for (int y = 0; y < doc.sprite.height; ++y)
				std::reverse(px.begin() + static_cast<long>(y) * doc.sprite.width,
				             px.begin() + static_cast<long>(y + 1) * doc.sprite.width);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Flips the current frame horizontally");
		ImGui::SameLine();
		ImGui::TextDisabled("%s%s", Utf8(doc.path.filename()).c_str(), doc.dirty ? " (not saved)" : "");

		if (ImGui::BeginPopup("##resize"))
		{
			ImGui::TextUnformatted("Canvas size (pixels, anchored top-left)");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
			ImGui::InputInt("Width", &doc.new_w);
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
			ImGui::InputInt("Height", &doc.new_h);
			doc.new_w = std::clamp(doc.new_w, 1, lsprite::kMaxSize);
			doc.new_h = std::clamp(doc.new_h, 1, lsprite::kMaxSize);
			for (int preset : { 8, 16, 24, 32, 48, 64 })
			{
				char label[16];
				std::snprintf(label, sizeof(label), "%d", preset);
				if (ImGui::SmallButton(label))
					doc.new_w = doc.new_h = preset;
				ImGui::SameLine();
			}
			ImGui::NewLine();
			const bool fits = doc.new_w * doc.sprite.FrameCount() <= lsprite::kMaxStripWidth;
			if (!fits)
				ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "Too wide for %d frames", doc.sprite.FrameCount());
			ImGui::BeginDisabled(!fits);
			if (ImGui::Button("Apply"))
			{
				CommitFloating(doc);
				PushUndo(doc);
				doc.sprite.Resize(doc.new_w, doc.new_h);
				doc.selection = {};
				doc.zoom = 0.f;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

		// ---- Layout : tools | canvas | palette + preview --------------------
		const float tools_w = ImGui::GetFontSize() * 4.8f;
		const float side_w = ImGui::GetFontSize() * 13.f;
		const float frames_h = ImGui::GetFrameHeight() * 4.2f;
		const float body_h = ImGui::GetContentRegionAvail().y - frames_h - ImGui::GetStyle().ItemSpacing.y;

		ImGui::BeginChild("##tools", ImVec2(tools_w, body_h), true);
		DrawTools(doc);
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##canvasarea", ImVec2(ImGui::GetContentRegionAvail().x - side_w - ImGui::GetStyle().ItemSpacing.x, body_h),
		                  true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		DrawCanvas(doc, shortcuts);
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##side", ImVec2(0.f, body_h), true);
		ImGui::SeparatorText("Palette");
		DrawPalette(doc);
		ImGui::SeparatorText("Preview");
		DrawPreview(doc, ImGui::GetContentRegionAvail().x);
		ImGui::EndChild();

		ImGui::BeginChild("##frames", ImVec2(0.f, 0.f), true);
		DrawFrames(doc);
		ImGui::EndChild();

		ImGui::End();
	}

	bool OpenSprite(const char* path_utf8, void*)
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
		doc.title = "Pixel Sprite - " + Utf8(path.filename()) + "##" + path_utf8;

		const std::string text = ReadFile(path);
		std::string error;
		if (text.empty())
			doc.sprite = lsprite::MakeDefault();
		else if (!lsprite::Parse(text, doc.sprite, error))
			doc.error = "Not a .lsprite file : " + error;

		g_api->add_window("PixelSprite", doc.title.c_str(), DrawDocument, &doc, true);
		g_api->open_window(doc.title.c_str());
		return true;
	}
}


LYNX_EDITOR_PLUGIN_STARTUP(api)
{
	LYNX_EDITOR_PLUGIN_INIT(api);
	g_api = api;

	api->add_file_editor("PixelSprite", ".lsprite", OpenSprite, nullptr);

	static const std::string new_file = lsprite::Serialize(lsprite::MakeDefault(16, 16));
	api->add_new_file("PixelSprite", "Pixel sprite (.lsprite)", "NewSprite", ".lsprite", new_file.c_str());
}

LYNX_EDITOR_PLUGIN_SHUTDOWN()
{
	g_docs.clear();
}
