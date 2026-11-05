#include "ui/panel_canvas.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
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

		// A rounded box blended straight into the DIB's pixels (GDI's own RoundRect has no
		// antialiasing): coverage from the distance to the rounded outline.
		void fill_rounded(std::uint32_t* pixels, int width, int height, const rect& area, int radius, std::uint32_t rgb)
		{
			const float r = static_cast<float>(std::min(radius, std::min(area.w, area.h) / 2));
			const float half_w = area.w * 0.5f, half_h = area.h * 0.5f;
			const float cx = area.x + half_w, cy = area.y + half_h;
			const int x0 = std::max(0, area.x), x1 = std::min(width, area.x + area.w);
			const int y0 = std::max(0, area.y), y1 = std::min(height, area.y + area.h);
			for (int y = y0; y < y1; ++y)
			{
				for (int x = x0; x < x1; ++x)
				{
					const float qx = std::fabs(x + 0.5f - cx) - (half_w - r);
					const float qy = std::fabs(y + 0.5f - cy) - (half_h - r);
					const float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
					const float coverage = std::clamp(0.5f - outside, 0.0f, 1.0f);
					if (coverage <= 0.0f)
					{
						continue;
					}
					std::uint32_t& pixel = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
					if (coverage >= 1.0f)
					{
						pixel = rgb;
						continue;
					}
					const auto mix = [&](int shift)
					{
						const float under = static_cast<float>((pixel >> shift) & 0xFFu);
						const float over = static_cast<float>((rgb >> shift) & 0xFFu);
						return static_cast<std::uint32_t>(std::lround(under + (over - under) * coverage)) << shift;
					};
					pixel = mix(16) | mix(8) | mix(0);
				}
			}
		}
	}

	bool rasterise(const std::vector<draw_item>& items, int width, int height, std::vector<std::uint32_t>& pixels, const std::vector<see_through>& keys,
		float scale, int offset_y)
	{
		const auto place = [&](const rect& from)
		{
			const int left = static_cast<int>(std::lround(from.x * scale));
			const int top = static_cast<int>(std::lround((from.y + offset_y) * scale));
			return rect{ left, top, static_cast<int>(std::lround((from.x + from.w) * scale)) - left,
				static_cast<int>(std::lround((from.y + offset_y + from.h) * scale)) - top };
		};
		const auto sized = [&](int pixels_at_full) { return std::max(1, static_cast<int>(std::lround(pixels_at_full * scale))); };
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
			const rect placed = place(item.area);
			RECT area{ placed.x, placed.y, placed.x + placed.w, placed.y + placed.h };
			if (item.kind == draw_item::type::box && item.radius > 0)
			{
				GdiFlush();
				fill_rounded(static_cast<std::uint32_t*>(bits), width, height, placed, sized(item.radius), item.color & 0xFFFFFFu);
				continue;
			}
			if (item.kind == draw_item::type::box)
			{
				const HBRUSH brush = CreateSolidBrush(to_colorref(item.color));
				FillRect(dc, &area, brush);
				DeleteObject(brush);
				continue;
			}
			const int size = sized(item.size);
			HFONT& font = fonts[size * 2 + (item.bold ? 1 : 0)];
			if (!font)
			{
				font = CreateFontW(-size, 0, 0, 0, item.bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
					CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
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
			// GDI leaves alpha at 0: opaque, except the keyed colours.
			const std::uint32_t rgb = pixel & 0xFFFFFFu;
			std::uint32_t alpha = 255;
			for (const see_through& key : keys)
			{
				if (key.color == rgb)
				{
					alpha = key.alpha;
					break;
				}
			}
			if (alpha == 255)
			{
				pixel = rgb | 0xFF000000u;
				continue;
			}
			const auto premultiplied = [&](int shift) { return (((rgb >> shift) & 0xFFu) * alpha / 255u) << shift; };
			pixel = (alpha << 24) | premultiplied(16) | premultiplied(8) | premultiplied(0);
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
