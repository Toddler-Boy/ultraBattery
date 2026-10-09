#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include "Autostart.h"
#include "Battery.h"
#include "GUI_BatteryIcon.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	trayMessage = UINT ( WM_APP + 1 );
	constexpr auto	pollTimer = UINT ( 1 );
	constexpr auto	pollMilliseconds = UINT ( 60 * 1000 );
	constexpr auto	connectionTimer = UINT ( 2 );
	constexpr auto	connectionSettleMilliseconds = UINT ( 2000 );
	constexpr auto	menuRefresh = UINT ( 1 );
	constexpr auto	menuAutostart = UINT ( 2 );
	constexpr auto	menuExit = UINT ( 3 );

	struct TrayEntry
	{
		std::wstring	id;
		UINT			uid = 0;
		int				percent = -1;
	};

	auto	entries = std::vector<TrayEntry> {};
	auto	taskbarCreated = UINT ( 0 );

	// Windows remembers "show on taskbar" per exe path and uid, so a reconnecting device reuses a known uid
	UINT freeUid ()
	{
		auto	uid = UINT ( 1 );
		while ( std::ranges::contains ( entries, uid, &TrayEntry::uid ) )
			++uid;
		return uid;
	}

	bool lightTaskbar ()
	{
		auto	light = DWORD ( 0 );
		auto	size = DWORD ( sizeof ( light ) );
		RegGetValueW ( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme",
					   RRF_RT_REG_DWORD, nullptr, &light, &size );
		return light != 0;
	}

	NOTIFYICONDATAW iconData ( HWND window, UINT uid )
	{
		auto	data = NOTIFYICONDATAW {};
		data.cbSize = sizeof ( data );
		data.hWnd = window;
		data.uID = uid;
		return data;
	}

	// Modify falls back to add, which also covers a failed add at logon and a restarted Explorer
	void show ( HWND window, TrayEntry& entry, const battery::Device& device, int iconSize, bool light )
	{
		auto	data = iconData ( window, entry.uid );
		data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
		data.uCallbackMessage = trayMessage;
		data.hIcon = batteryIcon::create ( device.percent, device.kind, iconSize, light );
		wcsncpy_s ( data.szTip, ( device.name + L": " + std::to_wstring ( device.percent ) + L"%" ).c_str (), _TRUNCATE );

		auto	shown = Shell_NotifyIconW ( NIM_MODIFY, &data ) != FALSE;
		if ( ! shown && Shell_NotifyIconW ( NIM_ADD, &data ) != FALSE )
		{
			data.uVersion = NOTIFYICON_VERSION_4;
			Shell_NotifyIconW ( NIM_SETVERSION, &data );
			shown = true;
		}

		DestroyIcon ( data.hIcon );
		entry.percent = shown ? device.percent : -1;
	}

	void remove ( HWND window, const TrayEntry& entry )
	{
		auto	data = iconData ( window, entry.uid );
		Shell_NotifyIconW ( NIM_DELETE, &data );
	}

	void refresh ( HWND window, bool redraw )
	{
		const auto	devices = battery::scan ();

		std::erase_if ( entries, [ & ] ( const TrayEntry& entry )
		{
			if ( std::ranges::contains ( devices, entry.id, &battery::Device::id ) )
				return false;

			remove ( window, entry );
			return true;
		} );

		const auto	iconSize = GetSystemMetricsForDpi ( SM_CXSMICON, GetDpiForWindow ( window ) );
		const auto	light = lightTaskbar ();

		for ( const auto& device : devices )
		{
			auto	it = std::ranges::find ( entries, device.id, &TrayEntry::id );
			if ( it == entries.end () )
				it = entries.insert ( entries.end (), { device.id, freeUid () } );

			if ( redraw || it->percent != device.percent )
				show ( window, *it, device, iconSize, light );
		}
	}

	// anchor = where the shell wants the menu, the cursor or the icon when opened by keyboard
	void showMenu ( HWND window, POINT anchor )
	{
		const auto	menu = CreatePopupMenu ();
		AppendMenuW ( menu, MF_STRING | MF_GRAYED, 0, L"ultraBattery " APP_VERSION );
		AppendMenuW ( menu, MF_SEPARATOR, 0, nullptr );
		AppendMenuW ( menu, MF_STRING, menuRefresh, L"Refresh" );
		AppendMenuW ( menu, MF_STRING | ( autostart::isEnabled () ? MF_CHECKED : MF_UNCHECKED ), menuAutostart, L"Start with Windows" );
		AppendMenuW ( menu, MF_SEPARATOR, 0, nullptr );
		AppendMenuW ( menu, MF_STRING, menuExit, L"Exit" );

		SetForegroundWindow ( window );
		const auto	choice = TrackPopupMenu ( menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, anchor.x, anchor.y, 0, window, nullptr );
		PostMessageW ( window, WM_NULL, 0, 0 );
		DestroyMenu ( menu );

		if ( choice == menuRefresh )
			refresh ( window, true );
		else if ( choice == menuAutostart )
			autostart::setEnabled ( ! autostart::isEnabled () );
		else if ( choice == menuExit )
			DestroyWindow ( window );
	}

	LRESULT CALLBACK windowProc ( HWND window, UINT message, WPARAM wParam, LPARAM lParam )
	{
		if ( message == taskbarCreated )
		{
			refresh ( window, true );
			return 0;
		}

		switch ( message )
		{
			case WM_TIMER:
				if ( wParam == connectionTimer )
					KillTimer ( window, connectionTimer );
				refresh ( window, false );
				return 0;

			// Delayed so the stack has updated the device properties, and a burst of events causes one refresh
			case WM_DEVICECHANGE:
				if ( battery::onDeviceChange ( window, wParam, lParam ) )
					SetTimer ( window, connectionTimer, connectionSettleMilliseconds, nullptr );
				return TRUE;

			case WM_SETTINGCHANGE:
				if ( lParam != 0 && wcscmp ( reinterpret_cast<const wchar_t*> ( lParam ), L"ImmersiveColorSet" ) == 0 )
					refresh ( window, true );
				return 0;

			case WM_DISPLAYCHANGE:
			case WM_DPICHANGED:
				refresh ( window, true );
				return 0;

			case trayMessage:
				if ( LOWORD ( lParam ) == WM_CONTEXTMENU )
					showMenu ( window, { GET_X_LPARAM ( wParam ), GET_Y_LPARAM ( wParam ) } );
				return 0;

			case WM_DESTROY:
				for ( const auto& entry : entries )
					remove ( window, entry );
				PostQuitMessage ( 0 );
				return 0;
		}

		return DefWindowProcW ( window, message, wParam, lParam );
	}
}
//-----------------------------------------------------------------------------

int WINAPI wWinMain ( HINSTANCE instance, HINSTANCE, PWSTR, int )
{
	CreateMutexW ( nullptr, FALSE, L"ultraBattery" );
	if ( GetLastError () == ERROR_ALREADY_EXISTS )
		return 0;

	autostart::initialise ();
	SetProcessDpiAwarenessContext ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
	taskbarCreated = RegisterWindowMessageW ( L"TaskbarCreated" );

	auto	windowClass = WNDCLASSW {};
	windowClass.lpfnWndProc = windowProc;
	windowClass.hInstance = instance;
	windowClass.lpszClassName = L"ultraBattery";
	RegisterClassW ( &windowClass );

	// A hidden top-level window, message-only windows miss the TaskbarCreated broadcast
	const auto	window = CreateWindowExW ( 0, windowClass.lpszClassName, L"ultraBattery", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr );
	if ( window == nullptr )
		return 1;

	battery::watchConnections ( window );
	refresh ( window, true );
	SetTimer ( window, pollTimer, pollMilliseconds, nullptr );

	auto	message = MSG {};
	while ( GetMessageW ( &message, nullptr, 0, 0 ) > 0 )
		DispatchMessageW ( &message );

	return 0;
}
//-----------------------------------------------------------------------------
