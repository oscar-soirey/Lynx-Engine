#ifndef LEVEL_H
#define LEVEL_H


#include <vector>
#include <string>
#include <cstddef>
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

		// In-memory snapshot of every actor (same XML format as SaveToFile).
		std::string SerializeToString() const;

		// Destroys every current actor, then recreates them from a string produced
		// by SerializeToString(). Same path as loading a level file : properties
		// are set BEFORE Init(). All Actor pointers obtained before are invalid.
		void RestoreFromString(const std::string& _data);

	private:
		std::vector<Actor*> actors_;

		Level();
		~Level();

		void LoadFromFile(const char* _path, Engine* engine);
		void LoadFromBuffer(const char* _data, size_t _size, Engine* engine);
	};
}


#endif //LEVEL_H
