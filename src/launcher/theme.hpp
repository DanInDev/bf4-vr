#pragma once

// The launcher's look: a dark, borderless window in the Battlefield 4 style (orange accent, uppercase
// condensed Bahnschrift headings, cut corners), drawn with plain GDI over owner-drawn controls.

#include <windows.h>

namespace bfvr::launcher::theme
{
	namespace color
	{
		constexpr COLORREF background = RGB(18, 20, 22);
		constexpr COLORREF chrome = RGB(11, 12, 14);
		constexpr COLORREF surface = RGB(28, 31, 34);
		constexpr COLORREF raised = RGB(40, 44, 48);
		constexpr COLORREF raised_hot = RGB(52, 57, 62);
		constexpr COLORREF border = RGB(54, 59, 65);
		constexpr COLORREF text = RGB(232, 232, 232);
		constexpr COLORREF muted = RGB(150, 156, 163);
		constexpr COLORREF dim = RGB(92, 97, 103);
		constexpr COLORREF accent = RGB(245, 135, 31);
		constexpr COLORREF accent_hot = RGB(255, 162, 66);
		constexpr COLORREF accent_pressed = RGB(205, 108, 18);
		constexpr COLORREF on_accent = RGB(16, 16, 16);
		constexpr COLORREF error = RGB(255, 122, 98);
		constexpr COLORREF close_hot = RGB(196, 43, 28);
	}

	enum class font
	{
		label,   // small uppercase captions above values
		note,    // descriptions under switch titles
		body,
		heading, // panel headings and tabs
		button,
		launch,  // the big start button
		brand,
		title,   // the key art's game title
		count,
	};

	enum class surface
	{
		background,
		chrome,
		panel,
	};

	enum class button
	{
		normal,
		primary,
		launch,
		tab,
		minimize,
		close,
		toggle,
	};

	// Fonts and brushes for the window's current DPI; call again after a DPI change.
	void create(HWND window);
	void destroy();

	HFONT get(font role);
	HBRUSH brush(surface area);
	COLORREF fill(surface area);
	int scale(int pixels); // 96-DPI pixels to the window's DPI

	// Owner-drawn buttons keep their hover and on/off state here (subclassed on attach).
	void attach(HWND control);
	bool checked(HWND control);
	void set_checked(HWND control, bool on);

	void draw_button(const DRAWITEMSTRUCT& item, button style, surface backdrop);
	// The game's short name in the header ("BF4", before the orange "VR") and its full name on the key art.
	void set_brand(const wchar_t* game, const wchar_t* title);
	void draw_header(HDC dc, const RECT& area);
	void draw_footer(HDC dc, const RECT& area);
	void draw_panel(HDC dc, const RECT& area);
	void draw_art(HDC dc, const RECT& area);

	// The borderless window: no system frame, dark DWM attributes, rounded corners on Windows 11.
	void frame(HWND window);
}
