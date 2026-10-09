#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

#include <windows.h>

#include <initguid.h>

#include <bluetoothapis.h>
#include <bthdef.h>
#include <cfgmgr32.h>
#include <dbt.h>
#include <devpkey.h>

#include "Battery.h"

//-----------------------------------------------------------------------------

namespace
{
	// Written by the Windows Bluetooth stack from the device's GATT Battery Service
	DEFINE_DEVPROPKEY ( batteryLevelKey, 0x104EA319, 0x6EE2, 0x4701, 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5, 2 );

	// Undocumented, the Bluetooth stack's live link state (the battery level stays cached after a disconnect)
	DEFINE_DEVPROPKEY ( connectedKey, 0x83DA6326, 0x97A6, 0x4088, 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29, 15 );

	std::vector<wchar_t> idList ( const wchar_t* enumerator )
	{
		constexpr auto	flags = ULONG ( CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT );

		auto	list = std::vector<wchar_t> {};
		for ( auto result = CONFIGRET ( CR_BUFFER_SMALL ); result == CR_BUFFER_SMALL; )
		{
			auto	length = ULONG ( 0 );
			if ( CM_Get_Device_ID_List_SizeW ( &length, enumerator, flags ) != CR_SUCCESS )
				return {};

			list.resize ( length );
			result = CM_Get_Device_ID_ListW ( enumerator, list.data (), length, flags );
			if ( result != CR_SUCCESS && result != CR_BUFFER_SMALL )
				return {};
		}

		return list;
	}

	auto	radio = HANDLE ( nullptr );
	auto	radioNotification = HDEVNOTIFY ( nullptr );

	template <typename T>
	bool property ( DEVINST node, const DEVPROPKEY& key, DEVPROPTYPE wanted, T& value )
	{
		auto	type = DEVPROPTYPE ( 0 );
		auto	size = ULONG ( sizeof ( value ) );

		// Text buffers keep two zeroed wchar_t at the end, so a full string list stays double-terminated
		if constexpr ( requires { value.data (); } )
			size -= sizeof ( wchar_t ) * 2;

		return CM_Get_DevNode_PropertyW ( node, &key, &type, reinterpret_cast<PBYTE> ( &value ), &size, 0 ) == CR_SUCCESS && type == wanted;
	}

	// A REG_MULTI_SZ style list: strings separated by one zero, ended by two
	std::vector<std::wstring_view> strings ( const wchar_t* list )
	{
		auto	result = std::vector<std::wstring_view> {};
		for ( const auto* text = list; text != nullptr && *text != 0; text += result.back ().size () + 1 )
			result.emplace_back ( text );
		return result;
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
		auto	ids = std::array<wchar_t, 2048> {};
		if ( ! property ( node, DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST, ids ) )
			return false;

		return std::ranges::any_of ( strings ( ids.data () ), [ & ] ( std::wstring_view id ) { return _wcsicmp ( id.data (), wanted ) == 0; } );
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

		auto	categories = std::array<wchar_t, 512> {};
		if ( ! property ( node, DEVPKEY_DeviceContainer_Category, DEVPROP_TYPE_STRING_LIST, categories ) )
			return std::nullopt;

		for ( const auto category : strings ( categories.data () ) )
		{
			if ( category == L"Input.Keyboard" )
				return battery::Kind::keyboard;
			if ( category == L"Input.Mouse" )
				return battery::Kind::mouse;
			if ( category == L"Input.Gaming" )
				return battery::Kind::gamepad;
			if ( category.starts_with ( L"Input." ) )
				return battery::Kind::other;
		}

		return std::nullopt;
	}

	std::wstring friendlyName ( DEVINST node, const wchar_t* fallback )
	{
		auto	name = std::array<wchar_t, 256> {};
		if ( ! property ( node, DEVPKEY_Device_FriendlyName, DEVPROP_TYPE_STRING, name ) )
			return fallback;

		return name.data ();
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

std::vector<battery::Device> battery::scan ()
{
	auto	devices = std::vector<Device> {};

	// BTHLE = Bluetooth Low Energy, BTHENUM = classic Bluetooth (battery via hands-free profile)
	for ( const auto* enumerator : { L"BTHLE", L"BTHENUM" } )
	{
		const auto	list = idList ( enumerator );

		for ( const auto id : strings ( list.data () ) )
		{
			auto	node = DEVINST ( 0 );
			if ( CM_Locate_DevNodeW ( &node, const_cast<DEVINSTID_W> ( id.data () ), CM_LOCATE_DEVNODE_NORMAL ) != CR_SUCCESS )
				continue;

			const auto	percent = batteryLevel ( node );
			if ( ! percent || ! isConnected ( node ) )
				continue;

			if ( const auto kind = inputKind ( node ) )
				devices.push_back ( { std::wstring ( id ), friendlyName ( node, id.data () ), *kind, *percent } );
		}
	}

	std::ranges::sort ( devices, {}, &Device::name );
	return devices;
}
//-----------------------------------------------------------------------------
