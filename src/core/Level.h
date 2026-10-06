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

		/**
		 * Ajoute au niveau un acteur deja construit (ex : new Player() en
		 * JavaScript) : Init() puis ajout (differe si le niveau est parcouru).
		 */
		void AddSpawnedActor(Actor* actor);
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

		/**
		 * Ecrit le niveau dans assets/<_path> (format XML <Level>). Extension
		 * normale : ".level" (les anciens .xml se chargent toujours).
		 */
		void SaveToFile(const char* _path);

		/**
		 * Monde voxel lie a ce niveau (.hrlv), relatif au DOSSIER du fichier
		 * .level (attribut voxels="..." de <Level>). "" : meme nom que le
		 * niveau avec l'extension .hrlv (voir GetLinkedVoxelWorld).
		 */
		std::string voxel_world;

		/** Fichier (relatif a assets/) dont ce niveau a ete charge. */
		const std::string& GetFilePath() const { return file_path_; }

		// --------------------------------------------------------------------
		// Fichiers de niveau (.level <-> .hrlv)
		// --------------------------------------------------------------------

		/** ".level" */
		static const char* GetExtension();

		/** Extension ".level". */
		static bool IsLevelFile(const std::string& path);

		/**
		 * Monde voxel d'un niveau (chemins relatifs a assets/, lu avec lynx::fs) :
		 * l'attribut voxels du fichier, sinon <meme nom>.hrlv a cote du niveau.
		 */
		static std::string GetLinkedVoxelWorld(const std::string& level_path);

		/**
		 * Niveau de demarrage du jeu : assets/save_file.txt contient le chemin
		 * d'un .level. Ancien format (save_file.txt = chemin du monde voxel,
		 * acteurs dans world.xml) : `legacy_voxel_world` recoit ce chemin et le
		 * niveau retourne est world.level s'il existe, sinon world.xml.
		 */
		static std::string ResolveStartupLevel(std::string* legacy_voxel_world = nullptr);

		/**
		 * Editeur (fichiers sur le disque, chemins reels) : ecrit / remplace
		 * l'attribut voxels d'un .level. `voxels_relative` : relatif au dossier
		 * du niveau ("" retire l'attribut).
		 */
		static bool SetLinkedVoxelWorldOnDisk(const std::string& level_disk_path, const std::string& voxels_relative);

		/** Editeur : attribut voxels brut d'un .level sur le disque ("" si absent). */
		static std::string ReadVoxelLinkOnDisk(const std::string& level_disk_path);

		/**
		 * Folders of the Outliner of the editor (also the empty ones), saved in
		 * the <Level> element. The actors keep theirs in Actor::outliner_folder.
		 */
		std::vector<std::string> outliner_folders;

	private:
		std::vector<Actor*> actors_;
		std::string file_path_;
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
