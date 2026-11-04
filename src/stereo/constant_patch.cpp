#include "stereo/constant_patch.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bfvr::stereo
{
	namespace
	{
		bool same(const float* a, const float* b, std::size_t count)
		{
			for (std::size_t i = 0; i < count; ++i)
			{
				// The arena holds the engine's own copies of these values: equal up to rounding.
				if (!(std::fabs(a[i] - b[i]) <= 1e-6f + 1e-5f * std::fabs(b[i])))
				{
					return false;
				}
			}
			return true;
		}

		bool all_zero(const float* a, std::size_t count)
		{
			for (std::size_t i = 0; i < count; ++i)
			{
				if (a[i] != 0.0f)
				{
					return false;
				}
			}
			return true;
		}

		bool finite(const float* a, std::size_t count)
		{
			for (std::size_t i = 0; i < count; ++i)
			{
				if (!std::isfinite(a[i]))
				{
					return false;
				}
			}
			return true;
		}
	}

	void constant_patch::clear()
	{
		matrices_.clear();
		positions_.clear();
		matrix_index_.clear();
		position_index_.clear();
		filter_.clear();
		near_zero_ = false;
	}

	void constant_patch::rebuild_index()
	{
		matrix_index_.clear();
		position_index_.clear();
		for (std::uint32_t i = 0; i < matrices_.size(); ++i)
		{
			matrix_index_.emplace_back(matrices_[i].centre[0], i);
		}
		for (std::uint32_t i = 0; i < positions_.size(); ++i)
		{
			position_index_.emplace_back(positions_[i].centre[0], i);
		}
		std::sort(matrix_index_.begin(), matrix_index_.end());
		std::sort(position_index_.begin(), position_index_.end());
		filter_.assign((1u << 20) / 64, 0);
		near_zero_ = false;
		const auto mark = [this](float value) {
			if (std::fabs(value) < 1e-5f)
			{
				near_zero_ = true;
			}
			std::uint32_t bits{};
			std::memcpy(&bits, &value, sizeof(bits));
			const std::uint32_t key = bits >> 12;
			// A key spans 2^-11 of the magnitude; same()'s tolerance 1e-6 + 1e-5 |c| spans this many keys
			// (doubled: below an exponent boundary the keys are twice as fine), plus one.
			const float magnitude = std::fmax(std::fabs(value), 1e-5f);
			const auto span = static_cast<std::uint32_t>(std::fmin(2.0f * (1e-6f + 1e-5f * magnitude) / (magnitude * 4.8828125e-4f), 1024.0f)) + 1;
			const std::uint32_t low = key > span ? key - span : 0;
			for (std::uint32_t k = low; k <= key + span && k < (1u << 20); ++k)
			{
				filter_[k >> 6] |= std::uint64_t{ 1 } << (k & 63);
			}
		};
		for (const auto& entry : matrix_index_)
		{
			mark(entry.first);
		}
		for (const auto& entry : position_index_)
		{
			mark(entry.first);
		}
	}

	bool constant_patch::may_match(float value) const
	{
		if (filter_.empty())
		{
			return false;
		}
		if (near_zero_ && std::fabs(value) < 2e-5f)
		{
			return true;
		}
		std::uint32_t bits{};
		std::memcpy(&bits, &value, sizeof(bits));
		const std::uint32_t key = bits >> 12;
		return (filter_[key >> 6] >> (key & 63)) & 1u;
	}

	void constant_patch::add_view(const float* centre, const float* eye, std::size_t floats)
	{
		for (std::size_t i = 0; i + 16 <= floats; i += 4)
		{
			if (same(centre + i, eye + i, 16) || all_zero(centre + i, 16) || !finite(centre + i, 16) || !finite(eye + i, 16))
			{
				continue;
			}
			bool known = false;
			for (const matrix& m : matrices_)
			{
				known = known || same(m.centre, centre + i, 16);
			}
			if (!known)
			{
				matrix m;
				std::memcpy(m.centre, centre + i, sizeof(m.centre));
				std::memcpy(m.eye, eye + i, sizeof(m.eye));
				matrices_.push_back(m);
			}
		}
		for (std::size_t i = 0; i + 3 <= floats; i += 4)
		{
			if (same(centre + i, eye + i, 3) || all_zero(centre + i, 3) || !finite(centre + i, 3) || !finite(eye + i, 3))
			{
				continue;
			}
			bool known = false;
			for (const position& p : positions_)
			{
				known = known || same(p.centre, centre + i, 3);
			}
			if (!known)
			{
				position p;
				std::memcpy(p.centre, centre + i, sizeof(p.centre));
				std::memcpy(p.eye, eye + i, sizeof(p.eye));
				positions_.push_back(p);
			}
		}
		rebuild_index();
	}

	int constant_patch::shift_view_projections(float* block, std::size_t floats, float sx, float sy, float d)
	{
		const auto close = [](float a, float b) { return std::fabs(a - b) <= 2e-3f * std::fabs(b) + 1e-5f; };
		const auto length3 = [](float a, float b, float c) { return std::sqrt(a * a + b * b + c * c); };
		int shifted = 0;
		for (std::size_t i = 0; i + 16 <= floats; i += 4)
		{
			float* m = block + i;
			if (!finite(m, 16))
			{
				continue;
			}
			// Rows are clip x, y, z, w (translation in column 3): the z and w rows share their
			// direction (infinite far plane), x and y rows have the projection's scales.
			const bool rows = close(length3(m[0], m[1], m[2]), sx) && close(length3(m[4], m[5], m[6]), sy)
				&& close(length3(m[12], m[13], m[14]), 1.0f) && same(m + 8, m + 12, 3);
			// Columns are clip x, y, z, w (translation in row 3).
			const bool columns = close(length3(m[0], m[4], m[8]), sx) && close(length3(m[1], m[5], m[9]), sy)
				&& close(length3(m[3], m[7], m[11]), 1.0f) && std::fabs(m[2] - m[3]) <= 1e-5f && std::fabs(m[6] - m[7]) <= 1e-5f
				&& std::fabs(m[10] - m[11]) <= 1e-5f;
			if (rows && (m[3] != 0.0f || m[7] != 0.0f))
			{
				m[3] -= d * sx;
				++shifted;
				i += 12;
			}
			else if (columns && (m[12] != 0.0f || m[13] != 0.0f))
			{
				m[12] -= d * sx;
				++shifted;
				i += 12;
			}
		}
		return shifted;
	}

	bool constant_patch::shift_per_view_block(float* block, std::size_t floats, float d)
	{
		if (floats < 0x150 / 4 || !finite(block, 0x150 / 4))
		{
			return false;
		}
		float* view = block + 0x30 / 4;          // rows: R | t
		float* projection = block + 0x70 / 4;
		float* view_projection = block + 0xb0 / 4;
		const float* scales = block + 0x130 / 4;
		float* camera = block + 0x140 / 4;
		const auto near_value = [](float a, float b) { return std::fabs(a - b) <= 1e-3f * std::fabs(b) + 1e-4f; };
		// Rows of unit length, or, with the camera hook's gun size factor g (gun_scale, gun_life_size),
		// right and up of 1/g and forward of 1/g^2 (live, g = 0.466: 2.146, 2.146, 4.605): the
		// projection divides x and y by depth, so the gun shrinks by g. Aiming down the sights the gun
		// pass narrows to 17 deg and g to 0.168 (rows 5.95, 2026-11-04): up to g = 0.04 is accepted.
		float lengths[3];
		for (int r = 0; r < 3; ++r)
		{
			const float* row = view + r * 4;
			lengths[r] = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
		}
		if (!near_value(lengths[1], lengths[0]) || !near_value(lengths[2], lengths[0] * lengths[0]) || lengths[0] < 0.2f || lengths[0] > 25.0f)
		{
			return false;
		}
		const float sx = projection[0], sy = projection[5];
		if (sx <= 0.0f || sy <= 0.0f || !near_value(scales[0], sx) || !near_value(scales[1], sy) || !near_value(scales[2], 1.0f))
		{
			return false;
		}
		// Camera position = -U^T (t / length) with U the unit rows (row = length * unit, t = -length * unit.p).
		for (int axis = 0; axis < 3; ++axis)
		{
			float expected = 0.0f;
			for (int r = 0; r < 3; ++r)
			{
				expected -= view[r * 4 + axis] * view[r * 4 + 3] / (lengths[r] * lengths[r]);
			}
			if (!near_value(camera[axis], expected))
			{
				return false;
			}
		}
		// View-projection row 0 = sx * view row 0, translation included (x scale of the same camera).
		for (int c = 0; c < 4; ++c)
		{
			if (!near_value(view_projection[c], sx * view[c]))
			{
				return false;
			}
		}
		// Moving the camera d along its unit right moves view-space x by d times the row's length.
		const float right[3] = { view[0] / lengths[0], view[1] / lengths[0], view[2] / lengths[0] };
		view[3] -= d * lengths[0];
		view_projection[3] -= d * sx * lengths[0];
		for (int axis = 0; axis < 3; ++axis)
		{
			camera[axis] += d * right[axis];
		}
		return true;
	}

	int constant_patch::apply(float* block, std::size_t floats) const
	{
		// same() allows 1e-6 + 1e-5 |centre|; the search window is a little wider.
		const auto candidates = [](const index& sorted, float value) {
			const float tolerance = 2e-6f + 2e-5f * std::fabs(value);
			const auto first = std::lower_bound(sorted.begin(), sorted.end(), std::make_pair(value - tolerance, std::uint32_t{ 0 }));
			return std::make_pair(first, value + tolerance);
		};
		int replaced = 0;
		std::size_t i = 0;
		while (i + 3 <= floats)
		{
			const float value = block[i];
			if (!may_match(value))
			{
				i += 4;
				continue;
			}
			bool done = false;
			if (i + 16 <= floats && !matrix_index_.empty())
			{
				const auto [it, last] = candidates(matrix_index_, value);
				for (auto entry = it; entry != matrix_index_.end() && entry->first <= last; ++entry)
				{
					const matrix& m = matrices_[entry->second];
					if (same(block + i, m.centre, 16))
					{
						std::memcpy(block + i, m.eye, sizeof(m.eye));
						++replaced;
						i += 16;
						done = true;
						break;
					}
				}
			}
			if (done)
			{
				continue;
			}
			if (!position_index_.empty())
			{
				const auto [it, last] = candidates(position_index_, value);
				for (auto entry = it; entry != position_index_.end() && entry->first <= last; ++entry)
				{
					const position& p = positions_[entry->second];
					if (same(block + i, p.centre, 3))
					{
						std::memcpy(block + i, p.eye, sizeof(p.eye));
						++replaced;
						break;
					}
				}
			}
			i += 4;
		}
		return replaced;
	}
}
