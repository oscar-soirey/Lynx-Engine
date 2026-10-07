#include "LSprite.h"

#include <json/json.hpp>

#include <algorithm>
#include <cstdio>

namespace lsprite
{
	namespace
	{
		int HexDigit(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		bool ParseColor(const std::string& text, uint32_t& out)
		{
			std::string hex = text;
			if (!hex.empty() && hex[0] == '#')
				hex.erase(0, 1);
			if (hex.size() == 6)
				hex += "ff";
			if (hex.size() != 8)
				return false;
			uint32_t value = 0;
			for (char c : hex)
			{
				const int d = HexDigit(c);
				if (d < 0)
					return false;
				value = (value << 4) | static_cast<uint32_t>(d);
			}
			out = value;
			return true;
		}

		std::string ColorText(uint32_t c)
		{
			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "%08x", c);
			return buffer;
		}
	}

	uint8_t Sprite::Get(int frame, int x, int y) const
	{
		if (frame < 0 || frame >= FrameCount() || !Inside(x, y))
			return 0;
		return frames[static_cast<size_t>(frame)][static_cast<size_t>(y) * width + x];
	}

	void Sprite::Set(int frame, int x, int y, uint8_t index)
	{
		if (frame < 0 || frame >= FrameCount() || !Inside(x, y))
			return;
		frames[static_cast<size_t>(frame)][static_cast<size_t>(y) * width + x] = index;
	}

	void Sprite::Resize(int new_width, int new_height)
	{
		new_width = std::clamp(new_width, 1, kMaxSize);
		new_height = std::clamp(new_height, 1, kMaxSize);
		for (auto& frame : frames)
		{
			std::vector<uint8_t> resized(static_cast<size_t>(new_width) * new_height, 0);
			for (int y = 0; y < std::min(height, new_height); ++y)
				for (int x = 0; x < std::min(width, new_width); ++x)
					resized[static_cast<size_t>(y) * new_width + x] = frame[static_cast<size_t>(y) * width + x];
			frame.swap(resized);
		}
		width = new_width;
		height = new_height;
	}

	void Sprite::RemoveColor(int index)
	{
		if (index <= 0 || index >= static_cast<int>(palette.size()))
			return;
		palette.erase(palette.begin() + index);
		for (auto& frame : frames)
			for (uint8_t& p : frame)
			{
				if (p == index)
					p = 0;
				else if (p > index)
					--p;
			}
	}

	std::vector<uint32_t> DefaultPalette()
	{
		// Transparent + a 16 colors palette (PICO-8 like) + a few greys.
		return {
			0x00000000u,
			0x000000ffu, 0x1d2b53ffu, 0x7e2553ffu, 0x008751ffu,
			0xab5236ffu, 0x5f574fffu, 0xc2c3c7ffu, 0xfff1e8ffu,
			0xff004dffu, 0xffa300ffu, 0xffec27ffu, 0x00e436ffu,
			0x29adffffu, 0x83769cffu, 0xff77a8ffu, 0xffccaaffu,
		};
	}

	Sprite MakeDefault(int width, int height)
	{
		Sprite s;
		s.width = std::clamp(width, 1, kMaxSize);
		s.height = std::clamp(height, 1, kMaxSize);
		s.palette = DefaultPalette();
		s.frames.assign(1, std::vector<uint8_t>(static_cast<size_t>(s.width) * s.height, 0));
		return s;
	}

	bool Parse(const std::string& text, Sprite& out, std::string& error)
	{
		const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
		if (!j.is_object())
		{
			error = "not JSON";
			return false;
		}
		Sprite s;
		s.width = std::clamp(j.value("width", 16), 1, kMaxSize);
		s.height = std::clamp(j.value("height", 16), 1, kMaxSize);
		s.fps = std::clamp(j.value("fps", 8.f), 0.1f, 120.f);
		s.loop = j.value("loop", true);

		if (j.contains("palette") && j["palette"].is_array())
		{
			for (const auto& c : j["palette"])
			{
				uint32_t color = 0;
				if (c.is_string() && ParseColor(c.get<std::string>(), color))
					s.palette.push_back(color);
				else
					s.palette.push_back(0);
				if (static_cast<int>(s.palette.size()) >= kMaxColors)
					break;
			}
		}
		if (s.palette.empty())
			s.palette = DefaultPalette();
		s.palette[0] = 0;   // index 0 : always transparent

		const size_t count = static_cast<size_t>(s.width) * s.height;
		if (j.contains("frames") && j["frames"].is_array())
		{
			for (const auto& f : j["frames"])
			{
				std::vector<uint8_t> pixels(count, 0);
				if (f.is_string())
				{
					const std::string& hex = f.get_ref<const std::string&>();
					for (size_t i = 0; i < count && i * 2 + 1 < hex.size(); ++i)
					{
						const int hi = HexDigit(hex[i * 2]);
						const int lo = HexDigit(hex[i * 2 + 1]);
						if (hi < 0 || lo < 0)
							continue;
						const int index = hi * 16 + lo;
						pixels[i] = index < static_cast<int>(s.palette.size()) ? static_cast<uint8_t>(index) : 0;
					}
				}
				s.frames.push_back(std::move(pixels));
				if (s.FrameCount() >= kMaxFrames)
					break;
			}
		}
		if (s.frames.empty())
			s.frames.assign(1, std::vector<uint8_t>(count, 0));

		out = std::move(s);
		return true;
	}

	std::string Serialize(const Sprite& s)
	{
		static const char* kHex = "0123456789abcdef";
		std::string out;
		out += "{\n  \"format\": \"lsprite\",\n  \"version\": 1,\n";
		out += "  \"width\": " + std::to_string(s.width) + ",\n";
		out += "  \"height\": " + std::to_string(s.height) + ",\n";
		char fps[32];
		std::snprintf(fps, sizeof(fps), "%g", s.fps);
		out += std::string("  \"fps\": ") + fps + ",\n";
		out += std::string("  \"loop\": ") + (s.loop ? "true" : "false") + ",\n";
		out += "  \"palette\": [";
		for (size_t i = 0; i < s.palette.size(); ++i)
			out += (i ? ", \"" : "\"") + ColorText(s.palette[i]) + "\"";
		out += "],\n  \"frames\": [\n";
		for (size_t f = 0; f < s.frames.size(); ++f)
		{
			out += "    \"";
			for (uint8_t p : s.frames[f])
			{
				out += kHex[p >> 4];
				out += kHex[p & 15];
			}
			out += f + 1 < s.frames.size() ? "\",\n" : "\"\n";
		}
		out += "  ]\n}\n";
		return out;
	}

	std::vector<uint8_t> BuildStrip(const Sprite& s)
	{
		const int frames = std::max(1, s.FrameCount());
		const int strip_w = s.width * frames;
		std::vector<uint8_t> rgba(static_cast<size_t>(strip_w) * s.height * 4, 0);
		for (int f = 0; f < s.FrameCount(); ++f)
			for (int y = 0; y < s.height; ++y)
				for (int x = 0; x < s.width; ++x)
				{
					const uint8_t index = s.frames[static_cast<size_t>(f)][static_cast<size_t>(y) * s.width + x];
					const uint32_t c = index < s.palette.size() ? s.palette[index] : 0u;
					uint8_t* p = &rgba[(static_cast<size_t>(y) * strip_w + static_cast<size_t>(f) * s.width + x) * 4];
					p[0] = R(c); p[1] = G(c); p[2] = B(c); p[3] = index == 0 ? 0 : A(c);
				}
		return rgba;
	}

	std::vector<uint8_t> BuildTga(const Sprite& s)
	{
		const int frames = std::max(1, s.FrameCount());
		const int w = s.width * frames;
		const int h = s.height;
		if (w > kMaxStripWidth || h > kMaxSize)
			return {};
		const std::vector<uint8_t> rgba = BuildStrip(s);

		std::vector<uint8_t> tga(18 + rgba.size(), 0);
		tga[2] = 2;                                   // uncompressed true color
		tga[12] = static_cast<uint8_t>(w & 255);
		tga[13] = static_cast<uint8_t>(w >> 8);
		tga[14] = static_cast<uint8_t>(h & 255);
		tga[15] = static_cast<uint8_t>(h >> 8);
		tga[16] = 32;                                 // bits per pixel
		tga[17] = 0x28;                               // 8 alpha bits, top-left origin
		for (size_t i = 0; i < rgba.size(); i += 4)
		{
			tga[18 + i + 0] = rgba[i + 2];   // B
			tga[18 + i + 1] = rgba[i + 1];   // G
			tga[18 + i + 2] = rgba[i + 0];   // R
			tga[18 + i + 3] = rgba[i + 3];   // A
		}
		return tga;
	}
}
