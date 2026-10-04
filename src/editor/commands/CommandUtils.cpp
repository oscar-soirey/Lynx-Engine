#include "CommandUtils.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace lynx::editor::commands
{
	// =========================================================================
	// Base64
	// =========================================================================

	namespace
	{
		const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	}

	std::string Base64Encode(const uint8_t* data, size_t size)
	{
		std::string out;
		out.reserve((size + 2) / 3 * 4);

		size_t i = 0;

		for (; i + 2 < size; i += 3)
		{
			const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
			out += kBase64[(v >> 18) & 63];
			out += kBase64[(v >> 12) & 63];
			out += kBase64[(v >> 6) & 63];
			out += kBase64[v & 63];
		}

		if (i < size)
		{
			uint32_t v = uint32_t(data[i]) << 16;

			if (i + 1 < size)
				v |= uint32_t(data[i + 1]) << 8;

			out += kBase64[(v >> 18) & 63];
			out += kBase64[(v >> 12) & 63];
			out += (i + 1 < size) ? kBase64[(v >> 6) & 63] : '=';
			out += '=';
		}

		return out;
	}

	std::string Base64Encode(const std::string& data)
	{
		return Base64Encode(reinterpret_cast<const uint8_t*>(data.data()), data.size());
	}

	bool Base64Decode(const std::string& text, std::string& out)
	{
		static const std::array<int, 256> table = []()
		{
			std::array<int, 256> t{};
			t.fill(-1);

			for (int i = 0; i < 64; ++i)
				t[static_cast<uint8_t>(kBase64[i])] = i;

			// URL-safe variant too.
			t[static_cast<uint8_t>('-')] = 62;
			t[static_cast<uint8_t>('_')] = 63;
			return t;
		}();

		out.clear();
		out.reserve(text.size() / 4 * 3);

		uint32_t buffer = 0;
		int bits = 0;

		for (char c : text)
		{
			if (c == '=' )
				break;

			if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
				continue;

			const int value = table[static_cast<uint8_t>(c)];

			if (value < 0)
				return false;

			buffer = (buffer << 6) | static_cast<uint32_t>(value);
			bits += 6;

			if (bits >= 8)
			{
				bits -= 8;
				out += static_cast<char>((buffer >> bits) & 0xFF);
			}
		}

		return true;
	}


	// =========================================================================
	// Deflate (fixed Huffman codes + LZ77) : small, and good enough for
	// screenshots of a 2D game (large flat areas).
	// =========================================================================

	namespace
	{
		class BitWriter
		{
		public:
			std::string bytes;

			void Bits(uint32_t value, int count)   // LSB first
			{
				buffer_ |= value << used_;
				used_ += count;

				while (used_ >= 8)
				{
					bytes += static_cast<char>(buffer_ & 0xFF);
					buffer_ >>= 8;
					used_ -= 8;
				}
			}

			// Huffman codes are written MSB first.
			void Code(uint32_t code, int length)
			{
				uint32_t reversed = 0;

				for (int i = 0; i < length; ++i)
					reversed |= ((code >> i) & 1u) << (length - 1 - i);

				Bits(reversed, length);
			}

			void Flush()
			{
				if (used_ > 0)
					bytes += static_cast<char>(buffer_ & 0xFF);

				buffer_ = 0;
				used_ = 0;
			}

		private:
			uint32_t buffer_ = 0;
			int used_ = 0;
		};

		void WriteLiteral(BitWriter& w, int symbol)
		{
			if (symbol < 144)
				w.Code(0x30 + symbol, 8);
			else if (symbol < 256)
				w.Code(0x190 + (symbol - 144), 9);
			else if (symbol < 280)
				w.Code(symbol - 256, 7);
			else
				w.Code(0xC0 + (symbol - 280), 8);
		}

		const int kLengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		                              35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
		const int kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
		                               3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
		const int kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		                            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
		                            8193, 12289, 16385, 24577 };
		const int kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
		                             7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

		void WriteMatch(BitWriter& w, int length, int distance)
		{
			int l = 28;

			while (kLengthBase[l] > length)
				--l;

			WriteLiteral(w, 257 + l);
			w.Bits(static_cast<uint32_t>(length - kLengthBase[l]), kLengthExtra[l]);

			int d = 29;

			while (kDistBase[d] > distance)
				--d;

			w.Code(static_cast<uint32_t>(d), 5);
			w.Bits(static_cast<uint32_t>(distance - kDistBase[d]), kDistExtra[d]);
		}

		uint32_t Adler32(const uint8_t* data, size_t size)
		{
			uint32_t a = 1;
			uint32_t b = 0;

			while (size > 0)
			{
				const size_t chunk = std::min<size_t>(size, 5552);

				for (size_t i = 0; i < chunk; ++i)
				{
					a += data[i];
					b += a;
				}

				a %= 65521u;
				b %= 65521u;
				data += chunk;
				size -= chunk;
			}

			return (b << 16) | a;
		}

		std::string ZlibCompress(const uint8_t* data, size_t size)
		{
			BitWriter w;
			w.bytes += static_cast<char>(0x78);
			w.bytes += static_cast<char>(0x01);

			// One fixed-Huffman block.
			w.Bits(1, 1);   // final
			w.Bits(1, 2);   // fixed codes

			constexpr int kHashBits = 15;
			constexpr int kWindow = 32768;
			constexpr int kMaxChain = 32;

			std::vector<int> head(1u << kHashBits, -1);
			std::vector<int> previous(size, -1);

			const auto hash = [&](size_t i)
			{
				const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
				return static_cast<int>((v * 2654435761u) >> (32 - kHashBits));
			};

			const auto insert = [&](size_t i)
			{
				if (i + 2 >= size)
					return;

				const int h = hash(i);
				previous[i] = head[h];
				head[h] = static_cast<int>(i);
			};

			size_t i = 0;

			while (i < size)
			{
				int best_length = 0;
				int best_distance = 0;

				if (i + 2 < size)
				{
					int candidate = head[hash(i)];
					int chain = 0;
					const size_t max_length = std::min<size_t>(258, size - i);

					while (candidate >= 0 && chain++ < kMaxChain &&
					       static_cast<int>(i) - candidate <= kWindow)
					{
						size_t length = 0;

						while (length < max_length && data[candidate + length] == data[i + length])
							++length;

						if (static_cast<int>(length) > best_length)
						{
							best_length = static_cast<int>(length);
							best_distance = static_cast<int>(i) - candidate;

							if (length == max_length)
								break;
						}

						candidate = previous[candidate];
					}
				}

				if (best_length >= 3)
				{
					WriteMatch(w, best_length, best_distance);

					for (int k = 0; k < best_length; ++k)
						insert(i + static_cast<size_t>(k));

					i += static_cast<size_t>(best_length);
				}
				else
				{
					WriteLiteral(w, data[i]);
					insert(i);
					++i;
				}
			}

			WriteLiteral(w, 256);   // end of block
			w.Flush();

			const uint32_t adler = Adler32(data, size);
			w.bytes += static_cast<char>((adler >> 24) & 0xFF);
			w.bytes += static_cast<char>((adler >> 16) & 0xFF);
			w.bytes += static_cast<char>((adler >> 8) & 0xFF);
			w.bytes += static_cast<char>(adler & 0xFF);

			return w.bytes;
		}

		uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0)
		{
			static const std::array<uint32_t, 256> table = []()
			{
				std::array<uint32_t, 256> t{};

				for (uint32_t n = 0; n < 256; ++n)
				{
					uint32_t c = n;

					for (int k = 0; k < 8; ++k)
						c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;

					t[n] = c;
				}

				return t;
			}();

			crc = ~crc;

			for (size_t i = 0; i < size; ++i)
				crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);

			return ~crc;
		}

		void Put32(std::string& out, uint32_t v)
		{
			out += static_cast<char>((v >> 24) & 0xFF);
			out += static_cast<char>((v >> 16) & 0xFF);
			out += static_cast<char>((v >> 8) & 0xFF);
			out += static_cast<char>(v & 0xFF);
		}

		void Chunk(std::string& png, const char* type, const std::string& data)
		{
			Put32(png, static_cast<uint32_t>(data.size()));

			std::string body(type, 4);
			body += data;

			png += body;
			Put32(png, Crc32(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
		}
	}


	std::string EncodePng(const uint8_t* pixels, int width, int height, int channels)
	{
		const size_t row = static_cast<size_t>(width) * static_cast<size_t>(channels);

		// Filter "Sub" on every row : flat areas become zeros.
		std::vector<uint8_t> raw;
		raw.reserve((row + 1) * static_cast<size_t>(height));

		for (int y = 0; y < height; ++y)
		{
			const uint8_t* line = pixels + row * static_cast<size_t>(y);
			raw.push_back(1);

			for (size_t x = 0; x < row; ++x)
			{
				const uint8_t left = x >= static_cast<size_t>(channels) ? line[x - channels] : 0;
				raw.push_back(static_cast<uint8_t>(line[x] - left));
			}
		}

		std::string png("\x89PNG\r\n\x1a\n", 8);

		std::string header;
		Put32(header, static_cast<uint32_t>(width));
		Put32(header, static_cast<uint32_t>(height));
		header += static_cast<char>(8);                          // bit depth
		header += static_cast<char>(channels == 4 ? 6 : 2);      // RGBA / RGB
		header += '\0';                                          // compression
		header += '\0';                                          // filter
		header += '\0';                                          // interlace

		Chunk(png, "IHDR", header);
		Chunk(png, "IDAT", ZlibCompress(raw.data(), raw.size()));
		Chunk(png, "IEND", std::string());

		return png;
	}


	void Downscale(std::vector<uint8_t>& pixels, int& width, int& height, int channels, int max_size)
	{
		if (max_size <= 0 || (width <= max_size && height <= max_size))
			return;

		const double scale = static_cast<double>(max_size) / std::max(width, height);
		const int new_width = std::max(1, static_cast<int>(width * scale));
		const int new_height = std::max(1, static_cast<int>(height * scale));

		std::vector<uint8_t> out(static_cast<size_t>(new_width) * new_height * channels);

		for (int y = 0; y < new_height; ++y)
		{
			const int y0 = y * height / new_height;
			const int y1 = std::max(y0 + 1, (y + 1) * height / new_height);

			for (int x = 0; x < new_width; ++x)
			{
				const int x0 = x * width / new_width;
				const int x1 = std::max(x0 + 1, (x + 1) * width / new_width);

				for (int c = 0; c < channels; ++c)
				{
					uint32_t sum = 0;

					for (int sy = y0; sy < y1; ++sy)
					{
						for (int sx = x0; sx < x1; ++sx)
							sum += pixels[(static_cast<size_t>(sy) * width + sx) * channels + c];
					}

					out[(static_cast<size_t>(y) * new_width + x) * channels + c] =
						static_cast<uint8_t>(sum / static_cast<uint32_t>((y1 - y0) * (x1 - x0)));
				}
			}
		}

		pixels = std::move(out);
		width = new_width;
		height = new_height;
	}
}
