#pragma once

// =============================================================================
// .sequence files (JSON) : a cinematic. Shared by the runtime module (player)
// and the editor module (sequencer).
// -----------------------------------------------------------------------------
// {
//   "length": 8,
//   "cuts":   [ { "t": 0, "camera": "cam_intro", "blend": 0 },        camera cuts : actor ids
//               { "t": 4, "camera": "cam_door",  "blend": 1.5 } ],     (CineCameraActor / any
//                                                                       actor with a Camera)
//   "tracks": [ { "actor": "door", "keys": [                           actor animation
//                 { "t": 0, "location": [0,0,0], "rotation": [0,0,0], "scale": [1,1,1], "interp": "smooth" },
//                 { "t": 3, "location": [0,8,0], ... } ] } ],
//   "events": [ { "t": 2.5, "name": "Explosion", "script": "print('boom')" } ],
//   "fades":  [ { "t": 0, "value": 1, "color": [0,0,0] }, { "t": 1, "value": 0 } ]
// }
//
// interp : "linear", "smooth" (ease in / out), "step" (holds until the next key).
// Events : SequenceEvents.OnSequenceEvent(name, sequence) to the actors that
// implement it, then the JS script.
// =============================================================================

#include <string>
#include <vector>

namespace sequence
{
	struct V3
	{
		float x = 0.f, y = 0.f, z = 0.f;
	};

	struct Cut
	{
		float t = 0.f;
		std::string camera;
		float blend = 0.f;
	};

	struct TransformKey
	{
		float t = 0.f;
		V3 location;
		V3 rotation;
		V3 scale{ 1.f, 1.f, 1.f };
		std::string interp = "smooth";
	};

	struct Track
	{
		std::string actor;
		std::vector<TransformKey> keys;
	};

	struct Event
	{
		float t = 0.f;
		std::string name;
		std::string script;
	};

	struct Fade
	{
		float t = 0.f;
		float value = 0.f;
		V3 color;
	};

	struct Sequence
	{
		float length = 5.f;
		std::vector<Cut> cuts;
		std::vector<Track> tracks;
		std::vector<Event> events;
		std::vector<Fade> fades;

		/** Keys sorted by time (after an edit). */
		void Sort();

		/** Index of the camera cut active at `t` (-1 : none). */
		int CutAt(float t) const;

		/** Fade at `t` (false : no fade key). */
		bool FadeAt(float t, float& value, V3& color) const;
	};

	/** Transform of a track at `t` (false : no key). */
	bool Sample(const Track& track, float t, V3& location, V3& rotation, V3& scale);

	bool Parse(const std::string& text, Sequence& out, std::string& error);
	std::string Serialize(const Sequence& sequence);
}
