#pragma once

#include <array>
#include <span>

#include <windows.h>

#include <cfgmgr32.h>

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

	constexpr auto	maxDevices = 16;

	// No default member values: zero-initialised globals of these stay out of the exe
	struct Device
	{
		std::array<wchar_t, MAX_DEVICE_ID_LEN>	id;
		std::array<wchar_t, 128>				name;	// tooltip length
		Kind									kind;
		int										percent;
	};

	struct Devices
	{
		std::array<Device, maxDevices>	items;
		int								count;

		[[ nodiscard ]] std::span<const Device> list () const { return { items.data (), size_t ( count ) }; }
	};

	// window receives WM_DEVICECHANGE whenever a Bluetooth device connects or disconnects, or a radio comes or goes
	void watchConnections ( HWND window );

	// Feed WM_DEVICECHANGE here; true = the set of connected devices may have changed
	[[ nodiscard ]] bool onDeviceChange ( HWND window, WPARAM wParam, LPARAM lParam );

	// Connected Bluetooth input devices (keyboards, mice, gamepads) that report a battery level, sorted by name
	void scan ( Devices& devices );
}
//-----------------------------------------------------------------------------
