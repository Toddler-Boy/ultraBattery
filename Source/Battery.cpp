#include <algorithm>
#include <array>
#include <optional>

#include <windows.h>

#include <initguid.h>

#include <bluetoothapis.h>
#include <bthdef.h>
#include <cfgmgr32.h>
#include <dbt.h>
#include <devpkey.h>
#include <hidclass.h>

#include "Battery.h"
#include "Text.h"

//-----------------------------------------------------------------------------

namespace
{
	// Written by the Windows Bluetooth stack from the device's GATT Battery Service
	DEFINE_DEVPROPKEY ( batteryLevelKey, 0x104EA319, 0x6EE2, 0x4701, 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5, 2 );

	// Undocumented, the Bluetooth stack's live link state (the battery level stays cached after a disconnect)
	DEFINE_DEVPROPKEY ( connectedKey, 0x83DA6326, 0x97A6, 0x4088, 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29, 15 );

	auto	radio = HANDLE ( nullptr );
	auto	radioNotification = HDEVNOTIFY ( nullptr );

	// The present device IDs of one enumerator as a REG_MULTI_SZ style list; ids == nullptr = none
	struct IdList
	{
		wchar_t*	ids = nullptr;

		explicit IdList ( const wchar_t* enumerator )
		{
			constexpr auto	flags = ULONG ( CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT );

			for ( ;; )
			{
				auto	length = ULONG ( 0 );
				if ( CM_Get_Device_ID_List_SizeW ( &length, enumerator, flags ) != CR_SUCCESS )
					return;

				ids = static_cast<wchar_t*> ( HeapAlloc ( GetProcessHeap (), 0, length * sizeof ( wchar_t ) ) );
				if ( ids == nullptr )
					return;

				const auto	result = CM_Get_Device_ID_ListW ( enumerator, ids, length, flags );
				if ( result == CR_SUCCESS )
					return;

				release ();
				if ( result != CR_BUFFER_SMALL )
					return;
			}
		}

		~IdList () { release (); }

		IdList ( const IdList& ) = delete;
		IdList& operator= ( const IdList& ) = delete;

		void release ()
		{
			if ( ids != nullptr )
				HeapFree ( GetProcessHeap (), 0, ids );
			ids = nullptr;
		}
	};

	template <typename T>
	bool property ( DEVINST node, const DEVPROPKEY& key, DEVPROPTYPE wanted, T& value )
	{
		auto	type = DEVPROPTYPE ( 0 );
		auto	size = ULONG ( sizeof ( value ) );

		// Zeroed text buffers keep two wchar_t spare, so a full string list stays double-terminated
		if constexpr ( requires { value.data (); } )
			size -= sizeof ( wchar_t ) * 2;

		return CM_Get_DevNode_PropertyW ( node, &key, &type, reinterpret_cast<PBYTE> ( &value ), &size, 0 ) == CR_SUCCESS && type == wanted;
	}

	std::optional<int> batteryLevel ( DEVINST node )
	{
		auto	level = BYTE ( 0 );
		if ( ! property ( node, batteryLevelKey, DEVPROP_TYPE_BYTE, level ) )
			return std::nullopt;

		return int ( level );
	}

	bool isConnected ( DEVINST node )
	{
		auto	connected = DEVPROP_BOOLEAN ( DEVPROP_FALSE );
		return property ( node, connectedKey, DEVPROP_TYPE_BOOLEAN, connected ) && connected == DEVPROP_TRUE;
	}

	bool hasHardwareId ( DEVINST node, const wchar_t* wanted )
	{
		auto	ids = std::array<wchar_t, 1024> {};
		if ( ! property ( node, DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST, ids ) )
			return false;

		for ( const auto* id = ids.data (); *id != 0; id = text::next ( id ) )
			if ( text::equals ( id, wanted, true ) )
				return true;

		return false;
	}

	// The HID collections sit two levels down (BTHLE device, GATT HID service, HID collection)
	bool hasDescendantWithHardwareId ( DEVINST node, const wchar_t* wanted, int depth )
	{
		auto	child = DEVINST ( 0 );
		for ( auto result = CM_Get_Child ( &child, node, 0 ); result == CR_SUCCESS; result = CM_Get_Sibling ( &child, child, 0 ) )
			if ( hasHardwareId ( child, wanted ) || ( depth > 1 && hasDescendantWithHardwareId ( child, wanted, depth - 1 ) ) )
				return true;

		return false;
	}

	// Container categories are "Input.Keyboard", "Input.Mouse", "Input.Gaming" and so on; nullopt = not an input device.
	// Some gamepads report "Unknown", their HID game controller collection identifies them
	std::optional<battery::Kind> inputKind ( DEVINST node )
	{
		if ( hasDescendantWithHardwareId ( node, L"HID_DEVICE_SYSTEM_GAME", 2 ) )
			return battery::Kind::gamepad;

		auto	categories = std::array<wchar_t, 256> {};
		if ( ! property ( node, DEVPKEY_DeviceContainer_Category, DEVPROP_TYPE_STRING_LIST, categories ) )
			return std::nullopt;

		for ( const auto* category = categories.data (); *category != 0; category = text::next ( category ) )
		{
			if ( text::equals ( category, L"Input.Keyboard" ) )
				return battery::Kind::keyboard;
			if ( text::equals ( category, L"Input.Mouse" ) )
				return battery::Kind::mouse;
			if ( text::equals ( category, L"Input.Gaming" ) )
				return battery::Kind::gamepad;
			if ( text::startsWith ( category, L"Input." ) )
				return battery::Kind::other;
		}

		return std::nullopt;
	}

	// Nintendo Switch Pro Controller protocol (8BitDo controllers in Switch mode use it too). The level
	// only arrives inside replies, so a harmless "request device info" subcommand is sent to get one
	constexpr auto	switchHardwareId = L"_VID&0002057E_PID&2009";
	constexpr auto	switchOutputLength = 49;
	constexpr auto	switchReplyTimeout = ULONGLONG ( 1000 );

	auto	switchPacketCounter = BYTE ( 0 );

	std::optional<int> querySwitchController ( const wchar_t* interfacePath )
	{
		const auto	device = CreateFileW ( interfacePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
										   FILE_FLAG_OVERLAPPED, nullptr );
		if ( device == INVALID_HANDLE_VALUE )
			return std::nullopt;

		const auto	event = CreateEventW ( nullptr, TRUE, FALSE, nullptr );
		auto		overlapped = OVERLAPPED {};
		overlapped.hEvent = event;

		// Output report 0x01: packet counter, neutral rumble, subcommand 0x02
		auto	request = std::array<BYTE, switchOutputLength> { 0x01, BYTE ( switchPacketCounter++ & 0x0F ), 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x02 };
		auto	transferred = DWORD ( 0 );
		auto	sent = WriteFile ( device, request.data (), DWORD ( request.size () ), nullptr, &overlapped ) != FALSE
					   || ( GetLastError () == ERROR_IO_PENDING && GetOverlappedResult ( device, &overlapped, &transferred, TRUE ) != FALSE );

		// Reply report 0x21: byte 2 holds the level in its top three bits (0 empty .. 4 full)
		constexpr auto	percentForLevel = std::array { 0, 5, 30, 66, 100 };

		auto		level = std::optional<int> {};
		auto		report = std::array<BYTE, 512> {};
		const auto	deadline = GetTickCount64 () + switchReplyTimeout;

		while ( sent && ! level && GetTickCount64 () < deadline )
		{
			ResetEvent ( event );
			if ( ReadFile ( device, report.data (), DWORD ( report.size () ), nullptr, &overlapped ) == FALSE && GetLastError () != ERROR_IO_PENDING )
				break;

			if ( WaitForSingleObject ( event, DWORD ( deadline - GetTickCount64 () ) ) != WAIT_OBJECT_0 )
			{
				CancelIo ( device );
				GetOverlappedResult ( device, &overlapped, &transferred, TRUE );
				break;
			}

			if ( GetOverlappedResult ( device, &overlapped, &transferred, FALSE ) != FALSE && transferred > 2 && report[ 0 ] == 0x21 )
				level = percentForLevel[ std::min ( report[ 2 ] >> 5, 4 ) ];
		}

		CloseHandle ( event );
		CloseHandle ( device );
		return level;
	}

	// The controller's HID interface shares the Bluetooth device's container
	std::optional<int> switchControllerLevel ( DEVINST bluetoothNode )
	{
		auto	container = GUID {};
		if ( ! property ( bluetoothNode, DEVPKEY_Device_ContainerId, DEVPROP_TYPE_GUID, container ) )
			return std::nullopt;

		const auto	list = IdList ( L"HID" );
		if ( list.ids == nullptr )
			return std::nullopt;

		for ( const auto* id = list.ids; *id != 0; id = text::next ( id ) )
		{
			if ( ! text::contains ( id, switchHardwareId ) )
				continue;

			auto	node = DEVINST ( 0 );
			auto	nodeContainer = GUID {};
			if ( CM_Locate_DevNodeW ( &node, const_cast<DEVINSTID_W> ( id ), CM_LOCATE_DEVNODE_NORMAL ) != CR_SUCCESS
				 || ! property ( node, DEVPKEY_Device_ContainerId, DEVPROP_TYPE_GUID, nodeContainer ) || nodeContainer != container )
				continue;

			auto	interfaces = std::array<wchar_t, 512> {};
			if ( CM_Get_Device_Interface_ListW ( const_cast<GUID*> ( &GUID_DEVINTERFACE_HID ), const_cast<DEVINSTID_W> ( id ), interfaces.data (),
												 ULONG ( interfaces.size () - 1 ), CM_GET_DEVICE_INTERFACE_LIST_PRESENT ) != CR_SUCCESS || interfaces[ 0 ] == 0 )
				continue;

			return querySwitchController ( interfaces.data () );
		}

		return std::nullopt;
	}

	void unwatchRadio ()
	{
		if ( radioNotification != nullptr )
			UnregisterDeviceNotification ( radioNotification );
		if ( radio != nullptr )
			CloseHandle ( radio );

		radioNotification = nullptr;
		radio = nullptr;
	}

	// The connect/disconnect notifications are tied to an open radio handle
	void watchRadio ( HWND window )
	{
		unwatchRadio ();

		auto		params = BLUETOOTH_FIND_RADIO_PARAMS { sizeof ( BLUETOOTH_FIND_RADIO_PARAMS ) };
		const auto	find = BluetoothFindFirstRadio ( &params, &radio );
		if ( find == nullptr )
			return;

		BluetoothFindRadioClose ( find );

		auto	filter = DEV_BROADCAST_HANDLE {};
		filter.dbch_size = sizeof ( filter );
		filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
		filter.dbch_handle = radio;
		radioNotification = RegisterDeviceNotificationW ( window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE );
	}
}
//-----------------------------------------------------------------------------

void battery::watchConnections ( HWND window )
{
	// A radio that arrives later (USB dongle plugged in, re-enumerated after sleep) shows up as this interface
	auto	filter = DEV_BROADCAST_DEVICEINTERFACE_W {};
	filter.dbcc_size = sizeof ( filter );
	filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	filter.dbcc_classguid = GUID_BTHPORT_DEVICE_INTERFACE;
	RegisterDeviceNotificationW ( window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE );

	watchRadio ( window );
}

bool battery::onDeviceChange ( HWND window, WPARAM wParam, LPARAM lParam )
{
	const auto*	header = reinterpret_cast<const DEV_BROADCAST_HDR*> ( lParam );
	if ( header == nullptr )
		return false;

	if ( header->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE && wParam == DBT_DEVICEARRIVAL )
	{
		watchRadio ( window );
		return true;
	}

	if ( header->dbch_devicetype != DBT_DEVTYP_HANDLE )
		return false;

	switch ( wParam )
	{
		case DBT_CUSTOMEVENT:
			return reinterpret_cast<const DEV_BROADCAST_HANDLE*> ( header )->dbch_eventguid == GUID_BLUETOOTH_HCI_EVENT;

		// Holding the handle would block disabling or removing the radio
		case DBT_DEVICEQUERYREMOVE:
		case DBT_DEVICEREMOVECOMPLETE:
			unwatchRadio ();
			return true;
	}

	return false;
}

void battery::scan ( Devices& devices )
{
	devices.count = 0;

	// BTHLE = Bluetooth Low Energy, BTHENUM = classic Bluetooth (battery via hands-free profile)
	for ( const auto* enumerator : { L"BTHLE", L"BTHENUM" } )
	{
		const auto	list = IdList ( enumerator );
		if ( list.ids == nullptr )
			continue;

		for ( const auto* id = list.ids; *id != 0 && devices.count < maxDevices; id = text::next ( id ) )
		{
			auto	node = DEVINST ( 0 );
			if ( CM_Locate_DevNodeW ( &node, const_cast<DEVINSTID_W> ( id ), CM_LOCATE_DEVNODE_NORMAL ) != CR_SUCCESS )
				continue;

			if ( ! isConnected ( node ) )
				continue;

			const auto	kind = inputKind ( node );
			if ( ! kind )
				continue;

			auto	percent = batteryLevel ( node );
			if ( ! percent && *kind == Kind::gamepad )
				percent = switchControllerLevel ( node );
			if ( ! percent )
				continue;

			auto&	device = devices.items[ devices.count++ ];
			lstrcpynW ( device.id.data (), id, int ( device.id.size () ) );
			if ( ! property ( node, DEVPKEY_Device_FriendlyName, DEVPROP_TYPE_STRING, device.name ) )
				lstrcpynW ( device.name.data (), id, int ( device.name.size () ) );

			device.kind = *kind;
			device.percent = *percent;
		}
	}

	std::ranges::sort ( devices.items.data (), devices.items.data () + devices.count,
						[] ( const Device& a, const Device& b ) { return text::less ( a.name.data (), b.name.data () ); } );
}
//-----------------------------------------------------------------------------
