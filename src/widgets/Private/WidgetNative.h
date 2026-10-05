#pragma once

// =============================================================================
// Native widget (renderer side) : the only place with HRL types. Included by
// the widget .cpp files, never by a public header.
// =============================================================================

#include <hrl/hrl.h>

#include <string>

#include "../../core/Common.h"

namespace lynx
{
	struct WidgetNative
	{
		HRL_id id = HRL_INVALID_ID;
		int type = -1;
		bool hovered = false;
		bool interactive = true;     // computed by the system (visibility, enabled)
	};

	namespace ui::native
	{
		// Events queued by the renderer callbacks, dispatched by WidgetSystem::Tick.
		enum Event : int
		{
			kPressed = 0,
			kReleased,
			kHovered,
			kUnhovered,
			kValueChanged,     // slider : value
			kCheckChanged,     // checkbox : value 0 / 1
		};

		/** Texture of an asset ("" or missing = a white texture, for tints). */
		HRL_id Texture(const std::string& path);
		/** Font of an asset (HRL_INVALID_ID = default font). */
		HRL_id Font(const std::string& path);

		/** Queues an event for the widget with this serial. */
		void PushEvent(uint64_t serial, int event, float value);

		/** user_data of the HRL callbacks <-> serial of the widget. */
		inline void* ToUserData(uint64_t serial) { return reinterpret_cast<void*>(static_cast<uintptr_t>(serial)); }
		inline uint64_t FromUserData(void* data) { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(data)); }
	}
}
