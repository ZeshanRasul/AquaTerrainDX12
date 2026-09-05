#pragma once
#include "ScalarGrid.h"

class FaceCenteredVelocityGrid3
{
public:
	FaceCenteredVelocityGrid3() = default;

	FaceCenteredVelocityGrid3(
		Size3 resolution,
		Vector3 gridSpacing = { 1.0, 1.0, 1.0 },
		Vector3 origin = {},
		Vector3 initialValue = {})
	{
		Resize(resolution, gridSpacing, origin, initialValue);
	}

	Real& U(std::size_t i, std::size_t j, std::size_t k)
	{
		return m_U(i, j, k);
	}

	const Real& U(
		std::size_t i,
		std::size_t j,
		std::size_t k) const
	{
		return m_U(i, j, k);
	}

	Real& V(std::size_t i, std::size_t j, std::size_t k)
	{
		return m_V(i, j, k);
	}

	const Real& V(
		std::size_t i,
		std::size_t j,
		std::size_t k) const
	{
		return m_V(i, j, k);
	}

	Real& W(std::size_t i, std::size_t j, std::size_t k)
	{
		return m_W(i, j, k);
	}

	const Real& W(
		std::size_t i,
		std::size_t j,
		std::size_t k) const
	{
		return m_W(i, j, k);
	}

	[[nodiscard]] Vector3 UPosition(
		std::size_t i,
		std::size_t j,
		std::size_t k) const;

	[[nodiscard]] Vector3 VPosition(
		std::size_t i,
		std::size_t j,
		std::size_t k) const;

	[[nodiscard]] Vector3 WPosition(
		std::size_t i,
		std::size_t j,
		std::size_t k) const;

	Vector3 Sample(const Vector3& position) const
	{
		return {
			m_U.Sample(position),
			m_V.Sample(position),
			m_W.Sample(position)
		};
	}

	[[nodiscard]] Real SampleU(const Vector3& position) const
	{
		return m_U.Sample(position);
	}

	[[nodiscard]] Real SampleV(const Vector3& position) const
	{
		return m_V.Sample(position);
	}

	[[nodiscard]] Real SampleW(const Vector3& position) const
	{
		return m_W.Sample(position);
	}

	[[nodiscard]] Size3 Resolution() const noexcept
	{
		return m_Resolution;
	}

	[[nodiscard]] Size3 UResolution() const noexcept
	{
		return m_U.Resolution();
	}

	[[nodiscard]] Size3 VResolution() const noexcept
	{
		return m_V.Resolution();
	}

	[[nodiscard]] Size3 WResolution() const noexcept
	{
		return m_W.Resolution();
	}

	[[nodiscard]] Vector3 GridSpacing() const noexcept
	{
		return m_GridSpacing;
	}
	[[nodiscard]] Vector3 Origin() const noexcept
	{
		return m_Origin;
	}

	Vector3 ValueAtCellCenter(std::size_t i, std::size_t j, std::size_t k) const
	{
		return {
			0.5 * (m_U(i, j, k) + m_U(i + 1, j, k)),
			0.5 * (m_V(i, j, k) + m_V(i, j + 1, k)),
			0.5 * (m_W(i, j, k) + m_W(i, j, k + 1))
		};
	}

	Real DivergenceAtCellCenter(std::size_t i, std::size_t j, std::size_t k) const
	{
		return
			(m_U(i + 1, j, k) - m_U(i, j, k))
			/ m_GridSpacing.x
			+
			(m_V(i, j + 1, k) - m_V(i, j, k))
			/ m_GridSpacing.y
			+
			(m_W(i, j, k + 1) - m_W(i, j, k))
			/ m_GridSpacing.z;
	}

	void Fill(const Vector3& value)
	{
		m_U.Fill(value.x);
		m_V.Fill(value.y);
		m_W.Fill(value.z);
	}

	void Resize(
		Size3 resolution,
		Vector3 gridSpacing,
		Vector3 origin,
		Vector3 initialValue = {})
	{
		m_Resolution = resolution;
		m_GridSpacing = gridSpacing;
		m_Origin = origin;

		m_U.Resize(
			{ resolution.x + 1, resolution.y, resolution.z },
			gridSpacing,
			{
				origin.x - 0.5 * gridSpacing.x,
				origin.y,
				origin.z
			},
			initialValue.x);

		m_V.Resize(
			{ resolution.x, resolution.y + 1, resolution.z },
			gridSpacing,
			{
				origin.x,
				origin.y - 0.5 * gridSpacing.y,
				origin.z
			},
			initialValue.y);

		m_W.Resize(
			{ resolution.x, resolution.y, resolution.z + 1 },
			gridSpacing,
			{
				origin.x,
				origin.y,
				origin.z - 0.5 * gridSpacing.z
			},
			initialValue.z);
	}

private:
	Size3 m_Resolution{};
	Vector3 m_GridSpacing{ 1.0, 1.0, 1.0 };
	Vector3 m_Origin{};


	ScalarGrid3 m_U;
	ScalarGrid3 m_V;
	ScalarGrid3 m_W;
};