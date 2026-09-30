#include "PlayerController.h"

#include "Actor.h"
#include "core/engine.h"

#include <hrl/hrl.h>
#include <unordered_map>


static int internal_player_id=0;

struct player_controller_t {
	lynx::Actor* possessed_actor_=nullptr;
	HRL_id viewport_ = HRL_INVALID_ID;
};

static std::unordered_map<int, player_controller_t> controllers;



namespace lynx
{

	int CreatePlayer()
	{
		int new_id = internal_player_id++;

		player_controller_t pc_{
			nullptr, HRL_INVALID_ID
		};

		controllers.emplace(new_id, pc_);
		return new_id;
	}

	void DeletePlayer(int pc)
	{
		auto it = controllers.find(pc);
		if (it == controllers.end())
		{
			//LOG_ERROR("DeletePlayer, invalid id");
			return;
		}
		UnpossessActor(pc);
		controllers.erase(it);
	}


	void PossessActor(int pc, Actor *act)
	{
		auto it = controllers.find(pc);
		if (it == controllers.end())
		{
			//LOG_ERROR("PossessActor, invalid id");
			return;
		}
		//unpossess current possessed actor
		UnpossessActor(pc);
		act->OnPossessed(pc);
		it->second.possessed_actor_ = act;
	}

	void UnpossessActor(int pc)
	{
		auto it = controllers.find(pc);
		if (it == controllers.end())
		{
			//LOG_ERROR("UnpossessActor, invalid id");
			return;
		}
		if (it->second.possessed_actor_)
		{
			it->second.possessed_actor_->OnUnpossessed(pc);
		}
		it->second.possessed_actor_ = nullptr;

	}

	Actor *GetPossessedActor(int pc)
	{
		auto it = controllers.find(pc);
		if (it == controllers.end())
		{
			//LOG_ERROR("GetPossessedActor, invalid id");
			return nullptr;
		}
		return it->second.possessed_actor_;
	}

	void SetPlayerViewportSize(int pc,
		float x, float y,
		float _width, float _height
	)
	{
		auto it = controllers.find(pc);
		if (it == controllers.end())
		{
			//LOG_ERROR("SetPlayerViewportSize, invalid id");
			return;
		}
		HRL_SetViewportRect(it->second.viewport_, x, y, _width, _height);
	}

	uint32_t GetPlayerViewportBackend(int pc)
	{
		auto it = controllers.find(pc);
		if (it == controllers.end())
		{
			//LOG_ERROR("GetPlayerViewportBackend, invalid id");
			return -1;
		}
		return it->second.viewport_;
	}



	int GetPlayerCount()
	{
		return controllers.size();
	}
}

