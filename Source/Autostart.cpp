#include <string>

#include <windows.h>

#include "Autostart.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	constexpr auto	appKey = L"Software\\ultraBattery";
	constexpr auto	valueName = L"ultraBattery";

	std::wstring command ()
	{
		auto	path = std::wstring ( 32768, L'\0' );
		path.resize ( GetModuleFileNameW ( nullptr, path.data (), DWORD ( path.size () ) ) );
		return L"\"" + path + L"\"";
	}
}
//-----------------------------------------------------------------------------

void autostart::initialise ()
{
	auto	key = HKEY ( nullptr );
	auto	disposition = DWORD ( 0 );
	if ( RegCreateKeyExW ( HKEY_CURRENT_USER, appKey, 0, nullptr, 0, KEY_READ, nullptr, &key, &disposition ) != ERROR_SUCCESS )
		return;

	RegCloseKey ( key );

	if ( disposition == REG_CREATED_NEW_KEY || isEnabled () )
		setEnabled ( true );
}

bool autostart::isEnabled ()
{
	return RegGetValueW ( HKEY_CURRENT_USER, runKey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr ) == ERROR_SUCCESS;
}

void autostart::setEnabled ( bool enabled )
{
	if ( ! enabled )
	{
		RegDeleteKeyValueW ( HKEY_CURRENT_USER, runKey, valueName );
		return;
	}

	const auto	value = command ();
	RegSetKeyValueW ( HKEY_CURRENT_USER, runKey, valueName, REG_SZ, value.c_str (), DWORD ( ( value.size () + 1 ) * sizeof ( wchar_t ) ) );
}
//-----------------------------------------------------------------------------
