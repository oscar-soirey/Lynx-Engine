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
		// Pendant sa duree de vie, les spawns sont mis en file d'attente et ne
		// rejoignent actors_ qu'a la fin de la frame (Level::Update). Sans ca, un
		// push_back pendant que le moteur parcourt actors_ peut reallouer le
		// vector et invalider l'iterateur de la boucle for -> crash aleatoire.
		struct IterationScope
		{
			explicit IterationScope(Level& level) : level_(level) { ++level_.iteration_depth_; }
			~IterationScope() { --level_.iteration_depth_; }
			IterationScope(const IterationScope&) = delete;
			IterationScope& operator=(const IterationScope&) = delete;
		private:
			Level& level_;
		};

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
			AddActor(act);

			return t;
		}


		Actor* GetActorFromID(const char* id);
		const std::vector<Actor*>& GetActors() const;
		int CountActorsOfClass(const char* _className) const;

		void SaveToFile(const char* _path);

	private:
		std::vector<Actor*> actors_;
		std::vector<Actor*> destroy_queue_;
		std::vector<Actor*> spawn_queue_;
		int iteration_depth_ = 0;

		// Ajoute directement si personne ne parcourt actors_, sinon differe.
		void AddActor(Actor* act)
		{
			if (iteration_depth_ > 0)
				spawn_queue_.push_back(act);
			else
				actors_.push_back(act);
		}

		Level();
		~Level();

		void Update();

		void LoadFromFile(const char* _path, Engine* engine);
	};
}


#endif //LEVEL_H
