#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

	struct Ink
	{
		std::vector<uint8_t>	alpha;
		int						width = 0;
		int						height = 0;
	};

	// The largest rendering of text whose inked pixels fit maxWidth x maxHeight, cropped to them
	Ink render ( const std::wstring& text, const wchar_t* face, int weight, int maxWidth, int maxHeight )
	{
		// Rendered white on black, the grey levels become alpha
		const auto	scratchSize = std::max ( maxWidth, maxHeight ) * 2;
		const auto	scratch = createDib ( scratchSize, scratchSize );
		const auto	dc = CreateCompatibleDC ( nullptr );
		const auto	oldBitmap = SelectObject ( dc, scratch.bitmap );

		SetTextColor ( dc, RGB ( 255, 255, 255 ) );
		SetBkMode ( dc, TRANSPARENT );

		auto	bounds = RECT {};
		for ( auto em = scratchSize; em > 4; --em )
		{
			std::fill_n ( scratch.pixels, scratchSize * scratchSize, 0u );

			const auto	font = CreateFontW ( -em, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
											 ANTIALIASED_QUALITY, DEFAULT_PITCH, face );
			const auto	oldFont = SelectObject ( dc, font );
			TextOutW ( dc, 0, 0, text.c_str (), int ( text.size () ) );
			SelectObject ( dc, oldFont );
			DeleteObject ( font );
			GdiFlush ();

			bounds = { scratchSize, scratchSize, 0, 0 };
			for ( auto y = 0; y < scratchSize; ++y )
				for ( auto x = 0; x < scratchSize; ++x )
					if ( ( scratch.pixels[ y * scratchSize + x ] & 0xFF ) != 0 )
						bounds = { std::min ( bounds.left, LONG ( x ) ), std::min ( bounds.top, LONG ( y ) ),
								   std::max ( bounds.right, LONG ( x + 1 ) ), std::max ( bounds.bottom, LONG ( y + 1 ) ) };

			if ( bounds.right - bounds.left <= maxWidth && bounds.bottom - bounds.top <= maxHeight )
				break;
		}

		SelectObject ( dc, oldBitmap );
		DeleteDC ( dc );

		auto	ink = Ink {};
		ink.width = std::max ( 0, int ( bounds.right - bounds.left ) );
		ink.height = std::max ( 0, int ( bounds.bottom - bounds.top ) );
		ink.alpha.resize ( ink.width * ink.height );

		for ( auto y = 0; y < ink.height; ++y )
			for ( auto x = 0; x < ink.width; ++x )
				ink.alpha[ y * ink.width + x ] = uint8_t ( scratch.pixels[ ( y + bounds.top ) * scratchSize + x + bounds.left ] & 0xFF );

		DeleteObject ( scratch.bitmap );
		return ink;
	}

	// The design as large as fits maxWidth x maxHeight, its edges anti-aliased by supersampling
	Ink paint ( const Design& design, int maxWidth, int maxHeight )
	{
		constexpr auto	supersample = 4;

		const auto	scale = std::min ( float ( maxWidth ) / design.width, float ( maxHeight ) / design.height );
		auto		ink = Ink {};
		ink.width = int ( std::lround ( design.width * scale ) );
		ink.height = int ( std::lround ( design.height * scale ) );

		const auto	scratchWidth = ink.width * supersample;
		const auto	scratchHeight = ink.height * supersample;
		const auto	scratch = createDib ( scratchWidth, scratchHeight );
		const auto	dc = CreateCompatibleDC ( nullptr );
		const auto	oldBitmap = SelectObject ( dc, scratch.bitmap );
		const auto	oldPen = SelectObject ( dc, GetStockObject ( NULL_PEN ) );
		const auto	oldBrush = SelectObject ( dc, GetStockObject ( WHITE_BRUSH ) );
		const auto	toScratch = [ & ] ( float units ) { return int ( std::lround ( units * scale * supersample ) ); };

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

		ink.alpha.resize ( ink.width * ink.height );
		for ( auto y = 0; y < ink.height; ++y )
			for ( auto x = 0; x < ink.width; ++x )
			{
				auto	sum = 0u;
				for ( auto sy = 0; sy < supersample; ++sy )
					for ( auto sx = 0; sx < supersample; ++sx )
						sum += scratch.pixels[ ( y * supersample + sy ) * scratchWidth + x * supersample + sx ] & 0xFF;

				ink.alpha[ y * ink.width + x ] = uint8_t ( sum / ( supersample * supersample ) );
			}

		DeleteObject ( scratch.bitmap );
		return ink;
	}

	void draw ( const Ink& ink, uint32_t* pixels, int size, int left, int top, uint32_t rgb )
	{
		for ( auto y = 0; y < ink.height; ++y )
			for ( auto x = 0; x < ink.width; ++x )
				if ( const auto alpha = uint32_t ( ink.alpha[ y * ink.width + x ] ); alpha != 0 )
					pixels[ ( y + top ) * size + x + left ] = ( alpha << 24 ) | rgb;
	}
}
//-----------------------------------------------------------------------------

HICON batteryIcon::create ( int percent, battery::Kind kind, int size, bool lightTaskbar )
{
	const auto	colour = createDib ( size, size );
	const auto	rgb = textColour ( percent, lightTaskbar );
	const auto	symbol = design ( kind );
	const auto	symbolHeight = symbol ? size * 9 / 16 - 1 : 0;
	const auto	gap = symbol ? symbolGap : 0;
	const auto	numberHeight = size - symbolHeight - gap;

	const auto	number = render ( std::to_wstring ( percent ), L"Segoe UI", FW_BOLD, size, numberHeight );
	draw ( number, colour.pixels, size, ( size - number.width ) / 2, ( numberHeight - number.height ) / 2, rgb );

	if ( symbol )
	{
		const auto	bandTop = numberHeight + gap + symbol->topInset;
		const auto	bandHeight = symbolHeight - symbol->topInset;
		const auto	device = paint ( *symbol, size, bandHeight );
		draw ( device, colour.pixels, size, ( size - device.width ) / 2, bandTop + ( bandHeight - device.height ) / 2, rgb );
	}

	const auto	opaque = std::vector<uint8_t> ( ( size + 15 ) / 16 * 2 * size, 0 );
	const auto	mask = CreateBitmap ( size, size, 1, 1, opaque.data () );
	auto		info = ICONINFO { TRUE, 0, 0, mask, colour.bitmap };
	const auto	icon = CreateIconIndirect ( &info );

	DeleteObject ( mask );
	DeleteObject ( colour.bitmap );
	return icon;
}
//-----------------------------------------------------------------------------
