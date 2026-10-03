#pragma once

#include "Object.h"
#include "../core/data/EventDispatcher.h"

namespace lynx
{
	class Engine;

	class LYNX_API Actor : public Object {
		friend class Level;
		friend class Engine;
	public:
		transform transform{};


		Actor();


		//actor class is not copiable
		Actor(const Actor&) = delete;
		Actor& operator=(const Actor&) = delete;
		Actor(Actor&&) = default;
		Actor& operator=(Actor&&) = default;

		HEventDispatcher<> ED_transform_modified;

		virtual void OnPossessed(int pc);
		virtual void OnUnpossessed(int pc);

	protected:
		virtual void OnTransformChanged();

		bool input_enabled_=true;

	private:

		virtual void ProcessInput(){}

		//backend information, do not modify
		Engine* engine_internal_=nullptr;
	};
}