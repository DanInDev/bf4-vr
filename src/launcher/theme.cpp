#include "launcher/theme.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <wincodec.h>

#include <algorithm>
#include <string>

#include "launcher/resource.hpp"

namespace bfvr::launcher::theme
{
	namespace
	{
		constexpr LONG_PTR hot_flag = 1;
		constexpr LONG_PTR checked_flag = 2;

		UINT dpi = USER_DEFAULT_SCREEN_DPI;
		HFONT fonts[static_cast<int>(font::count)]{};
		HBRUSH brushes[3]{};
		HICON brand_icon{};

		// The decoded wallpaper and its last cropped, darkened copy for the key art panel.
		IWICBitmapSource* wallpaper{};
		HBITMAP wallpaper_cut{};
		SIZE wallpaper_cut_size{};

		template<typename com>
		void release(com*& object)
		{
			if (object)
			{
				object->Release();
				object = nullptr;
			}
		}

		IWICBitmapSource* decode_wallpaper(IWICImagingFactory* factory)
		{
			const HMODULE module = GetModuleHandleW(nullptr);
			const HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(IDR_WALLPAPER), RT_RCDATA);
			const HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
			const auto* bytes = loaded ? static_cast<BYTE*>(LockResource(loaded)) : nullptr;
			if (!bytes)
			{
				return nullptr;
			}

			IWICStream* stream{};
			IWICBitmapDecoder* decoder{};
			IWICBitmapFrameDecode* frame{};
			IWICBitmapSource* converted{};
			IWICBitmap* bitmap{};
			// The resource memory is read-only, which is all WIC needs from it.
			if (SUCCEEDED(factory->CreateStream(&stream))
				&& SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), SizeofResource(module, found)))
				&& SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
				&& SUCCEEDED(decoder->GetFrame(0, &frame))
				&& SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGR, frame, &converted)))
			{
				factory->CreateBitmapFromSource(converted, WICBitmapCacheOnLoad, &bitmap);
			}
			release(converted);
			release(frame);
			release(decoder);
			release(stream);
			return bitmap;
		}

		// The wallpaper cropped to cover the panel (anchored left of centre, where the soldier is),
		// darkened toward the bottom so the title stays readable.
		HBITMAP cut_wallpaper(int width, int height)
		{
			if (!wallpaper || width <= 0 || height <= 0)
			{
				return nullptr;
			}
			if (wallpaper_cut && wallpaper_cut_size.cx == width && wallpaper_cut_size.cy == height)
			{
				return wallpaper_cut;
			}
			if (wallpaper_cut)
			{
				DeleteObject(wallpaper_cut);
				wallpaper_cut = nullptr;
			}

			IWICImagingFactory* factory{};
			if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			{
				return nullptr;
			}
			UINT source_w{};
			UINT source_h{};
			wallpaper->GetSize(&source_w, &source_h);
			const double zoom = std::max(static_cast<double>(width) / source_w, static_cast<double>(height) / source_h);
			const int crop_w = std::min(static_cast<int>(source_w), static_cast<int>(width / zoom + 0.5));
			const int crop_h = std::min(static_cast<int>(source_h), static_cast<int>(height / zoom + 0.5));
			const WICRect crop{ (static_cast<int>(source_w) - crop_w) * 3 / 10, (static_cast<int>(source_h) - crop_h) / 2, crop_w, crop_h };

			IWICBitmapClipper* clipper{};
			IWICBitmapScaler* scaler{};
			BITMAPINFO info{};
			info.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB };
			void* pixels{};
			HBITMAP result = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
			const UINT stride = static_cast<UINT>(width) * 4;
			const bool ok = result && SUCCEEDED(factory->CreateBitmapClipper(&clipper))
				&& SUCCEEDED(clipper->Initialize(wallpaper, &crop))
				&& SUCCEEDED(factory->CreateBitmapScaler(&scaler))
				&& SUCCEEDED(scaler->Initialize(clipper, width, height, WICBitmapInterpolationModeHighQualityCubic))
				&& SUCCEEDED(scaler->CopyPixels(nullptr, stride, stride * height, static_cast<BYTE*>(pixels)));
			release(scaler);
			release(clipper);
			release(factory);
			if (!ok)
			{
				if (result)
				{
					DeleteObject(result);
				}
				return nullptr;
			}

			const auto smooth = [](double value)
			{
				const double t = std::clamp(value, 0.0, 1.0);
				return t * t * (3 - 2 * t);
			};
			auto* row = static_cast<BYTE*>(pixels);
			for (int y = 0; y < height; ++y, row += stride)
			{
				// 80% brightness at the top, fading to 12% at the bottom, and darker still toward the
				// lower left where the title sits.
				const double down = static_cast<double>(y) / height;
				const double vertical = 0.8 - 0.68 * smooth((down - 0.3) / 0.7);
				const double title_rows = smooth((down - 0.25) / 0.35);
				for (int x = 0; x < width; ++x)
				{
					const double side = 1 - 0.55 * title_rows * smooth(1 - static_cast<double>(x) / (width * 0.8));
					const int keep = static_cast<int>(256 * vertical * side);
					BYTE* pixel = row + x * 4;
					pixel[0] = static_cast<BYTE>(pixel[0] * keep >> 8);
					pixel[1] = static_cast<BYTE>(pixel[1] * keep >> 8);
					pixel[2] = static_cast<BYTE>(pixel[2] * keep >> 8);
				}
			}
			wallpaper_cut = result;
			wallpaper_cut_size = { width, height };
			return wallpaper_cut;
		}

		struct font_spec
		{
			const wchar_t* face;
			int tenths; // point size x10
			int weight;
		};

		// Bahnschrift ships with Windows 10 and later; its named instances select width and weight.
		constexpr font_spec specs[] = {
			{ L"Bahnschrift SemiBold", 80, FW_NORMAL },
			{ L"Segoe UI", 80, FW_NORMAL },
			{ L"Segoe UI", 90, FW_NORMAL },
			{ L"Bahnschrift SemiBold", 110, FW_NORMAL },
			{ L"Bahnschrift SemiBold", 95, FW_NORMAL },
			{ L"Bahnschrift SemiBold", 150, FW_NORMAL },
			{ L"Bahnschrift SemiBold Condensed", 150, FW_NORMAL },
			{ L"Bahnschrift Bold Condensed", 420, FW_NORMAL },
		};
		static_assert(std::size(specs) == static_cast<std::size_t>(font::count));

		std::wstring upper(std::wstring text)
		{
			if (!text.empty())
			{
				CharUpperBuffW(text.data(), static_cast<DWORD>(text.size()));
			}
			return text;
		}

		std::wstring text_of(HWND control)
		{
			std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
			text.resize(static_cast<std::size_t>(GetWindowTextW(control, text.data(), static_cast<int>(text.size()))));
			return text;
		}

		void fill_rect(HDC dc, const RECT& area, COLORREF fill)
		{
			SetDCBrushColor(dc, fill);
			FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
		}

		void polygon(HDC dc, const POINT* points, int count, COLORREF fill, COLORREF outline)
		{
			const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
			const HGDIOBJ old_pen = SelectObject(dc, GetStockObject(DC_PEN));
			SetDCBrushColor(dc, fill);
			SetDCPenColor(dc, outline);
			Polygon(dc, points, count);
			SelectObject(dc, old_pen);
			SelectObject(dc, old_brush);
		}

		// A rectangle with its top-right corner cut off, the Battlefield 4 menu shape.
		void cut_box(HDC dc, const RECT& area, int cut, COLORREF fill, COLORREF outline)
		{
			const LONG right = area.right - 1;
			const LONG bottom = area.bottom - 1;
			const POINT points[] = {
				{ area.left, area.top }, { right - cut, area.top }, { right, area.top + cut }, { right, bottom }, { area.left, bottom },
			};
			polygon(dc, points, 5, fill, outline);
		}

		void line(HDC dc, int x0, int y0, int x1, int y1, COLORREF color, int width)
		{
			const HPEN pen = CreatePen(PS_SOLID, width, color);
			const HGDIOBJ old = SelectObject(dc, pen);
			MoveToEx(dc, x0, y0, nullptr);
			LineTo(dc, x1, y1);
			SelectObject(dc, old);
			DeleteObject(pen);
		}

		void text(HDC dc, const std::wstring& content, RECT area, font role, COLORREF color, UINT flags)
		{
			const HGDIOBJ old = SelectObject(dc, get(role));
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, color);
			DrawTextW(dc, content.c_str(), static_cast<int>(content.size()), &area, flags | DT_NOPREFIX);
			SelectObject(dc, old);
		}

		SIZE measure(HDC dc, const std::wstring& content, font role)
		{
			const HGDIOBJ old = SelectObject(dc, get(role));
			SIZE size{};
			GetTextExtentPoint32W(dc, content.c_str(), static_cast<int>(content.size()), &size);
			SelectObject(dc, old);
			return size;
		}

		COLORREF mix(COLORREF from, COLORREF to, int step, int steps)
		{
			const auto channel = [&](int a, int b) { return a + (b - a) * step / std::max(steps, 1); };
			return RGB(channel(GetRValue(from), GetRValue(to)), channel(GetGValue(from), GetGValue(to)), channel(GetBValue(from), GetBValue(to)));
		}

		// Draws into an off-screen bitmap so hover repaints do not flicker.
		class buffered
		{
		public:
			buffered(HDC target, const RECT& area) : target_(target), area_(area)
			{
				dc_ = CreateCompatibleDC(target);
				bitmap_ = CreateCompatibleBitmap(target, area.right - area.left, area.bottom - area.top);
				old_ = SelectObject(dc_, bitmap_);
				SetViewportOrgEx(dc_, -area.left, -area.top, nullptr);
			}
			~buffered()
			{
				SetViewportOrgEx(dc_, 0, 0, nullptr);
				BitBlt(target_, area_.left, area_.top, area_.right - area_.left, area_.bottom - area_.top, dc_, 0, 0, SRCCOPY);
				SelectObject(dc_, old_);
				DeleteObject(bitmap_);
				DeleteDC(dc_);
			}
			buffered(const buffered&) = delete;
			buffered& operator=(const buffered&) = delete;
			HDC dc() const { return dc_; }

		private:
			HDC target_;
			RECT area_;
			HDC dc_{};
			HBITMAP bitmap_{};
			HGDIOBJ old_{};
		};

		void draw_toggle(HDC dc, RECT area, const std::wstring& content, bool on, bool hot, bool disabled, bool focus, COLORREF backdrop)
		{
			fill_rect(dc, area, hot && !disabled ? mix(backdrop, color::raised_hot, 1, 3) : backdrop);
			if (focus)
			{
				SetDCBrushColor(dc, color::accent);
				FrameRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
			}

			const auto split = content.find(L'\n');
			const std::wstring title = content.substr(0, split);
			const std::wstring description = split == std::wstring::npos ? std::wstring() : content.substr(split + 1);

			const int track_w = scale(36);
			const int track_h = scale(20);
			const int pad = scale(8);
			RECT track{ area.right - pad - track_w, (area.top + area.bottom - track_h) / 2, area.right - pad, 0 };
			track.bottom = track.top + track_h;

			RECT words{ area.left + pad, area.top, track.left - pad, area.bottom };
			if (description.empty())
			{
				text(dc, title, words, font::body, disabled ? color::dim : color::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
			else
			{
				const int middle = (area.top + area.bottom) / 2;
				RECT top = words;
				top.bottom = middle + scale(1);
				RECT bottom = words;
				bottom.top = middle + scale(1);
				text(dc, title, top, font::body, disabled ? color::dim : color::text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
				text(dc, description, bottom, font::note, disabled ? color::dim : color::muted, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
			}

			const COLORREF track_fill = on ? (disabled ? mix(color::accent, backdrop, 2, 3) : color::accent) : color::raised;
			const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
			const HGDIOBJ old_pen = SelectObject(dc, GetStockObject(DC_PEN));
			SetDCBrushColor(dc, track_fill);
			SetDCPenColor(dc, on ? track_fill : color::border);
			RoundRect(dc, track.left, track.top, track.right, track.bottom, track_h, track_h);

			const int inset = scale(3);
			const int knob = track_h - 2 * inset;
			const int knob_x = on ? track.right - inset - knob : track.left + inset;
			const COLORREF knob_fill = on ? color::on_accent : (disabled ? color::dim : color::muted);
			SetDCBrushColor(dc, knob_fill);
			SetDCPenColor(dc, knob_fill);
			Ellipse(dc, knob_x, track.top + inset, knob_x + knob, track.top + inset + knob);
			SelectObject(dc, old_pen);
			SelectObject(dc, old_brush);
		}

		LRESULT CALLBACK button_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
		{
			switch (message)
			{
			case WM_MOUSEMOVE:
				if (!(GetWindowLongPtrW(window, GWLP_USERDATA) & hot_flag))
				{
					SetWindowLongPtrW(window, GWLP_USERDATA, GetWindowLongPtrW(window, GWLP_USERDATA) | hot_flag);
					TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
					TrackMouseEvent(&track);
					InvalidateRect(window, nullptr, FALSE);
				}
				break;
			case WM_MOUSELEAVE:
				SetWindowLongPtrW(window, GWLP_USERDATA, GetWindowLongPtrW(window, GWLP_USERDATA) & ~hot_flag);
				InvalidateRect(window, nullptr, FALSE);
				break;
			case WM_LBUTTONDBLCLK:
				// Owner-drawn buttons report a double click instead of a second click; a fast double
				// click on a switch should flip it twice.
				return DefSubclassProc(window, WM_LBUTTONDOWN, wparam, lparam);
			case WM_NCDESTROY:
				RemoveWindowSubclass(window, button_proc, 1);
				break;
			}
			return DefSubclassProc(window, message, wparam, lparam);
		}
	}

	void create(HWND window)
	{
		destroy();
		dpi = GetDpiForWindow(window);
		for (std::size_t i = 0; i < std::size(specs); ++i)
		{
			fonts[i] = CreateFontW(-MulDiv(specs[i].tenths, static_cast<int>(dpi), 720), 0, 0, 0, specs[i].weight, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, specs[i].face);
		}
		brushes[static_cast<int>(surface::background)] = CreateSolidBrush(color::background);
		brushes[static_cast<int>(surface::chrome)] = CreateSolidBrush(color::chrome);
		brushes[static_cast<int>(surface::panel)] = CreateSolidBrush(color::surface);
		const int icon_size = scale(20);
		brand_icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, icon_size, icon_size, 0));

		if (!wallpaper)
		{
			IWICImagingFactory* factory{};
			if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			{
				wallpaper = decode_wallpaper(factory);
				release(factory);
			}
		}
	}

	void destroy()
	{
		if (brand_icon)
		{
			DestroyIcon(brand_icon);
			brand_icon = nullptr;
		}
		if (wallpaper_cut)
		{
			DeleteObject(wallpaper_cut);
			wallpaper_cut = nullptr;
		}
		release(wallpaper);
		for (HFONT& handle : fonts)
		{
			if (handle)
			{
				DeleteObject(handle);
				handle = nullptr;
			}
		}
		for (HBRUSH& handle : brushes)
		{
			if (handle)
			{
				DeleteObject(handle);
				handle = nullptr;
			}
		}
	}

	HFONT get(font role) { return fonts[static_cast<int>(role)]; }
	HBRUSH brush(surface area) { return brushes[static_cast<int>(area)]; }

	COLORREF fill(surface area)
	{
		switch (area)
		{
		case surface::chrome: return color::chrome;
		case surface::panel: return color::surface;
		default: return color::background;
		}
	}

	int scale(int pixels) { return MulDiv(pixels, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI); }

	void attach(HWND control) { SetWindowSubclass(control, button_proc, 1, 0); }

	bool checked(HWND control) { return (GetWindowLongPtrW(control, GWLP_USERDATA) & checked_flag) != 0; }

	void set_checked(HWND control, bool on)
	{
		const LONG_PTR flags = GetWindowLongPtrW(control, GWLP_USERDATA);
		SetWindowLongPtrW(control, GWLP_USERDATA, on ? flags | checked_flag : flags & ~checked_flag);
		InvalidateRect(control, nullptr, FALSE);
	}

	void draw_button(const DRAWITEMSTRUCT& item, button style, surface backdrop)
	{
		const RECT area = item.rcItem;
		buffered buffer(item.hDC, area);
		const HDC dc = buffer.dc();

		const std::wstring content = text_of(item.hwndItem);
		const bool disabled = item.itemState & ODS_DISABLED;
		const bool pressed = item.itemState & ODS_SELECTED;
		const bool focus = (item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT);
		const bool hot = GetWindowLongPtrW(item.hwndItem, GWLP_USERDATA) & hot_flag;
		const bool on = checked(item.hwndItem);
		const COLORREF back = fill(backdrop);
		fill_rect(dc, area, back);

		const UINT centered = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
		switch (style)
		{
		case button::normal:
		{
			const COLORREF body = disabled ? mix(color::raised, back, 1, 2) : pressed ? mix(color::raised, back, 1, 2) : hot ? color::raised_hot : color::raised;
			cut_box(dc, area, scale(6), body, focus ? color::accent : (hot && !disabled ? color::muted : color::border));
			text(dc, upper(content), area, font::button, disabled ? color::dim : color::text, centered);
			break;
		}
		case button::primary:
		case button::launch:
		{
			const int cut = scale(style == button::launch ? 14 : 8);
			if (disabled)
			{
				cut_box(dc, area, cut, mix(color::raised, back, 1, 2), color::border);
			}
			else
			{
				const COLORREF body = pressed ? color::accent_pressed : hot ? color::accent_hot : color::accent;
				cut_box(dc, area, cut, body, focus ? color::text : body);
			}
			RECT label = area;
			if (style == button::launch)
			{
				// A play marker before the words.
				const SIZE size = measure(dc, upper(content), font::launch);
				const int marker = scale(10);
				const int gap = scale(10);
				const int left = (area.left + area.right - size.cx - marker - gap) / 2;
				const int middle = (area.top + area.bottom) / 2;
				const POINT points[] = { { left, middle - marker / 2 - 1 }, { left + marker, middle }, { left, middle + marker / 2 + 1 } };
				const COLORREF ink = disabled ? color::dim : color::on_accent;
				polygon(dc, points, 3, ink, ink);
				label.left = left + marker + gap;
				text(dc, upper(content), label, font::launch, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			}
			else
			{
				text(dc, upper(content), label, font::button, disabled ? color::dim : color::on_accent, centered);
			}
			break;
		}
		case button::tab:
		{
			if (hot && !on)
			{
				fill_rect(dc, area, mix(back, color::raised, 1, 2));
			}
			text(dc, upper(content), area, font::heading, on || hot ? color::text : color::muted, centered);
			if (on)
			{
				const RECT bar{ area.left + scale(10), area.bottom - scale(3), area.right - scale(10), area.bottom };
				fill_rect(dc, bar, color::accent);
			}
			if (focus)
			{
				SetDCBrushColor(dc, color::border);
				FrameRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
			}
			break;
		}
		case button::minimize:
		case button::close:
		{
			if (hot || pressed)
			{
				fill_rect(dc, area, style == button::close ? color::close_hot : color::raised);
			}
			const int x = (area.left + area.right) / 2;
			const int y = (area.top + area.bottom) / 2;
			const int half = scale(5);
			const COLORREF ink = hot ? color::text : color::muted;
			if (style == button::minimize)
			{
				line(dc, x - half, y, x + half + 1, y, ink, std::max(1, scale(1)));
			}
			else
			{
				line(dc, x - half, y - half, x + half + 1, y + half + 1, ink, std::max(1, scale(1)));
				line(dc, x + half, y - half, x - half - 1, y + half + 1, ink, std::max(1, scale(1)));
			}
			break;
		}
		case button::toggle:
			draw_toggle(dc, area, content, on, hot, disabled, focus, back);
			break;
		}
	}

	void draw_header(HDC dc, const RECT& area)
	{
		fill_rect(dc, area, color::chrome);
		fill_rect(dc, RECT{ area.left, area.bottom - 1, area.right, area.bottom }, color::border);

		// Brand: the app icon, then "BF4" and an orange "VR".
		const int size = scale(20);
		const int left = area.left + scale(12);
		if (brand_icon)
		{
			DrawIconEx(dc, left, (area.top + area.bottom - 1 - size) / 2, brand_icon, size, size, 0, nullptr, DI_NORMAL);
		}

		const std::wstring game = L"BF4";
		const std::wstring vr = L" VR";
		RECT words{ left + size + scale(8), area.top, area.right, area.bottom - 1 };
		text(dc, game, words, font::brand, color::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		words.left += measure(dc, game, font::brand).cx;
		text(dc, vr, words, font::brand, color::accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	}

	void draw_footer(HDC dc, const RECT& area)
	{
		fill_rect(dc, area, color::chrome);
		fill_rect(dc, RECT{ area.left, area.top, area.right, area.top + 1 }, color::border);
	}

	void draw_panel(HDC dc, const RECT& area)
	{
		cut_box(dc, area, scale(10), color::surface, color::border);
		fill_rect(dc, RECT{ area.left, area.top + scale(11), area.left + scale(3), area.top + scale(29) }, color::accent);
	}

	void draw_art(HDC dc, const RECT& area)
	{
		const int saved = SaveDC(dc);
		IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
		const int width = area.right - area.left;
		const int height = area.bottom - area.top;

		if (const HBITMAP picture = cut_wallpaper(width, height))
		{
			const HDC source = CreateCompatibleDC(dc);
			const HGDIOBJ old = SelectObject(source, picture);
			BitBlt(dc, area.left, area.top, width, height, source, 0, 0, SRCCOPY);
			SelectObject(source, old);
			DeleteDC(source);
		}
		else
		{
			// Without the wallpaper: a night-sky gradient, darkest at the bottom where the title sits,
			// with diagonal hatching in the upper right.
			for (int y = 0; y < height; ++y)
			{
				fill_rect(dc, RECT{ area.left, area.top + y, area.right, area.top + y + 1 }, mix(RGB(34, 40, 47), RGB(9, 10, 12), y, height));
			}
			const POINT band[] = { { area.left + width * 2 / 5, area.top }, { area.right, area.top }, { area.right, area.top + height * 3 / 4 } };
			const HRGN region = CreatePolygonRgn(band, 3, WINDING);
			ExtSelectClipRgn(dc, region, RGN_AND);
			DeleteObject(region);
			const int step = scale(11);
			for (int x = area.left + width * 2 / 5; x < area.right + height; x += step)
			{
				line(dc, x, area.top, x - height, area.top + height, RGB(44, 51, 59), 1);
			}
		}
		RestoreDC(dc, saved);

		const int saved_again = SaveDC(dc);
		IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);

		// HUD corner brackets.
		const int inset = scale(12);
		const int arm = scale(18);
		const int thick = std::max(2, scale(2));
		const RECT inner{ area.left + inset, area.top + inset, area.right - inset - 1, area.bottom - inset - 1 };
		const COLORREF bracket = color::accent;
		line(dc, inner.left, inner.top, inner.left + arm, inner.top, bracket, thick);
		line(dc, inner.left, inner.top, inner.left, inner.top + arm, bracket, thick);
		line(dc, inner.right, inner.top, inner.right - arm, inner.top, bracket, thick);
		line(dc, inner.right, inner.top, inner.right, inner.top + arm, bracket, thick);
		line(dc, inner.left, inner.bottom, inner.left + arm, inner.bottom, bracket, thick);
		line(dc, inner.left, inner.bottom, inner.left, inner.bottom - arm, bracket, thick);
		line(dc, inner.right, inner.bottom, inner.right - arm, inner.bottom, bracket, thick);
		line(dc, inner.right, inner.bottom, inner.right, inner.bottom - arm, bracket, thick);

		// Title block, laid out from the bottom up.
		const int pad = scale(30);
		const int left = area.left + pad;
		int bottom = area.bottom - pad;
		const auto place = [&](const std::wstring& words, font role, COLORREF ink, int lift)
		{
			const SIZE size = measure(dc, words, role);
			text(dc, words, RECT{ left, bottom - size.cy, area.right - pad, bottom }, role, ink, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
			bottom -= size.cy - lift;
		};

		place(L"Quest 3 through Steam Link or Meta Horizon Link", font::body, color::muted, -scale(10));

		const int bar_top = bottom - scale(5);
		const POINT bar[] = { { left, bar_top }, { left + scale(64), bar_top }, { left + scale(60), bottom }, { left, bottom } };
		polygon(dc, bar, 4, color::accent, color::accent);
		bottom = bar_top - scale(6);

		place(L"VR", font::title, color::accent, scale(12));
		place(L"BATTLEFIELD 4", font::title, color::text, scale(4));
		place(L"SINGLE-PLAYER CAMPAIGN", font::heading, color::text, 0);
		RestoreDC(dc, saved_again);
	}

	void frame(HWND window)
	{
		const BOOL dark = TRUE;
		DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
		const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
		DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
		const COLORREF edge = color::border;
		DwmSetWindowAttribute(window, DWMWA_BORDER_COLOR, &edge, sizeof(edge));
		// A one-pixel frame keeps the DWM shadow on a window without a system frame.
		const MARGINS margins{ 0, 0, 0, 1 };
		DwmExtendFrameIntoClientArea(window, &margins);
		SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}
}
