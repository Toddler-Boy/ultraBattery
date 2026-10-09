#pragma once

#include <string>
#include <vector>

#include <windows.h>

//-----------------------------------------------------------------------------

namespace battery
{
	enum class Kind
	{
		keyboard,
		mouse,
		gamepad,
		other
	};

	struct Device
	{
		std::wstring	id;
		std::wstring	name;
		Kind			kind = Kind::other;
		int				percent = 0;
	};

	// window receives WM_DEVICECHANGE whenever a Bluetooth device connects or disconnects, or a radio comes or goes
	void watchConnections ( HWND window );

	// Feed WM_DEVICECHANGE here; true = the set of connected devices may have changed
	[[ nodiscard ]] bool onDeviceChange ( HWND window, WPARAM wParam, LPARAM lParam );

	// Connected Bluetooth input devices (keyboards, mice, gamepads) that report a battery level, sorted by name
	[[ nodiscard ]] std::vector<Device> scan ();
}
//-----------------------------------------------------------------------------
