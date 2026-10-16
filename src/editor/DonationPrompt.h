#pragma once

// =============================================================================
// "Buy me a coffee" : a small dialog asking for a donation (GitHub Sponsors).
// -----------------------------------------------------------------------------
//   - shown a few seconds after a project is opened, then again every
//     kRepeatMinutes of use (never while the game is playing) ;
//   - the amount is $5 by default, the user can pick or type another one
//     (remembered) ; "Donate" opens the GitHub Sponsors page in the browser ;
//   - "Don't show this again" stops it for good.
// Saved in %APPDATA%/Lynx/donation.cfg (~/.config/lynx/donation.cfg).
// =============================================================================

#include <Raven.h>

#include <functional>

namespace lynx::editor::donation
{
	constexpr const char* kSponsorUser = "oscar-soirey";
	constexpr int kDefaultAmount = 5;          // USD
	constexpr double kFirstDelaySeconds = 4.0; // after the project is opened
	constexpr double kRepeatMinutes = 60.0;    // then, while the editor is used

	/** After the editor window is shown. `busy` : true while it must not appear (playing...). */
	void Start(Rid window, std::function<bool()> busy = {});

	/** A good moment (a game was shipped...) : shown if it was not shown in the last 20 minutes. */
	void Moment();

	/** Shows it now (a "Support Lynx" menu item), even after "Don't show this again". */
	void Show();
}
