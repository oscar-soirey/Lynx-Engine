#pragma once

// =============================================================================
// Voxel Assembly (.vasm) : actor classes made of blocks
// -----------------------------------------------------------------------------
// A .vasm file (anywhere in assets/) is an actor class : a name, a parent C++
// class (Actor, Humanoid...), variables, event graphs of blocks (when the game
// starts, every frame, when I receive...) and functions, each with its graph.
// The editor makes them (Voxel Assembly editor) ; the engine runs them itself,
// without JavaScript : PrepareClasses puts every class in the factory (Place
// Actors, levels, Level spawn), a component runs the blocks of each actor.
//
// Like Scratch, the scripts of an actor run side by side ; wait, repeat,
// forever... let a frame pass. Variables of the actor are properties (Details,
// saved with the level) ; global variables are shared by every actor.
//
//   File format (text) :
//     lynx-vasm 1
//     class Hero
//     extends Humanoid
//     var v1 number actor score 0          id, type, actor / global, name, default
//     function f1 Take Damage               id, name
//     input f1 amount                       an input of f1
//     graph event Event Graph               an event graph and its scripts
//     (script 40 40 (block "ev.start" ()) ...)
//     end
//     graph function f1                     the graph of f1 (its "when called" block)
//     ...
//     end
//   The scripts are those of the Raven block editor (Rblock_editor_save).
// =============================================================================

#include <string>
#include <vector>

#include "../core/Common.h"

namespace lynx
{
	class Actor;
}

namespace lynx::vasm
{
	/** File extension : ".vasm" */
	LYNX_API const char* GetExtension();

	/**
	 * Reads every .vasm of assets/ and puts their classes in the factory (the
	 * first time a name is seen). Called before each level is created ; the
	 * editor calls it again after a save. The actors already made take the
	 * new blocks at their next BeginPlay.
	 */
	LYNX_API void PrepareClasses();

	/** Names of the Voxel Assembly classes (after PrepareClasses). */
	LYNX_API std::vector<std::string> GetClassNames();

	/** `name` is a Voxel Assembly class. */
	LYNX_API bool IsClass(const std::string& name);

	/**
	 * Checks a .vasm text (the editor, before a save) : `error` says what is
	 * wrong (a parent class that does not exist...). true : it can run.
	 */
	LYNX_API bool Check(const std::string& text, std::string& error);

	// ---- Events of the engine (collisions, Humanoid) -------------------------------

	/** "OnBeginOverlap", "OnEndOverlap", "OnLanded", "OnJumped". */
	LYNX_API void OnActorEvent(Actor* self, const char* event, Actor* other);
	LYNX_API void OnActorHit(Actor* self, Actor* other, const vec3& normal);
}
