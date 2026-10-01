#ifndef LEVEL_H
#define LEVEL_H


#include <vector>
#include "Common.h"

namespace lynx
{
	class Actor;
	class Engine;

	class LYNX_API Level {
		friend class Engine;

	public:
		Actor* SpawnActor(const char* _className);
		void DestroyActor(Actor* _act);


		Actor* GetActorFromID(const char* id);
		const std::vector<Actor*>& GetActors() const;
		int CountActorsOfClass(const char* _className) const;

		void SaveToFile(const char* _path);

	private:
		std::vector<Actor*> actors_;

		Level();
		~Level();

		void LoadFromFile(const char* _path, Engine* engine);
	};
}


#endif //LEVEL_H
