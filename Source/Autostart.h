#pragma once

//-----------------------------------------------------------------------------

// The HKCU Run entry. The first launch enables it, later launches keep its path current

namespace autostart
{
	void initialise ();

	[[ nodiscard ]] bool isEnabled ();
	void setEnabled ( bool enabled );
}
//-----------------------------------------------------------------------------
