#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "GUI_BatteryIcon.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	lowPercent = 30;
	constexpr auto	criticalPercent = 15;
	constexpr auto	symbolGap = 2;

	struct Dib
	{
		HBITMAP		bitmap = nullptr;
		uint32_t*	pixels = nullptr;
	};

	// The icon's colour bitmap, everything is drawn straight into it
	struct Target
	{
		uint32_t*	pixels = nullptr;
		int			size = 0;
		uint32_t	rgb = 0;

		void put ( int x, int y, uint32_t alpha ) const
		{
			if ( alpha != 0 )
				pixels[ y * size + x ] = ( alpha << 24 ) | rgb;
		}
	};

	Dib createDib ( int width, int height )
	{
		auto	info = BITMAPINFO {};
		info.bmiHeader.biSize = sizeof ( info.bmiHeader );
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		auto		bits = static_cast<void*> ( nullptr );
		const auto	bitmap = CreateDIBSection ( nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0 );
		return { bitmap, static_cast<uint32_t*> ( bits ) };
	}

	// Positive values only
	int roundToInt ( float value )
	{
		return int ( value + 0.5f );
	}

	uint32_t textColour ( int percent, bool lightTaskbar )
	{
		if ( percent <= criticalPercent )
			return 0xFF3030;
		if ( percent <= lowPercent )
			return 0xFFA000;
		return lightTaskbar ? 0x000000 : 0xFFFFFF;
	}

	struct Part
	{
		bool	cut = false;
		float	left = 0.0f;
		float	top = 0.0f;
		float	right = 0.0f;
		float	bottom = 0.0f;
		float	radius = 0.0f;
	};

	struct Design
	{
		float					width = 0.0f;
		float					height = 0.0f;
		std::span<const Part>	parts;
		int						topInset = 0;	// pixels taken off the top of the symbol band
	};

	// Proportions after the Font Awesome Free solid icons (CC BY 4.0), in their 640 unit grid
	constexpr auto	keyboardParts = std::array {
		Part { false, 0, 0, 576, 384, 64 },
		Part { true, 64, 64, 128, 128, 16 },	Part { true, 160, 64, 224, 128, 16 },	Part { true, 256, 64, 320, 128, 16 },
		Part { true, 352, 64, 416, 128, 16 },	Part { true, 448, 64, 512, 128, 16 },
		Part { true, 64, 160, 128, 224, 16 },	Part { true, 160, 160, 224, 224, 16 },	Part { true, 256, 160, 320, 224, 16 },
		Part { true, 352, 160, 416, 224, 16 },	Part { true, 448, 160, 512, 224, 16 },
		Part { true, 160, 256, 416, 320, 16 } };

	constexpr auto	mouseParts = std::array {
		Part { false, 0, 0, 384, 512, 160 },
		Part { true, 168, 0, 216, 192, 0 },
		Part { true, 0, 192, 384, 240, 0 } };

	constexpr auto	gamepadParts = std::array {
		Part { false, 0, 0, 640, 384, 192 },
		Part { true, 160, 96, 224, 288, 32 },
		Part { true, 96, 160, 288, 224, 32 },
		Part { true, 388, 196, 476, 284, 44 },
		Part { true, 452, 100, 540, 188, 44 } };

	// nullopt = no symbol, the number gets the full height
	std::optional<Design> design ( battery::Kind kind )
	{
		switch ( kind )
		{
			case battery::Kind::keyboard:	return Design { 576, 384, keyboardParts, 1 };
			case battery::Kind::mouse:		return Design { 384, 512, mouseParts };
			case battery::Kind::gamepad:	return Design { 640, 384, gamepadParts };
			case battery::Kind::other:		return std::nullopt;
		}
		return std::nullopt;
	}

	// The largest rendering whose inked pixels fit the band, centred in it
	void drawNumber ( const Target& target, int percent, RECT band )
	{
		auto	text = std::array<wchar_t, 8> {};
		const auto	length = wsprintfW ( text.data (), L"%d", percent );

		// Rendered white on black, the grey levels become alpha
		const auto	maxWidth = int ( band.right - band.left );
		const auto	maxHeight = int ( band.bottom - band.top );
		const auto	scratchSize = std::max ( maxWidth, maxHeight ) * 2;
		const auto	scratch = createDib ( scratchSize, scratchSize );
		const auto	dc = CreateCompatibleDC ( nullptr );
		const auto	oldBitmap = SelectObject ( dc, scratch.bitmap );

		SetTextColor ( dc, RGB ( 255, 255, 255 ) );
		SetBkMode ( dc, TRANSPARENT );

		auto	ink = RECT {};
		for ( auto em = scratchSize; em > 4; --em )
		{
			std::fill_n ( scratch.pixels, scratchSize * scratchSize, 0u );

			const auto	font = CreateFontW ( -em, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
											 ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI" );
			const auto	oldFont = SelectObject ( dc, font );
			TextOutW ( dc, 0, 0, text.data (), length );
			SelectObject ( dc, oldFont );
			DeleteObject ( font );
			GdiFlush ();

			ink = { scratchSize, scratchSize, 0, 0 };
			for ( auto y = 0; y < scratchSize; ++y )
				for ( auto x = 0; x < scratchSize; ++x )
					if ( ( scratch.pixels[ y * scratchSize + x ] & 0xFF ) != 0 )
						ink = { std::min ( ink.left, LONG ( x ) ), std::min ( ink.top, LONG ( y ) ),
								std::max ( ink.right, LONG ( x + 1 ) ), std::max ( ink.bottom, LONG ( y + 1 ) ) };

			if ( ink.right - ink.left <= maxWidth && ink.bottom - ink.top <= maxHeight )
				break;
		}

		SelectObject ( dc, oldBitmap );
		DeleteDC ( dc );

		const auto	offsetX = band.left + ( maxWidth - ( ink.right - ink.left ) ) / 2 - ink.left;
		const auto	offsetY = band.top + ( maxHeight - ( ink.bottom - ink.top ) ) / 2 - ink.top;

		for ( auto y = ink.top; y < ink.bottom; ++y )
			for ( auto x = ink.left; x < ink.right; ++x )
				target.put ( x + offsetX, y + offsetY, scratch.pixels[ y * scratchSize + x ] & 0xFF );

		DeleteObject ( scratch.bitmap );
	}

	// The design as large as fits the band, centred in it, its edges anti-aliased by supersampling
	void drawDesign ( const Target& target, const Design& design, RECT band )
	{
		constexpr auto	supersample = 4;

		const auto	maxWidth = int ( band.right - band.left );
		const auto	maxHeight = int ( band.bottom - band.top );
		const auto	scale = std::min ( float ( maxWidth ) / design.width, float ( maxHeight ) / design.height );
		const auto	width = roundToInt ( design.width * scale );
		const auto	height = roundToInt ( design.height * scale );

		const auto	scratchWidth = width * supersample;
		const auto	scratch = createDib ( scratchWidth, height * supersample );
		const auto	dc = CreateCompatibleDC ( nullptr );
		const auto	oldBitmap = SelectObject ( dc, scratch.bitmap );
		const auto	oldPen = SelectObject ( dc, GetStockObject ( NULL_PEN ) );
		const auto	oldBrush = SelectObject ( dc, GetStockObject ( WHITE_BRUSH ) );
		const auto	toScratch = [ & ] ( float units ) { return roundToInt ( units * scale * supersample ); };

		// A null pen leaves out the right and bottom edge, hence the + 1
		for ( const auto& part : design.parts )
		{
			SelectObject ( dc, GetStockObject ( part.cut ? BLACK_BRUSH : WHITE_BRUSH ) );
			RoundRect ( dc, toScratch ( part.left ), toScratch ( part.top ), toScratch ( part.right ) + 1, toScratch ( part.bottom ) + 1,
						toScratch ( part.radius * 2 ), toScratch ( part.radius * 2 ) );
		}

		GdiFlush ();
		SelectObject ( dc, oldBrush );
		SelectObject ( dc, oldPen );
		SelectObject ( dc, oldBitmap );
		DeleteDC ( dc );

		const auto	left = band.left + ( maxWidth - width ) / 2;
		const auto	top = band.top + ( maxHeight - height ) / 2;

		for ( auto y = 0; y < height; ++y )
			for ( auto x = 0; x < width; ++x )
			{
				auto	sum = 0u;
				for ( auto sy = 0; sy < supersample; ++sy )
					for ( auto sx = 0; sx < supersample; ++sx )
						sum += scratch.pixels[ ( y * supersample + sy ) * scratchWidth + x * supersample + sx ] & 0xFF;

				target.put ( left + x, top + y, sum / ( supersample * supersample ) );
			}

		DeleteObject ( scratch.bitmap );
	}
}
//-----------------------------------------------------------------------------

HICON batteryIcon::create ( int percent, battery::Kind kind, int size, bool lightTaskbar )
{
	const auto	colour = createDib ( size, size );
	const auto	target = Target { colour.pixels, size, textColour ( percent, lightTaskbar ) };
	const auto	symbol = design ( kind );
	const auto	symbolHeight = symbol ? size * 9 / 16 - 1 : 0;
	const auto	gap = symbol ? symbolGap : 0;
	const auto	numberHeight = size - symbolHeight - gap;

	drawNumber ( target, percent, { 0, 0, size, numberHeight } );

	if ( symbol )
		drawDesign ( target, *symbol, { 0, numberHeight + gap + symbol->topInset, size, size } );

	// An all-zero mask: the colour bitmap's alpha decides
	const auto	maskBytes = SIZE_T ( ( size + 15 ) / 16 * 2 * size );
	const auto	maskBits = HeapAlloc ( GetProcessHeap (), HEAP_ZERO_MEMORY, maskBytes );
	const auto	mask = CreateBitmap ( size, size, 1, 1, maskBits );
	HeapFree ( GetProcessHeap (), 0, maskBits );

	auto		info = ICONINFO { TRUE, 0, 0, mask, colour.bitmap };
	const auto	icon = CreateIconIndirect ( &info );

	DeleteObject ( mask );
	DeleteObject ( colour.bitmap );
	return icon;
}
//-----------------------------------------------------------------------------
