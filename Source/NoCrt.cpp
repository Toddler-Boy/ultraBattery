#include <cstddef>

//-----------------------------------------------------------------------------

// The few symbols the compiler expects from a C runtime. Built with -fno-builtin,
// so these loops are never turned back into calls to themselves

extern "C"
{
	int	_fltused = 0;

	void* memset ( void* destination, int value, size_t count )
	{
		auto*	target = static_cast<unsigned char*> ( destination );

		for ( auto i = size_t ( 0 ); i < count; ++i )
			target[ i ] = static_cast<unsigned char> ( value );

		return destination;
	}

	void* memcpy ( void* destination, const void* source, size_t count )
	{
		auto*		target = static_cast<unsigned char*> ( destination );
		const auto*	from = static_cast<const unsigned char*> ( source );

		for ( auto i = size_t ( 0 ); i < count; ++i )
			target[ i ] = from[ i ];

		return destination;
	}

	int memcmp ( const void* a, const void* b, size_t count )
	{
		const auto*	left = static_cast<const unsigned char*> ( a );
		const auto*	right = static_cast<const unsigned char*> ( b );

		for ( auto i = size_t ( 0 ); i < count; ++i )
			if ( left[ i ] != right[ i ] )
				return left[ i ] - right[ i ];

		return 0;
	}
}
//-----------------------------------------------------------------------------
