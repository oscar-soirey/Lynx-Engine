#include "Profiler.h"
#include "Level.h"

#include "../gameplay/Actor.h"
#include "Factory.h"
#include "Filesystem.h"
#include "Engine.h"
#include "../gameplay/PlayerController.h"

#include <xml/tinyxml2.h>
#include <thread>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "data/Typename.h"
#include "../gameplay/Private/ECS.h"

namespace lynx
{
	Level::~Level()
	{
		for (const auto& a : actors_)
		{
			delete a;
		}
		actors_.clear();

		for (const auto& a : spawn_queue_)
		{
			delete a;
		}
		spawn_queue_.clear();
	}

	void Level::LoadFromFile(const char *_path, Engine* engine)
	{
		LYNX_PROFILE_SCOPE("Level::LoadFromFile");
		using namespace tinyxml2;

		auto file_data = fs::ReadBinary(_path);

		XMLDocument doc;
		XMLError eResult = doc.Parse(reinterpret_cast<const char*>(file_data.data()), file_data.size());

		if (eResult != XML_SUCCESS)
		{
			std::cout << "error while reading level file" << std::endl;
			//LOG_ERROR("error while reading level file");
			return;
		}

		XMLElement* root = doc.RootElement();  //level tag
		if (!root)
		{
			//LOG_ERROR("no <level> tag");
			std::cout << "no <level> tag" << std::endl;
			return;
		}

		file_path_ = _path ? _path : "";
		voxel_world = root->Attribute("voxels") ? root->Attribute("voxels") : "";

		outliner_folders.clear();
		if (const char* folders = root->Attribute("outliner_folders"))
		{
			std::string all = folders;
			size_t start = 0;
			while (start <= all.size())
			{
				const size_t bar = all.find('|', start);
				const std::string f = all.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
				if (!f.empty())
					outliner_folders.push_back(f);
				if (bar == std::string::npos)
					break;
				start = bar + 1;
			}
		}

		//each actor elements
		for (XMLElement* actor = root->FirstChildElement();
			actor != nullptr;
			actor = actor->NextSiblingElement()
		)
		{

			//find constructor actor
			std::string type_name = actor->Name();  //name of the actor class, eg. Player, Character
			auto ctor = engine->GetFactory().GetObjectConstr(type_name.c_str());
			if (!ctor.has_value())
			{
				//LOG_ERROR("actor not found in factory");
				std::cout << "actor not found in factory" << std::endl;
				continue;
			}

			//construct actor
			auto* obj = (Actor*)ctor.value()();
			if (!obj)
			{
				//LOG_ERROR("constructor error");
			std::cout << "constructor error" << std::endl;
				continue;
			}

			//pass the scene backend id
			//obj->backend_scene_id_ = GetEngineHRL_SceneID();
			obj->engine_internal_ = engine;

			//initialize each attributes
			const XMLAttribute* attr = actor->FirstAttribute();
			while (attr)
			{
				if (obj->PropertyExists(attr->Name()))
				{
					obj->SetPropertyValueStr(attr->Name(), attr->Value());
				}
				else
				{
					//LOG_ERROR("Property doesnt exists");
			std::cout << "Property doesnt exists: " << attr->Name() << std::endl;
				}

				attr = attr->Next();
			}

			//init before manipulate components (components created at init function)
			obj->Init();

			//add the actor ptr to the vector of the level
			actors_.emplace_back(obj);
		}
	}

	Actor *Level::GetActorFromID(const char* id)
	{
		for (const auto& a: actors_)
		{
			if (a->object_id_ == id)
			{
				return a;
			}
		}
		return nullptr;
	}

	const std::vector<Actor*> &Level::GetActors() const
	{
		return actors_;
	}

	Actor* Level::SpawnActor(const char* _className)
	{
		// .value() sur un optional vide leve une exception : on teste d'abord.
		auto constructor = lynx::Engine::Get()->GetFactory().GetObjectConstr(_className);
		if (!constructor.has_value() || !constructor.value())
		{
			printf("Class: %s not found\n", _className);
			return nullptr;
		}
		Object* obj = constructor.value()();
		auto* act = dynamic_cast<Actor*>(obj);

		if (!act)
		{
			//LOG_ERROR("Try to spawn actor but class is not derived of HGE_Actor");
			delete obj;
			return nullptr;
		}

		//act->backend_scene_id_ = GetEngineHRL_SceneID();
		act->engine_internal_ = lynx::Engine::Get();
		act->Init();
		AddActor(act);

		return act;
	}

	void Level::DestroyActor(Actor *_act)
	{
		destroy_queue_.push_back(_act);
	}

	void Level::AddSpawnedActor(Actor* actor)
	{
		if (!actor)
			return;

		actor->engine_internal_ = lynx::Engine::Get();
		actor->Init();
		AddActor(actor);
	}

	int Level::CountActorsOfClass(const char* _className) const
	{
		return std::count_if(actors_.begin(), actors_.end(), [_className](Actor* a)
		{
			//changer la Player brut par une fonction ClassName
			return a && a->GetTypeName() == _className;
		});
	}




	Level::Level()=default;


	void Level::SaveToFile(const char* _path)
	{
		using namespace tinyxml2;

		XMLDocument doc;

		XMLElement* root = doc.NewElement("Level");
		doc.InsertFirstChild(root);

		// Monde voxel lie (relatif au dossier du niveau).
		if (!voxel_world.empty())
			root->SetAttribute("voxels", voxel_world.c_str());

		// Folders of the Outliner (editor), "a|a/b|c".
		if (!outliner_folders.empty())
		{
			std::string folders;
			for (const std::string& f : outliner_folders)
				folders += (folders.empty() ? "" : "|") + f;
			root->SetAttribute("outliner_folders", folders.c_str());
		}

		for (Actor* actor : actors_)
		{
			if (!actor)
				continue;

			// Le nom de la classe devient le nom de l'élément XML.
			// C'est exactement ce que LoadFromFile() attend.
			std::string type_name = actor->GetTypeName();

			XMLElement* actor_element = doc.NewElement(type_name.c_str());

			// Sauvegarde de toutes les propriétés.
			const auto& properties = actor->GetProperties();

			for (const auto& [name, prop] : properties)
			{
				// Convertit automatiquement int, float, bool, string,
				// vec2, vec3, vec4 et transform en string.
				std::string value = PropertyToString(prop);

				actor_element->SetAttribute(
						name.c_str(),
						value.c_str()
				);
			}

			root->InsertEndChild(actor_element);
		}

		// Les niveaux sont sauvegardés dans assets/
		std::string output_path = "assets/" + std::string(_path);

		XMLError result = doc.SaveFile(output_path.c_str());

		if (result != XML_SUCCESS)
		{
			std::cout << "error while saving level file: "
								<< output_path << std::endl;
			return;
		}

		std::cout << "level saved: "
							<< output_path << std::endl;
	}


	void Level::Update()
	{
		// Les acteurs spawnes pendant la frame rejoignent la liste ici, hors de
		// toute boucle sur actors_. Avant les destructions : un acteur spawne
		// puis detruit dans la meme frame doit etre retrouve.
		actors_.insert(actors_.end(), spawn_queue_.begin(), spawn_queue_.end());
		spawn_queue_.clear();

		for (auto& a: destroy_queue_)
		{
			// std::erase retourne le nombre d'elements retires : 0 si l'acteur
			// a deja ete detruit (DestroyActor appele deux fois) -> pas de double delete.
			if (std::erase(actors_, a) > 0)
			{
				// Le joueur le relache (OnUnpossessed) tant qu'il est entier.
				if (a->controller_)
					a->controller_->Unpossess();

				// EndPlay des composants tant que l'acteur est encore entier
				ecs::OnActorDestroyed(a);
				delete a;
			}
		}
		destroy_queue_.clear();
	}


	// ========================================================================
	// Fichiers de niveau
	// ========================================================================

	namespace
	{
		std::string Trimmed(std::string s)
		{
			while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
				s.pop_back();
			size_t i = 0;
			while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\xEF' || s[i] == '\xBB' || s[i] == '\xBF'))
				++i;
			return s.substr(i);
		}

		std::string LowerExt(const std::string& path)
		{
			std::string ext = std::filesystem::path(path).extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return ext;
		}

		std::string ReadText(const std::string& path)
		{
			const auto data = fs::ReadBinary(path);
			return std::string(reinterpret_cast<const char*>(data.data()), data.size());
		}
	}

	const char* Level::GetExtension()
	{
		return ".level";
	}

	bool Level::IsLevelFile(const std::string& path)
	{
		return LowerExt(path) == ".level";
	}

	std::string Level::GetLinkedVoxelWorld(const std::string& level_path)
	{
		namespace sfs = std::filesystem;
		const sfs::path level(level_path);

		std::string link;
		{
			const std::string text = ReadText(level_path);
			tinyxml2::XMLDocument doc;
			if (!text.empty() && doc.Parse(text.c_str(), text.size()) == tinyxml2::XML_SUCCESS && doc.RootElement())
				if (const char* v = doc.RootElement()->Attribute("voxels"))
					link = v;
		}

		if (link.empty())
			link = level.stem().string() + ".hrlv";

		return (level.parent_path() / link).lexically_normal().generic_string();
	}

	std::string Level::ResolveStartupLevel(std::string* legacy_voxel_world)
	{
		if (legacy_voxel_world)
			legacy_voxel_world->clear();

		const std::string saved = Trimmed(ReadText("save_file.txt"));

		if (LowerExt(saved) == ".level")
			return saved;

		// Ancien format : save_file.txt = monde voxel, acteurs dans world.xml.
		if (legacy_voxel_world)
			*legacy_voxel_world = saved.empty() ? std::string("world.vox") : saved;

		if (fs::Exists("world.level"))
			return "world.level";
		return "world.xml";
	}

	bool Level::SetLinkedVoxelWorldOnDisk(const std::string& level_disk_path, const std::string& voxels_relative)
	{
		tinyxml2::XMLDocument doc;
		if (doc.LoadFile(level_disk_path.c_str()) != tinyxml2::XML_SUCCESS)
			return false;

		tinyxml2::XMLElement* root = doc.RootElement();
		if (!root)
			return false;

		if (voxels_relative.empty())
			root->DeleteAttribute("voxels");
		else
			root->SetAttribute("voxels", voxels_relative.c_str());

		return doc.SaveFile(level_disk_path.c_str()) == tinyxml2::XML_SUCCESS;
	}

	std::string Level::ReadVoxelLinkOnDisk(const std::string& level_disk_path)
	{
		tinyxml2::XMLDocument doc;
		if (doc.LoadFile(level_disk_path.c_str()) != tinyxml2::XML_SUCCESS || !doc.RootElement())
			return {};
		const char* v = doc.RootElement()->Attribute("voxels");
		return v ? v : "";
	}
}
