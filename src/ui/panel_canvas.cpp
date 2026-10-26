#include "ui/panel_canvas.hpp"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <map>

namespace bfvr::ui
{
	namespace
	{
		COLORREF to_colorref(std::uint32_t rgb)
		{
			return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
		}

		std::wstring widen(const std::string& text)
		{
			if (text.empty())
			{
				return {};
			}
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
			return wide;
		}
	}

	bool rasterise(const std::vector<draw_item>& items, int width, int height, std::vector<std::uint32_t>& pixels)
	{
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height; // top-down
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		HDC dc = CreateCompatibleDC(nullptr);
		if (!dc)
		{
			return false;
		}
		void* bits = nullptr;
		HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!bitmap || !bits)
		{
			DeleteDC(dc);
			return false;
		}
		const HGDIOBJ previous_bitmap = SelectObject(dc, bitmap);
		const HGDIOBJ previous_font = GetCurrentObject(dc, OBJ_FONT);
		SetBkMode(dc, TRANSPARENT);

		std::map<int, HFONT> fonts;
		for (const draw_item& item : items)
		{
			RECT area{ item.area.x, item.area.y, item.area.x + item.area.w, item.area.y + item.area.h };
			if (item.kind == draw_item::type::box)
			{
				const HBRUSH brush = CreateSolidBrush(to_colorref(item.color));
				FillRect(dc, &area, brush);
				DeleteObject(brush);
				continue;
			}
			HFONT& font = fonts[item.size];
			if (!font)
			{
				font = CreateFontW(-item.size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
			}
			SelectObject(dc, font);
			SetTextColor(dc, to_colorref(item.color));
			const UINT align = item.align == 1 ? DT_CENTER : item.align == 2 ? DT_RIGHT : DT_LEFT;
			std::wstring content = widen(item.text);
			const UINT layout = item.wrap ? DT_WORDBREAK | DT_END_ELLIPSIS : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
			DrawTextW(dc, content.data(), static_cast<int>(content.size()), &area, align | layout | DT_NOPREFIX);
		}
		GdiFlush();

		pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
		std::memcpy(pixels.data(), bits, pixels.size() * sizeof(std::uint32_t));
		for (std::uint32_t& pixel : pixels)
		{
			pixel |= 0xFF000000u; // GDI leaves alpha at 0; the panel is opaque
		}

		SelectObject(dc, previous_font);
		SelectObject(dc, previous_bitmap);
		for (const auto& [size, font] : fonts)
		{
			DeleteObject(font);
		}
		DeleteObject(bitmap);
		DeleteDC(dc);
		return true;
	}

	bool write_bmp(const std::string& path, const std::vector<std::uint32_t>& pixels, int width, int height)
	{
		if (pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
		{
			return false;
		}
		BITMAPINFOHEADER header{};
		header.biSize = sizeof(header);
		header.biWidth = width;
		header.biHeight = -height;
		header.biPlanes = 1;
		header.biBitCount = 32;
		header.biCompression = BI_RGB;
		const DWORD data_size = static_cast<DWORD>(pixels.size() * sizeof(std::uint32_t));
		BITMAPFILEHEADER file{};
		file.bfType = 0x4D42; // "BM"
		file.bfOffBits = sizeof(file) + sizeof(header);
		file.bfSize = file.bfOffBits + data_size;
		std::ofstream out(path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(&file), sizeof(file));
		out.write(reinterpret_cast<const char*>(&header), sizeof(header));
		out.write(reinterpret_cast<const char*>(pixels.data()), data_size);
		return static_cast<bool>(out);
	}
}
