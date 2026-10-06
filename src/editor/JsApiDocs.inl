// =============================================================================
// Lynx JavaScript API : signatures and documentation (language service)
// -----------------------------------------------------------------------------
// From scripting/README.md. `owner` : "" (global), a namespace object (Level,
// Input...) or a type of value (Actor, Component types, Vec3...). `returns` :
// the type of the result, used to complete what follows :
//   "Actor", "Vec3", "Transform", "PlayerController", "Widget", "Blackboard",
//   "Component:<first argument>" (addComponent("SoundSource") -> SoundSource),
//   "Array", "" (nothing known).
// The members of the engine objects are also read from the running scripting
// context (Object.getOwnPropertyNames) : this table adds the documentation.
// =============================================================================

struct ApiEntry
{
	const char* owner;
	const char* name;
	const char* signature;   // "spawn(className, options?)" ; property : "name : type"
	const char* doc;
	const char* returns;
	bool function;
};

static const ApiEntry kApiEntries[] = {
	// --- Globals -----------------------------------------------------------------
	{ "", "print", "print(...values)", "Prints the values in the editor console.", "", true },
	{ "", "vec3", "vec3(x, y, z)", "A vector {x, y, z}. vec3(s) : the same value 3 times.", "Vec3", true },
	{ "", "vec2", "vec2(x, y)", "A vector {x, y}.", "Vec2", true },
	{ "", "parent", "parent : Actor", "The actor this script is attached to.", "Actor", false },
	{ "", "Level", "Level", "The current level : spawn, find and destroy actors.", "", false },
	{ "", "Input", "Input", "Input actions and axes of input.json (Input Settings).", "", false },
	{ "", "Engine", "Engine", "Time dilation, players, render size.", "", false },
	{ "", "UI", "UI", "Widgets (.widget files of the Widget Editor).", "", false },
	{ "", "BT", "BT", "Behavior trees : BT.Success, BT.Failure, BT.Running.", "", false },
	{ "", "Actor", "class Actor", "Base class of the actors. class Enemy extends Actor { ... }", "", false },
	{ "", "UserWidget", "class UserWidget", "Base class of the widget classes : Construct(), Tick(dt), Destruct().", "", false },
	{ "", "BTTask", "class BTTask", "Behavior tree task written in JS : Execute(dt), Tick(dt), Abort().", "", false },
	{ "", "BTDecorator", "class BTDecorator", "Behavior tree decorator written in JS : Check().", "", false },
	{ "", "BTService", "class BTService", "Behavior tree service written in JS : Activated(), Tick(dt), Deactivated().", "", false },

	// --- Lifecycle (functions of a script / methods of a class) --------------------
	{ "@lifecycle", "BeginPlay", "BeginPlay()", "Called when the game starts (or when the actor is spawned while it runs).", "", true },
	{ "@lifecycle", "Update", "Update(dt)", "Called every game tick. dt : seconds since the last tick.", "", true },
	{ "@lifecycle", "EndPlay", "EndPlay()", "Called when the game stops, the script is removed or the actor is destroyed.", "", true },
	{ "@lifecycle", "OnBeginOverlap", "OnBeginOverlap(other)", "A BoxCollider of the actor starts overlapping another actor.", "", true },
	{ "@lifecycle", "OnEndOverlap", "OnEndOverlap(other)", "A BoxCollider of the actor stops overlapping another actor.", "", true },
	{ "@lifecycle", "OnPossessed", "OnPossessed(player)", "A player possesses this actor.", "", true },
	{ "@lifecycle", "OnUnpossessed", "OnUnpossessed(player)", "The player releases this actor.", "", true },
	{ "@lifecycle", "ProcessInput", "ProcessInput(player)", "Every game frame, only while the actor is possessed.", "", true },

	// --- console ---------------------------------------------------------------------
	{ "console", "log", "log(...values)", "Prints in the editor console.", "", true },
	{ "console", "warn", "warn(...values)", "Prints a warning in the editor console.", "", true },
	{ "console", "error", "error(...values)", "Prints an error in the editor console.", "", true },

	// --- Level -----------------------------------------------------------------------
	{ "Level", "spawn", "spawn(className, options?)", "Spawns an actor of this class. options : {position, rotation, scale} or {x, y, z}.", "Actor", true },
	{ "Level", "find", "find(id)", "The actor with this object id (or null).", "Actor", true },
	{ "Level", "findWithTag", "findWithTag(tag)", "The actors that have this tag.", "Array", true },
	{ "Level", "all", "all()", "Every actor of the level.", "Array", true },
	{ "Level", "count", "count(className?)", "Number of actors (of this class).", "", true },
	{ "Level", "destroy", "destroy(actor)", "Destroys the actor at the end of the frame.", "", true },

	// --- Input -----------------------------------------------------------------------
	{ "Input", "pressed", "pressed(action)", "The action was pressed this frame (input.json).", "", true },
	{ "Input", "held", "held(action)", "The action is held down.", "", true },
	{ "Input", "released", "released(action)", "The action was released this frame.", "", true },
	{ "Input", "axis", "axis(axis)", "Value of the axis, between -1 and 1 (input.json).", "", true },

	// --- Engine ----------------------------------------------------------------------
	{ "Engine", "getTimeDilation", "getTimeDilation()", "Speed of the game time (1 : normal).", "", true },
	{ "Engine", "setTimeDilation", "setTimeDilation(value, duration?)", "Changes the speed of the game time (for `duration` seconds).", "", true },
	{ "Engine", "isPlaying", "isPlaying()", "The game is running (not only the editor).", "", true },
	{ "Engine", "createPlayer", "createPlayer()", "Adds a player (split screen). Destroyed at the end of the game if created during it.", "PlayerController", true },
	{ "Engine", "destroyPlayer", "destroyPlayer(player)", "Removes a player.", "", true },
	{ "Engine", "getPlayer", "getPlayer(index)", "The player with this index.", "PlayerController", true },
	{ "Engine", "players", "players : PlayerController[]", "Every player.", "Array", false },
	{ "Engine", "playerCount", "playerCount : number", "Number of players.", "", false },
	{ "Engine", "defaultPlayer", "defaultPlayer : PlayerController", "Player 0 (always there).", "PlayerController", false },
	{ "Engine", "renderSize", "renderSize : {x, y}", "Size of the render, in pixels.", "Vec2", false },

	// --- UI --------------------------------------------------------------------------
	{ "UI", "create", "create(path, player?, widgetClass?)", "Creates a widget from a .widget file.", "Widget", true },
	{ "UI", "destroy", "destroy(widget)", "Destroys a widget.", "", true },
	{ "UI", "all", "all()", "Every widget.", "Array", true },
	{ "UI", "classes", "classes()", "Names of the JS widget classes.", "Array", true },
	{ "UI", "getDPIReference", "getDPIReference()", "Reference resolution of the UI scale.", "", true },
	{ "UI", "setDPIReference", "setDPIReference(value)", "Changes the reference resolution of the UI scale.", "", true },

	// --- BT ----------------------------------------------------------------------------
	{ "BT", "Success", "Success", "A node succeeded.", "", false },
	{ "BT", "Failure", "Failure", "A node failed.", "", false },
	{ "BT", "Running", "Running", "A task is not finished (Tick is called again).", "", false },

	// --- Actor (parent, this in an actor class, Level.find...) -------------------------
	{ "Actor", "id", "id : string", "Object id of the actor (can be changed).", "", false },
	{ "Actor", "className", "className : string", "Class of the actor.", "", false },
	{ "Actor", "valid", "valid : boolean", "false once the actor is destroyed.", "", false },
	{ "Actor", "entity", "entity : number", "EnTT id of the actor.", "", false },
	{ "Actor", "transform", "transform : {position, rotation, scale}", "Live transform (partial assignment allowed).", "Transform", false },
	{ "Actor", "position", "position : Vec3", "Shortcut of transform.position (live : position.x += 1).", "Vec3", false },
	{ "Actor", "velocity", "velocity : Vec3", "VelocityComponent (created when written).", "Vec3", false },
	{ "Actor", "angularVelocity", "angularVelocity : Vec3", "VelocityComponent : rotation speed.", "Vec3", false },
	{ "Actor", "damping", "damping : number", "VelocityComponent : slows the velocity down.", "", false },
	{ "Actor", "lifetime", "lifetime : number | null", "Seconds before the actor is destroyed (null : forever).", "", false },
	{ "Actor", "tags", "tags : string[]", "Tags of the actor.", "Array", false },
	{ "Actor", "scripts", "scripts : string[]", "Scripts attached to the actor.", "Array", false },
	{ "Actor", "components", "components : string[]", "Types of the components of the actor.", "Array", false },
	{ "Actor", "controller", "controller : PlayerController | null", "The player that possesses the actor.", "PlayerController", false },
	{ "Actor", "isPossessed", "isPossessed : boolean", "A player possesses the actor.", "", false },
	{ "Actor", "autoPossessPlayer", "autoPossessPlayer : number", "Player that possesses the actor when the game starts (-1 : none).", "", false },
	{ "Actor", "getProperty", "getProperty(name)", "Value of a C++ property (HPROPERTY).", "", true },
	{ "Actor", "setProperty", "setProperty(name, value)", "Changes a C++ property (HPROPERTY).", "", true },
	{ "Actor", "hasProperty", "hasProperty(name)", "The C++ class has this property.", "", true },
	{ "Actor", "addTag", "addTag(tag)", "Adds a tag (TagsComponent).", "", true },
	{ "Actor", "removeTag", "removeTag(tag)", "Removes a tag.", "", true },
	{ "Actor", "hasTag", "hasTag(tag)", "The actor has this tag.", "", true },
	{ "Actor", "addScript", "addScript(path)", "Attaches a script (path in the assets).", "", true },
	{ "Actor", "removeScript", "removeScript(path)", "Removes a script.", "", true },
	{ "Actor", "hasScript", "hasScript(path)", "The script is attached.", "", true },
	{ "Actor", "call", "call(name, ...args)", "Calls a function of the scripts / class of this actor.", "", true },
	{ "Actor", "callNative", "callNative(name, ...args)", "Calls the C++ function (HFUNCTION) even if a JS method has the same name.", "", true },
	{ "Actor", "destroy", "destroy()", "Destroys the actor at the end of the frame.", "", true },
	{ "Actor", "addComponent", "addComponent(type, options?)", "Adds a component (or returns the one already there) and applies `options`.\nTypes : StaticSprite, AnimationSprite, Camera, BoxCollider, SoundSource, Light, Velocity, Lifetime, AI.", "Component:0", true },
	{ "Actor", "getComponent", "getComponent(type)", "The component of this type (or null).", "Component:0", true },
	{ "Actor", "hasComponent", "hasComponent(type)", "The actor has a component of this type.", "", true },
	{ "Actor", "removeComponent", "removeComponent(type)", "Removes the component (EndPlay is called).", "", true },

	// --- Values ------------------------------------------------------------------------
	{ "Vec3", "x", "x : number", "", "", false },
	{ "Vec3", "y", "y : number", "", "", false },
	{ "Vec3", "z", "z : number", "", "", false },
	{ "Vec3", "set", "set(x, y, z)", "Changes the 3 values (live vectors of the transform).", "", true },
	{ "Vec3", "clone", "clone()", "A copy, not linked to the actor any more.", "Vec3", true },
	{ "Vec2", "x", "x : number", "", "", false },
	{ "Vec2", "y", "y : number", "", "", false },
	{ "Transform", "position", "position : Vec3", "Live position (alias : location).", "Vec3", false },
	{ "Transform", "location", "location : Vec3", "Same as position.", "Vec3", false },
	{ "Transform", "rotation", "rotation : Vec3", "Live rotation, in degrees.", "Vec3", false },
	{ "Transform", "scale", "scale : Vec3", "Live scale (negative X : flipped).", "Vec3", false },

	// --- Components (common) -------------------------------------------------------------
	{ "Component", "owner", "owner : Actor", "The actor of the component.", "Actor", false },
	{ "Component", "type", "type : string", "Type of the component.", "", false },
	{ "Component", "valid", "valid : boolean", "false once removed or once the actor is destroyed.", "", false },
	{ "Component", "tickEnabled", "tickEnabled : boolean", "Tick is called during the game.", "", false },
	{ "Component", "hasBegunPlay", "hasBegunPlay : boolean", "BeginPlay was called.", "", false },

	// StaticSprite / AnimationSprite
	{ "StaticSprite", "texture", "texture : string", "Image of the sprite (path in the assets).", "", false },
	{ "StaticSprite", "region", "region : {x: u0, y: v0, z: u1, w: v1}", "Part of the image.", "", false },
	{ "StaticSprite", "size", "size : {x, y}", "World size (before the scale of the actor).", "Vec2", false },
	{ "StaticSprite", "offset", "offset : Vec3", "Offset from the actor (follows the flip). A copy : assign a new vector.", "Vec3", false },
	{ "StaticSprite", "visible", "visible : boolean", "", "", false },
	{ "StaticSprite", "flipX", "flipX : boolean", "", "", false },
	{ "StaticSprite", "flipY", "flipY : boolean", "", "", false },
	{ "AnimationSprite", "size", "size : {x, y}", "World size (before the scale of the actor).", "Vec2", false },
	{ "AnimationSprite", "offset", "offset : Vec3", "Offset from the actor.", "Vec3", false },
	{ "AnimationSprite", "visible", "visible : boolean", "", "", false },
	{ "AnimationSprite", "flipX", "flipX : boolean", "", "", false },
	{ "AnimationSprite", "flipY", "flipY : boolean", "", "", false },
	{ "AnimationSprite", "texture", "texture : string", "Horizontal sheet of frameCount images (single mode).", "", false },
	{ "AnimationSprite", "frameCount", "frameCount : number", "", "", false },
	{ "AnimationSprite", "frameTime", "frameTime : number", "Seconds per frame.", "", false },
	{ "AnimationSprite", "loop", "loop : boolean", "", "", false },
	{ "AnimationSprite", "autoPlay", "autoPlay : boolean", "", "", false },
	{ "AnimationSprite", "mode", "mode : \"single\" | \"stateMachine\"", "", "", false },
	{ "AnimationSprite", "frame", "frame : number", "Current frame.", "", false },
	{ "AnimationSprite", "playing", "playing : boolean", "", "", false },
	{ "AnimationSprite", "finished", "finished : boolean", "", "", false },
	{ "AnimationSprite", "setAnimation", "setAnimation(texture, frames, frameTime, loop)", "Single animation.", "", true },
	{ "AnimationSprite", "onFrame", "onFrame(frame, fn)", "Calls fn when this frame is shown.", "", true },
	{ "AnimationSprite", "onFinished", "onFinished(fn)", "Calls fn at the end of the animation.", "", true },
	{ "AnimationSprite", "play", "play()", "", "", true },
	{ "AnimationSprite", "pause", "pause()", "", "", true },
	{ "AnimationSprite", "stop", "stop()", "", "", true },
	{ "AnimationSprite", "restart", "restart()", "", "", true },
	{ "AnimationSprite", "addAnimation", "addAnimation(name, texture, frames, loop?, frameTime?)", "State machine : an animation.", "", true },
	{ "AnimationSprite", "addBlendSpace", "addBlendSpace(name, samples)", "State machine : [[position, animation], ...] chosen by a float parameter.", "", true },
	{ "AnimationSprite", "addState", "addState(name, animation, options?)", "State machine : options {variable, interruptible, restartOnEnter, next, onEnter, onExit}.", "", true },
	{ "AnimationSprite", "setDefaultState", "setDefaultState(name)", "", "", true },
	{ "AnimationSprite", "addTransition", "addTransition(from, to, condition, priority?, waitEnd?)", "Condition : (sprite) => bool, {param, op, value}, {isTrue}, {isFalse}, {trigger}.", "", true },
	{ "AnimationSprite", "addAnyTransition", "addAnyTransition(to, condition, priority?)", "Transition tested from any state.", "", true },
	{ "AnimationSprite", "setFloat", "setFloat(name, value)", "Parameter of the state machine / anim graph.", "", true },
	{ "AnimationSprite", "setBool", "setBool(name, value)", "", "", true },
	{ "AnimationSprite", "setInt", "setInt(name, value)", "", "", true },
	{ "AnimationSprite", "getFloat", "getFloat(name)", "", "", true },
	{ "AnimationSprite", "getBool", "getBool(name)", "", "", true },
	{ "AnimationSprite", "trigger", "trigger(name)", "Fires a trigger (consumed by the transition that uses it).", "", true },
	{ "AnimationSprite", "setTrigger", "setTrigger(name)", "Same as trigger(name).", "", true },
	{ "AnimationSprite", "forceState", "forceState(name)", "", "", true },
	{ "AnimationSprite", "currentState", "currentState : string", "", "", false },
	{ "AnimationSprite", "graph", "graph : string", "Anim graph asset (.animgraph).", "", false },
	{ "AnimationSprite", "loadGraph", "loadGraph(path)", "Loads an .animgraph (false : unreadable).", "", true },
	{ "AnimationSprite", "loadedGraph", "loadedGraph : string", "", "", false },
	{ "AnimationSprite", "onAnimationFrame", "onAnimationFrame(animation, frame, fn)", "", "", true },
	{ "AnimationSprite", "onAnimationFinished", "onAnimationFinished(animation, fn)", "", "", true },

	// Camera
	{ "Camera", "offset", "offset : Vec3", "Offset from the actor.", "Vec3", false },
	{ "Camera", "rotation", "rotation : Vec3", "Degrees (default (0, -90, 0)).", "Vec3", false },
	{ "Camera", "fov", "fov : number", "", "", false },
	{ "Camera", "near", "near : number", "", "", false },
	{ "Camera", "far", "far : number", "", "", false },
	{ "Camera", "followSpeed", "followSpeed : number", "0 : sticks to the actor.", "", false },
	{ "Camera", "useCameraShake", "useCameraShake : boolean", "", "", false },
	{ "Camera", "autoActivate", "autoActivate : boolean", "Becomes the view when the game starts.", "", false },
	{ "Camera", "active", "active : boolean", "", "", false },
	{ "Camera", "activate", "activate()", "Becomes the camera of the view.", "", true },
	{ "Camera", "activateFor", "activateFor(player)", "Becomes the view of this player.", "", true },
	{ "Camera", "snap", "snap()", "Jumps to its target (no smoothing).", "", true },

	// BoxCollider
	{ "BoxCollider", "size", "size : {x, y}", "", "Vec2", false },
	{ "BoxCollider", "offset", "offset : Vec3", "", "Vec3", false },
	{ "BoxCollider", "trigger", "trigger : boolean", "A trigger never blocks.", "", false },
	{ "BoxCollider", "layer", "layer : number", "Two boxes interact if a.layer & b.mask and b.layer & a.mask.", "", false },
	{ "BoxCollider", "mask", "mask : number", "", "", false },
	{ "BoxCollider", "collideWithVoxels", "collideWithVoxels : boolean", "", "", false },
	{ "BoxCollider", "voxelFlags", "voxelFlags : string[]", "Blocking voxels (default : solid voxels).", "Array", false },
	{ "BoxCollider", "debugDraw", "debugDraw : boolean", "", "", false },
	{ "BoxCollider", "moveAndCollide", "moveAndCollide(delta)", "Moves the actor, stopping against voxels and boxes. Result : {blockedX, blockedY, ...}.", "", true },
	{ "BoxCollider", "onBeginOverlap", "onBeginOverlap(fn)", "fn(other) when another actor starts overlapping.", "", true },
	{ "BoxCollider", "onEndOverlap", "onEndOverlap(fn)", "", "", true },
	{ "BoxCollider", "overlapping", "overlapping()", "Actors overlapping the box.", "Array", true },
	{ "BoxCollider", "overlapsVoxels", "overlapsVoxels()", "", "", true },
	{ "BoxCollider", "isOverlapping", "isOverlapping(actor)", "", "", true },

	// SoundSource
	{ "SoundSource", "sound", "sound : string", "Sound file (path in the assets).", "", false },
	{ "SoundSource", "spatial", "spatial : boolean", "3D sound that follows the actor (false : 2D).", "", false },
	{ "SoundSource", "loop", "loop : boolean", "", "", false },
	{ "SoundSource", "playOnBegin", "playOnBegin : boolean", "", "", false },
	{ "SoundSource", "volume", "volume : number", "", "", false },
	{ "SoundSource", "pitch", "pitch : number", "", "", false },
	{ "SoundSource", "volumeVariation", "volumeVariation : number", "", "", false },
	{ "SoundSource", "pitchVariation", "pitchVariation : number", "", "", false },
	{ "SoundSource", "referenceDistance", "referenceDistance : number", "", "", false },
	{ "SoundSource", "maxDistance", "maxDistance : number", "", "", false },
	{ "SoundSource", "rolloff", "rolloff : number", "", "", false },
	{ "SoundSource", "playing", "playing : boolean", "", "", false },
	{ "SoundSource", "play", "play()", "", "", true },
	{ "SoundSource", "stop", "stop()", "", "", true },
	{ "SoundSource", "playAt", "playAt(position)", "Plays the sound at a position of the world.", "", true },

	// Light
	{ "Light", "type", "type : \"point\" | \"spot\" | \"directional\" | \"sky\"", "", "", false },
	{ "Light", "color", "color : Vec3", "", "Vec3", false },
	{ "Light", "intensity", "intensity : number", "", "", false },
	{ "Light", "offset", "offset : Vec3", "", "Vec3", false },
	{ "Light", "enabled", "enabled : boolean", "", "", false },

	// AI
	{ "AI", "behaviorTree", "behaviorTree : string", "Behavior tree asset (.bt) ; changing it reloads the tree.", "", false },
	{ "AI", "startOnBegin", "startOnBegin : boolean", "", "", false },
	{ "AI", "running", "running : boolean", "", "", false },
	{ "AI", "activeNodes", "activeNodes : string[]", "", "Array", false },
	{ "AI", "loadedTree", "loadedTree : string", "", "", false },
	{ "AI", "start", "start()", "", "", true },
	{ "AI", "stop", "stop()", "Sends Abort to the running task.", "", true },
	{ "AI", "restart", "restart()", "", "", true },
	{ "AI", "load", "load(asset)", "", "", true },
	{ "AI", "blackboard", "blackboard : Blackboard", "Memory of the AI.", "Blackboard", false },
	{ "Blackboard", "set", "set(key, value)", "Actor, number, bool, text or vector {x, y, z}.", "", true },
	{ "Blackboard", "get", "get(key, defaultValue?)", "undefined when absent.", "", true },
	{ "Blackboard", "has", "has(key)", "", "", true },
	{ "Blackboard", "clear", "clear(key?)", "Removes a key (or every key).", "", true },
	{ "Blackboard", "keys", "keys()", "", "Array", true },

	// Velocity / Lifetime
	{ "Velocity", "velocity", "velocity : Vec3", "", "Vec3", false },
	{ "Velocity", "angularVelocity", "angularVelocity : Vec3", "", "Vec3", false },
	{ "Velocity", "damping", "damping : number", "", "", false },
	{ "Lifetime", "lifetime", "lifetime : number", "Seconds before the actor is destroyed.", "", false },

	// --- PlayerController ------------------------------------------------------------
	{ "PlayerController", "possess", "possess(actor)", "Takes control of the actor (its previous player releases it).", "", true },
	{ "PlayerController", "unpossess", "unpossess()", "", "", true },
	{ "PlayerController", "possessed", "possessed : Actor | null", "Alias : pawn.", "Actor", false },
	{ "PlayerController", "pawn", "pawn : Actor | null", "", "Actor", false },
	{ "PlayerController", "setViewTarget", "setViewTarget(actor)", "Its CameraComponent becomes the view of this player.", "", true },
	{ "PlayerController", "setViewportSize", "setViewportSize(x, y, width, height)", "Fixed viewport (normalized). resetViewportSize() : automatic.", "", true },
	{ "PlayerController", "resetViewportSize", "resetViewportSize()", "", "", true },
	{ "PlayerController", "createWidget", "createWidget(path)", "= UI.create(path, player).", "Widget", true },
	{ "PlayerController", "index", "index : number", "", "", false },
	{ "PlayerController", "id", "id : number", "", "", false },
	{ "PlayerController", "isDefault", "isDefault : boolean", "", "", false },
	{ "PlayerController", "valid", "valid : boolean", "", "", false },
	{ "PlayerController", "viewportRect", "viewportRect : {x, y, width, height}", "", "", false },

	// --- Widgets -----------------------------------------------------------------------
	{ "Widget", "find", "find(name)", "A widget of the tree, by name.", "Widget", true },
	{ "Widget", "set", "set(fields)", "Changes several fields : {text, fontSize, color...}.", "Widget", true },
	{ "Widget", "addToViewport", "addToViewport(zOrder?)", "", "", true },
	{ "Widget", "removeFromParent", "removeFromParent()", "", "", true },
	{ "Widget", "destroy", "destroy()", "", "", true },
	{ "Widget", "addChild", "addChild(classOrPath, options?)", "", "Widget", true },
	{ "Widget", "insertChildAt", "insertChildAt(index, className, options?)", "", "Widget", true },
	{ "Widget", "removeChild", "removeChild(widget)", "", "", true },
	{ "Widget", "clearChildren", "clearChildren()", "", "", true },
	{ "Widget", "getChildAt", "getChildAt(index)", "", "Widget", true },
	{ "Widget", "onClicked", "onClicked(fn)", "Button.", "", true },
	{ "Widget", "onPressed", "onPressed(fn)", "Button.", "", true },
	{ "Widget", "onReleased", "onReleased(fn)", "Button.", "", true },
	{ "Widget", "onHovered", "onHovered(fn)", "Button.", "", true },
	{ "Widget", "onUnhovered", "onUnhovered(fn)", "Button.", "", true },
	{ "Widget", "onValueChanged", "onValueChanged(fn)", "Slider.", "", true },
	{ "Widget", "onCheckStateChanged", "onCheckStateChanged(fn)", "CheckBox.", "", true },
	{ "Widget", "on", "on(event, fn)", "Returns an id for off(event, id).", "", true },
	{ "Widget", "off", "off(event, id)", "", "", true },
	{ "Widget", "name", "name : string", "", "", false },
	{ "Widget", "className", "className : string", "", "", false },
	{ "Widget", "parent", "parent : Widget", "", "Widget", false },
	{ "Widget", "children", "children : Widget[]", "", "Array", false },
	{ "Widget", "childCount", "childCount : number", "", "", false },
	{ "Widget", "root", "root : Widget", "", "Widget", false },
	{ "Widget", "visible", "visible : boolean", "Shortcut of visibility Visible / Collapsed.", "", false },
	{ "Widget", "visibility", "visibility : string", "Visible, Collapsed, Hidden, HitTestInvisible, SelfHitTestInvisible.", "", false },
	{ "Widget", "text", "text : string", "", "", false },
	{ "Widget", "fontSize", "fontSize : number", "", "", false },
	{ "Widget", "color", "color : string | {r, g, b, a}", "", "", false },
	{ "Widget", "percent", "percent : number", "ProgressBar.", "", false },
	{ "Widget", "renderOpacity", "renderOpacity : number", "", "", false },
	{ "Widget", "isEnabled", "isEnabled : boolean", "", "", false },
	{ "Widget", "hovered", "hovered : boolean", "", "", false },
	{ "Widget", "slot", "slot", "Layout of the widget in its parent (offsets, anchors, padding...).", "", false },
	{ "Widget", "owningPlayer", "owningPlayer : PlayerController", "", "PlayerController", false },
	{ "Widget", "valid", "valid : boolean", "", "", false },
	{ "Widget", "inViewport", "inViewport : boolean", "", "", false },

	// --- Nodes of behavior trees written in JS (this in BTTask / BTDecorator / BTService)
	{ "BTNode", "owner", "owner : Actor", "The actor that runs the tree.", "Actor", false },
	{ "BTNode", "blackboard", "blackboard : Blackboard", "", "Blackboard", false },
	{ "BTNode", "params", "params : object", "The other attributes of the node in the .bt file.", "", false },
	{ "BTNode", "name", "name : string", "The class of the node.", "", false },
};

// Types of component (addComponent("...")).
static const char* const kComponentTypes[] = {
	"StaticSprite", "AnimationSprite", "Camera", "BoxCollider", "SoundSource", "Light", "Velocity", "Lifetime", "AI",
};

// JavaScript built-ins, when the scripting context can't be read.
static const char* const kBuiltinGlobals[] = {
	"Object", "Function", "Array", "Number", "String", "Boolean", "Symbol", "BigInt", "Math", "JSON", "Date", "RegExp",
	"Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError", "EvalError", "URIError", "AggregateError",
	"Promise", "Map", "Set", "WeakMap", "WeakSet", "WeakRef", "FinalizationRegistry", "Proxy", "Reflect", "ArrayBuffer",
	"SharedArrayBuffer", "DataView", "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
	"Int32Array", "Uint32Array", "Float32Array", "Float64Array", "BigInt64Array", "BigUint64Array", "Atomics",
	"globalThis", "parseInt", "parseFloat", "isNaN", "isFinite", "encodeURI", "encodeURIComponent", "decodeURI",
	"decodeURIComponent", "escape", "unescape", "eval", "queueMicrotask", "structuredClone",
	"print", "console", "vec3", "vec2", "Level", "Input", "Engine", "UI", "BT", "Actor", "UserWidget", "BTTask",
	"BTDecorator", "BTService", "PointLightActor", "SpotLightActor", "DirectionalLightActor", "SkyLightActor",
	"SpriteActor", "SoundActor",
};

// Members of every object (Object.prototype).
static const char* const kObjectMembers[] = {
	"hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable", "toString", "toLocaleString", "valueOf",
};

static const char* const kKeywords[] = {
	"async", "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do",
	"else", "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "let",
	"new", "null", "of", "return", "static", "super", "switch", "this", "throw", "true", "try", "typeof",
	"undefined", "var", "void", "while", "with", "yield", "get", "set",
};
