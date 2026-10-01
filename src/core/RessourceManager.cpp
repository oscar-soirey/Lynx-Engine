#include "RessourceManager.h"

#include <unordered_map>
#include <string>
#include <hrl/hrl.h>
#include "Filesystem.h"

namespace
{
	std::unordered_map<std::string, uint32_t> res_;
}

namespace lynx
{
	uint32_t RessourceTex(const char *path)
	{
		auto it = res_.find(path);
		if (it == res_.end())
		{
			auto data = fs::ReadBinary(path);

			// Missing / unreadable file: don't cache it, so it can load later.
			if (data.empty())
				return HRL_INVALID_ID;

			HRL_id id = HRL_CreateTexture(reinterpret_cast<const char*>(data.data()), data.size());
			res_.emplace(path, id);
			return id;
		}
		return it->second;
	}
}
