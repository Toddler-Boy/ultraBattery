#pragma once

#include <windows.h>

//-----------------------------------------------------------------------------

// Wide string helpers on top of kernel32, the app links no C runtime

namespace text
{
	inline bool equals ( const wchar_t* a, const wchar_t* b, bool ignoreCase = false )
	{
		return CompareStringOrdinal ( a, -1, b, -1, ignoreCase ) == CSTR_EQUAL;
	}

	inline bool less ( const wchar_t* a, const wchar_t* b )
	{
		return CompareStringOrdinal ( a, -1, b, -1, FALSE ) == CSTR_LESS_THAN;
	}

	inline bool startsWith ( const wchar_t* text, const wchar_t* prefix )
	{
		const auto	length = lstrlenW ( prefix );
		return lstrlenW ( text ) >= length && CompareStringOrdinal ( text, length, prefix, length, FALSE ) == CSTR_EQUAL;
	}

	// Steps through a REG_MULTI_SZ style list: strings separated by one zero, ended by two
	inline const wchar_t* next ( const wchar_t* item )
	{
		return item + lstrlenW ( item ) + 1;
	}
}
//-----------------------------------------------------------------------------
