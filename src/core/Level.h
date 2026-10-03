#ifndef LEVEL_H
#define LEVEL_H

#include <vector>
#include <string>

#include <Lynx.h>
#include "Common.h"
#include "gameplay/Actor.h"

namespace lynx
{
	class Actor;
	class Engine;

	class LYNX_API Level {
		friend class Engine;

	public:
		Actor* SpawnActor(const char* _className);
		void DestroyActor(Actor* _act);

		template<typename T>
		T* SpawnActorFromClass(transform transform_, const char* id="")
		{
			T* t = new T();
			auto* act = dynamic_cast<Actor*>(t);
			if (!act)
			{
				delete t;
				printf("tried to spawn actor but given class is not an lynx::Actor\n");
				return nullptr;
			}

			act->object_id_ = id;
			act->transform = transform_;
			act->Init();
			actors_.push_back(act);

			return t;
		}


		Actor* GetActorFromID(const char* id);
		const std::vector<Actor*>& GetActors() const;
		int CountActorsOfClass(const char* _className) const;

		void SaveToFile(const char* _path);

	private:
		std::vector<Actor*> actors_;
		std::vector<Actor*> destroy_queue_;

		Level();
		~Level();

		void Update();

		void LoadFromFile(const char* _path, Engine* engine);
	};
}


#endif //LEVEL_H
