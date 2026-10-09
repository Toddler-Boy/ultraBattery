#include <array>

#include <windows.h>

#include "Autostart.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	constexpr auto	appKey = L"Software\\ultraBattery";
	constexpr auto	valueName = L"ultraBattery";
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

	auto		command = std::array<wchar_t, MAX_PATH * 4> {};
	const auto	length = GetModuleFileNameW ( nullptr, command.data () + 1, DWORD ( command.size () - 2 ) );
	command[ 0 ] = L'"';
	command[ length + 1 ] = L'"';

	RegSetKeyValueW ( HKEY_CURRENT_USER, runKey, valueName, REG_SZ, command.data (), DWORD ( ( length + 3 ) * sizeof ( wchar_t ) ) );
}
//-----------------------------------------------------------------------------
