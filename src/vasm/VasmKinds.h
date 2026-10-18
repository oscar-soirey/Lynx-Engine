#pragma once

// =============================================================================
// Voxel Assembly : the kinds of blocks
// -----------------------------------------------------------------------------
// One table for the editor (palette, labels, help) and the runtime (what each
// block does : VasmRuntime.cpp, a case per name). A label is the one of the
// Raven block editor : words and slots, %n a number, %s a text, %b a
// condition, %r any value, %m a choice in a menu ; {text} after a slot is its
// default ; | separates the sections of a C block.
//
// Kinds made for a file (not in this table) :
//   var.<id>        the value of a variable (reporter)
//   call.<fid>      runs a function (statement, one %r per input)
//   callv.<fid>     the value a function returns (reporter)
//   arg.<fid>.<n>   an input of the function, in its own graph (reporter)
// =============================================================================

#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace lynx::vasm
{
	enum class Shape
	{
		Statement,
		Hat,
		Cap,
		Wrap,       // C block
		Value,      // reporter
		Condition,  // boolean
	};

	/** What fills a %m slot (the editor gives the choices). */
	enum class Choices
	{
		None,
		Actions,        // input actions of the project
		Axes,           // input axes
		Variables,      // variables of the file
		Fixed,          // `options` below
	};

	struct KindInfo
	{
		const char* name;
		const char* label;
		Shape shape;
		const char* category;
		const char* help;
		Choices choices = Choices::None;   // of the first %m slot
		const char* options = "";          // Choices::Fixed, separated by '\n'
	};

	inline constexpr KindInfo kKinds[] = {
		// ---- Events ------------------------------------------------------------------------
		{ "ev.start", "when the game starts", Shape::Hat, "Events", "Runs once, when the actor begins to play." },
		{ "ev.tick", "every frame", Shape::Hat, "Events", "Runs at each frame. If it waits, the next run starts once this one is finished." },
		{ "ev.end", "when the game ends", Shape::Hat, "Events", "When the actor leaves the game (no waits here)." },
		{ "ev.key", "when %m{Jump} is pressed", Shape::Hat, "Events", "An action of Input Settings.", Choices::Actions },
		{ "ev.key_up", "when %m{Jump} is released", Shape::Hat, "Events", "An action of Input Settings.", Choices::Actions },
		{ "ev.overlap", "when something touches me", Shape::Hat, "Events", "Another collider starts to overlap mine. \"other actor\" is it." },
		{ "ev.overlap_end", "when something stops touching me", Shape::Hat, "Events", "\"other actor\" is it." },
		{ "ev.hit", "when I bump into something", Shape::Hat, "Events", "My blocking collider hit an actor or a wall. \"other actor\" is it (nothing for a wall)." },
		{ "ev.landed", "when I land", Shape::Hat, "Events", "Humanoid : back on the ground." },
		{ "ev.jumped", "when I jump", Shape::Hat, "Events", "Humanoid : a jump started." },
		{ "ev.receive", "when I receive %s{message1}", Shape::Hat, "Events", "A broadcast of any actor of the level." },
		{ "ev.broadcast", "broadcast %s{message1}", Shape::Statement, "Events", "Every Voxel Assembly actor runs its \"when I receive\" scripts." },
		{ "ev.broadcast_wait", "broadcast %s{message1} and wait", Shape::Statement, "Events", "Same, then waits until those scripts are finished." },

		// ---- Control -----------------------------------------------------------------------
		{ "ctl.wait", "wait %n{1} seconds", Shape::Statement, "Control", "Game time (follows the time dilation)." },
		{ "ctl.repeat", "repeat %n{10}", Shape::Wrap, "Control", "One turn per frame." },
		{ "ctl.forever", "forever", Shape::Wrap, "Control", "One turn per frame, until the script stops." },
		{ "ctl.if", "if %b then", Shape::Wrap, "Control", "" },
		{ "ctl.ifelse", "if %b then|else", Shape::Wrap, "Control", "" },
		{ "ctl.wait_until", "wait until %b", Shape::Statement, "Control", "Checked once per frame." },
		{ "ctl.repeat_until", "repeat until %b", Shape::Wrap, "Control", "One turn per frame." },
		{ "ctl.while", "while %b", Shape::Wrap, "Control", "One turn per frame." },
		{ "ctl.for", "for each %m from %n{1} to %n{10}", Shape::Wrap, "Control", "Counts with a variable, one turn per frame.", Choices::Variables },
		{ "ctl.stop", "stop this script", Shape::Cap, "Control", "" },
		{ "ctl.stop_all", "stop all my scripts", Shape::Cap, "Control", "Every script of this actor stops." },

		// ---- Motion ------------------------------------------------------------------------
		{ "mo.change_x", "change x by %n{1}", Shape::Statement, "Motion", "" },
		{ "mo.set_x", "set x to %n{0}", Shape::Statement, "Motion", "" },
		{ "mo.change_y", "change y by %n{1}", Shape::Statement, "Motion", "" },
		{ "mo.set_y", "set y to %n{0}", Shape::Statement, "Motion", "" },
		{ "mo.goto", "go to x %n{0} y %n{0}", Shape::Statement, "Motion", "" },
		{ "mo.goto_actor", "go to %r", Shape::Statement, "Motion", "Onto an actor (\"other actor\", \"first actor with tag\"...)." },
		{ "mo.glide", "glide %n{1} secs to x %n{0} y %n{0}", Shape::Statement, "Motion", "" },
		{ "mo.toward", "move toward %r speed %n{3}", Shape::Statement, "Motion", "One step of this frame (in a forever / every frame)." },
		{ "mo.set_vel", "set velocity x %n{0} y %n{0}", Shape::Statement, "Motion", "Units per second ; stops against the walls when the actor has a collider." },
		{ "mo.set_vel_x", "set velocity x to %n{0}", Shape::Statement, "Motion", "" },
		{ "mo.set_vel_y", "set velocity y to %n{0}", Shape::Statement, "Motion", "" },
		{ "mo.change_vel", "change velocity by x %n{0} y %n{0}", Shape::Statement, "Motion", "" },
		{ "mo.x", "x position", Shape::Value, "Motion", "" },
		{ "mo.y", "y position", Shape::Value, "Motion", "" },
		{ "mo.vx", "velocity x", Shape::Value, "Motion", "" },
		{ "mo.vy", "velocity y", Shape::Value, "Motion", "" },

		// ---- Character (Humanoid) ----------------------------------------------------------
		{ "ch.walk", "walk %n{1}", Shape::Statement, "Character", "Humanoid : -1 left, 1 right (each frame : in a forever / every frame)." },
		{ "ch.jump", "jump", Shape::Statement, "Character", "Humanoid." },
		{ "ch.stop_jump", "stop jumping", Shape::Statement, "Character", "Humanoid : a short jump when the button is released." },
		{ "ch.launch", "launch x %n{0} y %n{10}", Shape::Statement, "Character", "Humanoid : a push (jump pad, knock back)." },
		{ "ch.stop", "stop moving", Shape::Statement, "Character", "Humanoid." },
		{ "ch.grounded", "on the ground?", Shape::Condition, "Character", "Humanoid." },
		{ "ch.falling", "falling?", Shape::Condition, "Character", "Humanoid." },
		{ "ch.facing_right", "facing right?", Shape::Condition, "Character", "Humanoid." },

		// ---- Looks -------------------------------------------------------------------------
		{ "lk.show", "show", Shape::Statement, "Looks", "" },
		{ "lk.hide", "hide", Shape::Statement, "Looks", "" },
		{ "lk.image", "set image to %s{sprite.png}", Shape::Statement, "Looks", "A sprite (added when the actor has none)." },
		{ "lk.anim", "play animation %s{run.png} frames %n{8} every %n{0.1} s", Shape::Statement, "Looks", "A horizontal strip of frames." },
		{ "lk.graph", "use anim graph %s{anim/Hero.animgraph}", Shape::Statement, "Looks", "An Anim Graph of the project." },
		{ "lk.param", "set anim %s{speed} to %r{0}", Shape::Statement, "Looks", "A parameter of the Anim Graph." },
		{ "lk.trigger", "trigger anim %s{hurt}", Shape::Statement, "Looks", "A trigger of the Anim Graph." },
		{ "lk.face", "face %m{right}", Shape::Statement, "Looks", "Flips the sprite.", Choices::Fixed, "left\nright" },
		{ "lk.size", "set size to %n{100} %", Shape::Statement, "Looks", "" },
		{ "lk.state", "animation state", Shape::Value, "Looks", "" },

		// ---- Sound -------------------------------------------------------------------------
		{ "snd.play", "play sound %s{jump.wav}", Shape::Statement, "Sound", "" },
		{ "snd.stop", "stop sound", Shape::Statement, "Sound", "" },
		{ "snd.volume", "set volume to %n{100} %", Shape::Statement, "Sound", "" },

		// ---- Sensing -----------------------------------------------------------------------
		{ "se.pressed", "action %m{Jump} pressed?", Shape::Condition, "Sensing", "Went down this frame.", Choices::Actions },
		{ "se.held", "action %m{Jump} held?", Shape::Condition, "Sensing", "", Choices::Actions },
		{ "se.released", "action %m{Jump} released?", Shape::Condition, "Sensing", "", Choices::Actions },
		{ "se.axis", "axis %m{MoveRight}", Shape::Value, "Sensing", "An axis of Input Settings (-1 to 1).", Choices::Axes },
		{ "se.touching", "touching tag %s{enemy}?", Shape::Condition, "Sensing", "My collider overlaps an actor with this tag (empty : any actor)." },
		{ "se.has_tag", "%r has tag %s{player}?", Shape::Condition, "Sensing", "" },
		{ "se.distance", "distance to %r", Shape::Value, "Sensing", "" },
		{ "se.of", "%m{x} of %r", Shape::Value, "Sensing", "", Choices::Fixed, "x\ny\nz\nid\nclass" },
		{ "se.prop", "property %s{hp} of %r", Shape::Value, "Sensing", "A property of an actor (Details), or a variable of a Voxel Assembly actor." },
		{ "se.valid", "%r exists?", Shape::Condition, "Sensing", "The actor is still in the level." },
		{ "se.timer", "timer", Shape::Value, "Sensing", "Seconds since the start (or reset timer)." },
		{ "se.reset_timer", "reset timer", Shape::Statement, "Sensing", "" },

		// ---- Operators ---------------------------------------------------------------------
		{ "op.add", "%n + %n", Shape::Value, "Operators", "" },
		{ "op.sub", "%n - %n", Shape::Value, "Operators", "" },
		{ "op.mul", "%n * %n", Shape::Value, "Operators", "" },
		{ "op.div", "%n / %n", Shape::Value, "Operators", "" },
		{ "op.rand", "pick random %n{1} to %n{10}", Shape::Value, "Operators", "Whole numbers when both ends are whole." },
		{ "op.gt", "%r > %r{50}", Shape::Condition, "Operators", "" },
		{ "op.lt", "%r < %r{50}", Shape::Condition, "Operators", "" },
		{ "op.eq", "%r = %r{50}", Shape::Condition, "Operators", "Numbers, or texts (upper / lower case alike)." },
		{ "op.and", "%b and %b", Shape::Condition, "Operators", "" },
		{ "op.or", "%b or %b", Shape::Condition, "Operators", "" },
		{ "op.not", "not %b", Shape::Condition, "Operators", "" },
		{ "op.join", "join %s{apple } %s{banana}", Shape::Value, "Operators", "" },
		{ "op.letter", "letter %n{1} of %s{apple}", Shape::Value, "Operators", "" },
		{ "op.length", "length of %s{apple}", Shape::Value, "Operators", "" },
		{ "op.contains", "%s{apple} contains %s{a}?", Shape::Condition, "Operators", "" },
		{ "op.mod", "%n mod %n", Shape::Value, "Operators", "" },
		{ "op.round", "round %n", Shape::Value, "Operators", "" },
		{ "op.math", "%m{abs} of %n", Shape::Value, "Operators", "Angles in degrees.", Choices::Fixed,
		  "abs\nfloor\nceiling\nsqrt\nsin\ncos\ntan\nasin\nacos\natan\nln\nlog\ne ^\n10 ^" },

		// ---- Variables (and a reporter per variable : var.<id>) -------------------------------
		{ "var.set", "set %m to %r{0}", Shape::Statement, "Variables", "", Choices::Variables },
		{ "var.change", "change %m by %n{1}", Shape::Statement, "Variables", "", Choices::Variables },

		// ---- Actors ------------------------------------------------------------------------
		{ "ac.me", "myself", Shape::Value, "Actors", "" },
		{ "ac.other", "other actor", Shape::Value, "Actors", "The actor of the touch / bump that started this script." },
		{ "ac.find", "actor %s{id}", Shape::Value, "Actors", "By its id (Details)." },
		{ "ac.with_tag", "first actor with tag %s{player}", Shape::Value, "Actors", "" },
		{ "ac.count", "number of %s{Enemy}", Shape::Value, "Actors", "Actors of a class." },
		{ "ac.spawn", "spawn %s{Enemy} at x %n{0} y %n{0}", Shape::Statement, "Actors", "A class of Place Actors. \"last spawned\" is it." },
		{ "ac.spawn_here", "spawn %s{Bullet} here", Shape::Statement, "Actors", "\"last spawned\" is it." },
		{ "ac.spawned", "last spawned", Shape::Value, "Actors", "The actor made by the last spawn of this script." },
		{ "ac.destroy", "destroy myself", Shape::Cap, "Actors", "" },
		{ "ac.destroy_other", "destroy %r", Shape::Statement, "Actors", "" },
		{ "ac.lifetime", "destroy myself in %n{2} seconds", Shape::Statement, "Actors", "" },
		{ "ac.tag", "add tag %s{enemy}", Shape::Statement, "Actors", "" },
		{ "ac.untag", "remove tag %s{enemy}", Shape::Statement, "Actors", "" },
		{ "ac.set_of", "set %s{hp} of %r to %r{0}", Shape::Statement, "Actors", "x, y, z, a property (Details) or a variable of a Voxel Assembly actor." },
		{ "ac.call", "call %s{TakeDamage} of %r with %r{1}", Shape::Statement, "Actors", "A function of its Voxel Assembly, or a C++ function (HFUNCTION)." },
		{ "ac.collider", "add collider %n{1} by %n{1}", Shape::Statement, "Actors", "Blocks the other colliders and the walls." },
		{ "ac.trigger", "add trigger zone %n{1} by %n{1}", Shape::Statement, "Actors", "Overlaps without blocking (when something touches me)." },

		// ---- World -------------------------------------------------------------------------
		{ "wd.print", "print %r{Hello!}", Shape::Statement, "World", "In the Console." },
		{ "wd.shake", "shake the camera %n{1}", Shape::Statement, "World", "" },
		{ "wd.slowmo", "set time dilation to %n{1}", Shape::Statement, "World", "1 : normal, 0.5 : slow motion." },
		{ "wd.slowmo_for", "set time dilation to %n{0.3} for %n{0.5} s", Shape::Statement, "World", "" },
		{ "wd.widget", "show widget %s{ui/HUD.widget}", Shape::Statement, "World", "A widget of the Widget Editor, on the screen of the player." },

		// ---- Functions -----------------------------------------------------------------------
		{ "fn.entry", "when called", Shape::Hat, "Functions", "The start of the function (its inputs are in the palette)." },
		{ "fn.return", "return %r", Shape::Cap, "Functions", "Ends the function ; its value is the one of the function." },
	};

	inline constexpr std::size_t kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);

	constexpr bool SameName(const char* a, std::string_view b)
	{
		return std::string_view(a) == b;
	}

	/** Index of a kind in kKinds ; at compile time, an unknown name does not compile. */
	constexpr int KindIndex(std::string_view name)
	{
		for (std::size_t i = 0; i < kKindCount; ++i)
			if (SameName(kKinds[i].name, name))
				return static_cast<int>(i);
		throw std::logic_error("unknown Voxel Assembly kind");
	}

	/** Same, -1 when unknown (at run time). */
	inline int FindKind(std::string_view name)
	{
		for (std::size_t i = 0; i < kKindCount; ++i)
			if (SameName(kKinds[i].name, name))
				return static_cast<int>(i);
		return -1;
	}
}
