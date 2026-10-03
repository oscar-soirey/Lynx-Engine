#include "Level.h"

#include "../gameplay/Actor.h"
#include "Factory.h"
#include "Filesystem.h"
#include "Engine.h"

#include <xml/tinyxml2.h>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <iostream>

#include "data/Typename.h"

namespace lynx
{
	Level::~Level()
	{
		for (const auto& a : actors_)
		{
			delete a;
		}
		actors_.clear();
	}

	void Level::LoadFromFile(const char *_path, Engine* engine)
	{
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
		ObjectConstructor constructor = lynx::GetEngine()->GetFactory().GetObjectConstr(_className).value();
		if (!constructor)
		{
			printf("Class: %s not found\n", _className);
			return nullptr;
		}
		Object* obj = constructor();
		auto* act = dynamic_cast<Actor*>(obj);

		if (!act)
		{
			//LOG_ERROR("Try to spawn actor but class is not derived of HGE_Actor");
			return nullptr;
		}

		//act->backend_scene_id_ = GetEngineHRL_SceneID();
		act->Init();
		actors_.push_back(act);

		return act;
	}

	void Level::DestroyActor(Actor *_act)
	{
		destroy_queue_.push_back(_act);
	}

	int Level::CountActorsOfClass(const char* _className) const
	{
		return std::count_if(actors_.begin(), actors_.end(), [_className](Actor* a)
		{
			//changer la Player brut par une fonction ClassName
			return std::strcmp(ETypeName(*a).c_str(), _className) == 0;
		});
	}




	Level::Level()=default;


	void Level::SaveToFile(const char* _path)
	{
		using namespace tinyxml2;

		XMLDocument doc;

		XMLElement* root = doc.NewElement("Level");
		doc.InsertFirstChild(root);

		for (Actor* actor : actors_)
		{
			if (!actor)
				continue;

			// Le nom de la classe devient le nom de l'élément XML.
			// C'est exactement ce que LoadFromFile() attend.
			std::string type_name = ETypeName(*actor);

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
		for (auto& a: destroy_queue_)
		{
			delete a;
			std::erase(actors_, a);
		}
		destroy_queue_.clear();
	}
}
