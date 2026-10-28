#include <algorithm>
#include <array>
#include <span>

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include "Autostart.h"
#include "Battery.h"
#include "GUI_BatteryIcon.h"
#include "Text.h"

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
		std::array<wchar_t, MAX_DEVICE_ID_LEN>	id;
		UINT									uid;
		int										percent;	// -1 = not shown
	};

	auto	devices = battery::Devices {};
	auto	entries = std::array<TrayEntry, battery::maxDevices> {};
	auto	entryCount = 0;
	auto	taskbarCreated = UINT ( 0 );

	// Undocumented uxtheme exports, the only way to get a dark context menu
	using SetPreferredAppModeFn = int ( WINAPI* ) ( int );	// 0 default, 1 allow dark, 2 force dark, 3 force light
	using FlushMenuThemesFn = void ( WINAPI* ) ();

	auto	flushMenuThemes = FlushMenuThemesFn ( nullptr );

	void allowDarkMenus ()
	{
		const auto	uxtheme = LoadLibraryExW ( L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
		if ( uxtheme == nullptr )
			return;

		const auto	setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn> ( GetProcAddress ( uxtheme, MAKEINTRESOURCEA ( 135 ) ) );
		flushMenuThemes = reinterpret_cast<FlushMenuThemesFn> ( GetProcAddress ( uxtheme, MAKEINTRESOURCEA ( 136 ) ) );

		if ( setPreferredAppMode != nullptr )
			setPreferredAppMode ( 1 );
		if ( flushMenuThemes != nullptr )
			flushMenuThemes ();
	}

	std::span<TrayEntry> trayEntries ()
	{
		return { entries.data (), size_t ( entryCount ) };
	}

	TrayEntry* findEntry ( const wchar_t* id )
	{
		const auto	list = trayEntries ();
		const auto	it = std::ranges::find_if ( list, [ & ] ( const TrayEntry& entry ) { return text::equals ( entry.id.data (), id ); } );
		return it != list.end () ? &*it : nullptr;
	}

	// Windows remembers "show on taskbar" per exe path and uid, so a reconnecting device reuses a known uid
	UINT freeUid ()
	{
		auto	uid = UINT ( 1 );
		while ( std::ranges::contains ( trayEntries (), uid, &TrayEntry::uid ) )
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
		wsprintfW ( data.szTip, L"%.100s: %d%%", device.name.data (), device.percent );

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
		battery::scan ( devices );

		auto	kept = 0;
		for ( const auto& entry : trayEntries () )
		{
			if ( std::ranges::any_of ( devices.list (), [ & ] ( const battery::Device& device ) { return text::equals ( device.id.data (), entry.id.data () ); } ) )
				entries[ kept++ ] = entry;
			else
				remove ( window, entry );
		}
		entryCount = kept;


		const auto	iconSize = GetSystemMetricsForDpi ( SM_CXSMICON, GetDpiForWindow ( window ) );
		const auto	light = lightTaskbar ();

		for ( const auto& device : devices.list () )
		{
			auto*	entry = findEntry ( device.id.data () );
			if ( entry == nullptr )
			{
				const auto	uid = freeUid ();
				entry = &entries[ entryCount++ ];
				*entry = { device.id, uid, -1 };
			}

			if ( redraw || entry->percent != device.percent )
				show ( window, *entry, device, iconSize, light );
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

			case battery::changedMessage:
				refresh ( window, false );
				return 0;

			case WM_SETTINGCHANGE:
				if ( lParam != 0 && text::equals ( reinterpret_cast<const wchar_t*> ( lParam ), L"ImmersiveColorSet" ) )
				{
					if ( flushMenuThemes != nullptr )
						flushMenuThemes ();
					refresh ( window, true );
				}
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
				for ( const auto& entry : trayEntries () )
					remove ( window, entry );
				PostQuitMessage ( 0 );
				return 0;
		}

		return DefWindowProcW ( window, message, wParam, lParam );
	}

	int run ()
	{
		CreateMutexW ( nullptr, FALSE, L"ultraBattery" );
		if ( GetLastError () == ERROR_ALREADY_EXISTS )
			return 0;

		autostart::initialise ();
		allowDarkMenus ();
		SetProcessDpiAwarenessContext ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
		taskbarCreated = RegisterWindowMessageW ( L"TaskbarCreated" );

		const auto	instance = GetModuleHandleW ( nullptr );

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
}
//-----------------------------------------------------------------------------

// The linker's entry point (/ENTRY), there is no C runtime to call a WinMain
extern "C" void entry ()
{
	ExitProcess ( UINT ( run () ) );
}
//-----------------------------------------------------------------------------
