#include "RessourceManager.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <unordered_map>
#include <string>
#include <hrl/hrl.h>
#include "Filesystem.h"
#include "JobSystem.h"
#include "Profiler.h"

namespace
{
	std::unordered_map<std::string, uint32_t> res_;
	std::unordered_map<std::string, uint32_t> revisions_;
	bool async_decoding_ = true;

	struct Decoder
	{
		lynx::TextureDecoder decode = nullptr;
		bool nearest = true;
	};

	std::unordered_map<std::string, Decoder>& Decoders()
	{
		static std::unordered_map<std::string, Decoder> decoders;
		return decoders;
	}

	std::string Extension(const std::string& path)
	{
		const size_t dot = path.find_last_of('.');
		if (dot == std::string::npos || path.find_first_of("/\\", dot) != std::string::npos)
			return {};
		std::string ext = path.substr(dot);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return ext;
	}

	const Decoder* FindDecoder(const std::string& path)
	{
		const auto it = Decoders().find(Extension(path));
		return it != Decoders().end() && it->second.decode ? &it->second : nullptr;
	}
}

namespace lynx
{
	void RegisterTextureDecoder(const char* extension, TextureDecoder decoder, bool nearest)
	{
		if (!extension || !decoder)
			return;
		std::string ext = extension;
		if (!ext.empty() && ext[0] != '.')
			ext = "." + ext;
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		Decoders()[ext] = { decoder, nearest };
	}

	void UnregisterTextureDecoder(const char* extension)
	{
		if (!extension)
			return;
		std::string ext = extension;
		if (!ext.empty() && ext[0] != '.')
			ext = "." + ext;
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		Decoders().erase(ext);

		// Textures made by this decoder : forgotten (the plugin is unloaded).
		for (auto it = res_.begin(); it != res_.end();)
		{
			if (Extension(it->first) == ext)
				it = res_.erase(it);
			else
				++it;
		}
	}

	std::vector<std::uint8_t> ReadTextureFile(const char* path)
	{
		std::vector<std::uint8_t> data = fs::ReadBinary(path);
		if (data.empty())
			return data;

		if (const Decoder* decoder = FindDecoder(path))
		{
			std::vector<std::uint8_t> image;
			if (!decoder->decode(data, image))
			{
				std::cout << "[Texture] " << path << " : the file could not be decoded\n";
				return {};
			}
			return image;
		}
		return data;
	}

	uint32_t RessourceTex(const char *path)
	{
		auto it = res_.find(path);
		if (it == res_.end())
		{
			auto data = ReadTextureFile(path);

			// Missing / unreadable file: don't cache it, so it can load later.
			if (data.empty())
				return HRL_INVALID_ID;

			HRL_id id = HRL_CreateTexture(reinterpret_cast<const char*>(data.data()), data.size());
			if (id != HRL_INVALID_ID)
			{
				if (const Decoder* decoder = FindDecoder(path); decoder && decoder->nearest)
				{
					HRL_SetTextureMinFilter(id, HRL_FILTER_NEAREST);
					HRL_SetTextureMagFilter(id, HRL_FILTER_NEAREST);
				}
			}
			res_.emplace(path, id);
			return id;
		}
		return it->second;
	}

	void SetAsyncTextureDecoding(bool enabled)
	{
		async_decoding_ = enabled;
	}

	std::vector<std::uint32_t> CreateTextures(const std::vector<std::string>& paths)
	{
		LYNX_PROFILE_SCOPE("CreateTextures");
		LYNX_ASSERT_MAIN_THREAD();

		const int count = static_cast<int>(paths.size());
		std::vector<std::uint32_t> ids(paths.size(), HRL_INVALID_ID);
		if (count == 0)
			return ids;

		// 1. Files (disk / archive) and plugin decoders : worker threads.
		//    Nothing but the bytes of the file here (no HRL).
		std::vector<std::vector<std::uint8_t>> data(paths.size());
		jobs::ParallelFor(0, count, 1, [&](int i)
		{
			data[static_cast<size_t>(i)] = ReadTextureFile(paths[static_cast<size_t>(i)].c_str());
		});

		// 2. HRL (OpenGL context) : main thread. The async version decodes the
		//    images on the HRL threads while the next ones are queued.
		{
			LYNX_PROFILE_SCOPE("HRL textures");
			for (int i = 0; i < count; ++i)
			{
				const auto& bytes = data[static_cast<size_t>(i)];
				if (bytes.empty())
					continue;
				const char* ptr = reinterpret_cast<const char*>(bytes.data());
				ids[static_cast<size_t>(i)] = async_decoding_ ? HRL_CreateTextureAsync(ptr, bytes.size())
				                                              : HRL_CreateTexture(ptr, bytes.size());
			}
			// The buffers stay alive until the uploads are done.
			if (async_decoding_)
				HRL_WaitForAllAsyncResources();
		}

		for (int i = 0; i < count; ++i)
		{
			const uint32_t id = ids[static_cast<size_t>(i)];
			if (id == HRL_INVALID_ID)
				continue;
			if (!HRL_IsValidTexture(id))
			{
				ids[static_cast<size_t>(i)] = HRL_INVALID_ID;
				continue;
			}
			if (const Decoder* decoder = FindDecoder(paths[static_cast<size_t>(i)]); decoder && decoder->nearest)
			{
				HRL_SetTextureMinFilter(id, HRL_FILTER_NEAREST);
				HRL_SetTextureMagFilter(id, HRL_FILTER_NEAREST);
			}
		}
		return ids;
	}

	void PreloadTextures(const std::vector<std::string>& paths)
	{
		std::vector<std::string> missing;
		for (const std::string& path : paths)
		{
			if (path.empty() || res_.count(path) ||
			    std::find(missing.begin(), missing.end(), path) != missing.end())
				continue;
			missing.push_back(path);
		}

		const std::vector<std::uint32_t> ids = CreateTextures(missing);
		for (size_t i = 0; i < missing.size(); ++i)
		{
			// Same rule as RessourceTex : a failed file is not cached.
			if (ids[i] != HRL_INVALID_ID)
				res_.emplace(missing[i], ids[i]);
		}
	}

	uint32_t GetTextureRevision(const char* path)
	{
		const auto it = revisions_.find(path);
		return it != revisions_.end() ? it->second : 0u;
	}

	bool ReloadRessourceTexture(const char* path)
	{
		++revisions_[path];
		auto it = res_.find(path);
		if (it == res_.end() || !HRL_IsValidTexture(it->second))
			return false;

		const auto data = ReadTextureFile(path);
		if (data.empty())
			return false;

		HRL_ReloadTexture(it->second, reinterpret_cast<const char*>(data.data()), data.size());
		if (const Decoder* decoder = FindDecoder(path); decoder && decoder->nearest)
		{
			HRL_SetTextureMinFilter(it->second, HRL_FILTER_NEAREST);
			HRL_SetTextureMagFilter(it->second, HRL_FILTER_NEAREST);
		}
		return true;
	}

	uint32_t RessourceFont(const char *path)
	{
		auto it = res_.find(path);
		if (it == res_.end())
		{
			auto data = fs::ReadBinary(path);

			// Missing / unreadable file: don't cache it, so it can load later.
			if (data.empty())
				return HRL_INVALID_ID;

			HRL_id id = HRL_CreateFont(reinterpret_cast<const char*>(data.data()), data.size());
			res_.emplace(path, id);
			return id;
		}
		return it->second;
	}
}
