#pragma once

#include <windows.h>

#include "Battery.h"

//-----------------------------------------------------------------------------

namespace batteryIcon
{
	// The percentage as large as fits, with a small device symbol underneath; caller destroys it
	[[ nodiscard ]] HICON create ( int percent, battery::Kind kind, int size, bool lightTaskbar );
}
//-----------------------------------------------------------------------------
