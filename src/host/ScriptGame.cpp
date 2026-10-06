// =============================================================================
// LynxScriptGame.dll : the game DLL of the projects without C++
// -----------------------------------------------------------------------------
// Built with the engine (<editor>/scriptgame/). A project with no Build.bat /
// CMakeLists.txt and no DLL in build/ loads it (GameProject::script_only) :
// nothing is compiled, the game is made of JavaScript classes and scripts,
// the engine actors (Humanoid, lights, colliders...) and the plugins.
// It registers no class of its own.
// =============================================================================

#include <Lynx.h>

LYNX_LINK_MODULE( )
