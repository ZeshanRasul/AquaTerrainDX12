#include "StableFluids3D.h"
#include <cmath>
#include <algorithm>
#include <cassert>
#include <stdexcept>

StableFluids3D::StableFluids3D(
	int width,
	int height,
	int depth)
	: m_Width(width),
	m_Height(height),
	m_Depth(depth)
{
	if (width <= 0 || height <= 0 || depth <= 0)
		throw std::invalid_argument(
			"Fluid dimensions must be positive.");

	m_StrideY = m_Width + 2;
	m_StrideZ = (m_Width + 2) * (m_Height + 2);

	const std::size_t cellCount =
		static_cast<std::size_t>(m_Width + 2) *
		static_cast<std::size_t>(m_Height + 2) *
		static_cast<std::size_t>(m_Depth + 2);

	auto initialise = [cellCount](std::vector<float>& field)
		{
			field.assign(cellCount, 0.0f);
		};

	initialise(m_Density);
	initialise(m_DensitySource);
	initialise(m_DensityWork);

	initialise(m_Temperature);
	initialise(m_TemperatureSource);

	initialise(m_U);
	initialise(m_V);
	initialise(m_W);

	initialise(m_USource);
	initialise(m_VSource);
	initialise(m_WSource);

	initialise(m_UWork);
	initialise(m_VWork);
	initialise(m_WWork);

	initialise(m_Pressure);
	initialise(m_Divergence);
}

void StableFluids3D::AddSources(
	std::vector<float>& destination,
	const std::vector<float>& source,
	float dt)
{
	assert(destination.size() == source.size());

	for (std::size_t index = 0;
		index < destination.size();
		++index)
	{
		destination[index] += dt * source[index];
	}
}

void StableFluids3D::AddSourceRates(
	int i,
	int j,
	int k,
	float densityRate,
	float forceX,
	float forceY,
	float forceZ)
{
	// Sources must be placed in interior cells.
	if (i < 1 || i > m_Width ||
		j < 1 || j > m_Height ||
		k < 1 || k > m_Depth)
	{
		return;
	}

	const std::size_t index = Index(i, j, k);

	// Accumulate so that several emitters may affect the same cell.
	m_DensitySource[index] += densityRate;

	m_USource[index] += forceX;
	m_VSource[index] += forceY;
	m_WSource[index] += forceZ;
}

void StableFluids3D::Diffuse(
	BoundaryType boundary,
	std::vector<float>& destination,
	const std::vector<float>& source,
	float diffusion,
	float dt)
{
	if (diffusion == 0.0f)
	{
		destination = source;
		SetBoundary(boundary, destination);
		return;
	}

	const float hx = 1.0f / static_cast<float>(m_Width);
	const float hy = 1.0f / static_cast<float>(m_Height);
	const float hz = 1.0f / static_cast<float>(m_Depth);

	const float ax = dt * diffusion / (hx * hx);
	const float ay = dt * diffusion / (hy * hy);
	const float az = dt * diffusion / (hz * hz);

	const float denominator =
		1.0f + 2.0f * (ax + ay + az);

	constexpr int relaxationIterations = 20;

	for (int iteration = 0;
		iteration < relaxationIterations;
		++iteration)
	{
		for (int k = 1; k <= m_Depth; ++k)
		{
			for (int j = 1; j <= m_Height; ++j)
			{
				for (int i = 1; i <= m_Width; ++i)
				{
					destination[Index(i, j, k)] =
						(
							source[Index(i, j, k)] +

							ax * (
								destination[Index(i - 1, j, k)] +
								destination[Index(i + 1, j, k)]
								) +

							ay * (
								destination[Index(i, j - 1, k)] +
								destination[Index(i, j + 1, k)]
								) +

							az * (
								destination[Index(i, j, k - 1)] +
								destination[Index(i, j, k + 1)]
								)
							) / denominator;
				}
			}
		}

		SetBoundary(boundary, destination);
	}
}

void StableFluids3D::Advect(
	BoundaryType boundaryType,
	std::vector<float>& destination,
	const std::vector<float>& source,
	const std::vector<float>& velocityX,
	const std::vector<float>& velocityY,
	const std::vector<float>& velocityZ,
	float dt)
{
	// For a unit-sized simulation domain.
	const float dtX = dt * static_cast<float>(m_Width);
	const float dtY = dt * static_cast<float>(m_Height);
	const float dtZ = dt * static_cast<float>(m_Depth);

	for (int k = 1; k <= m_Depth; ++k)
	{
		for (int j = 1; j <= m_Height; ++j)
		{
			for (int i = 1; i <= m_Width; ++i)
			{
				const std::size_t index = Index(i, j, k);

				// Trace the cell centre backwards through the velocity field.
				const float previousX =
					static_cast<float>(i) - dtX * velocityX[index];

				const float previousY =
					static_cast<float>(j) - dtY * velocityY[index];

				const float previousZ =
					static_cast<float>(k) - dtZ * velocityZ[index];

				destination[index] = SampleTrilinear(
					source,
					previousX,
					previousY,
					previousZ);
			}
		}
	}

	SetBoundary(boundaryType, destination);
}

void StableFluids3D::SetBoundary(
	BoundaryType boundary,
	std::vector<float>& field)
{
	for (int k = 0; k <= m_Depth + 1; ++k)
	{
		for (int j = 0; j <= m_Height + 1; ++j)
		{
			for (int i = 0; i <= m_Width + 1; ++i)
			{
				const bool onXWall =
					i == 0 || i == m_Width + 1;

				const bool onYWall =
					j == 0 || j == m_Height + 1;

				const bool onZWall =
					k == 0 || k == m_Depth + 1;

				if (!onXWall && !onYWall && !onZWall)
					continue;

				// Nearest interior cell. This also handles edges/corners.
				const int interiorI =
					std::clamp(i, 1, m_Width);

				const int interiorJ =
					std::clamp(j, 1, m_Height);

				const int interiorK =
					std::clamp(k, 1, m_Depth);

				float sign = 1.0f;

				if (boundary == BoundaryType::VelocityX &&
					onXWall)
				{
					sign = -1.0f;
				}
				else if (
					boundary == BoundaryType::VelocityY &&
					onYWall)
				{
					sign = -1.0f;
				}
				else if (
					boundary == BoundaryType::VelocityZ &&
					onZWall)
				{
					sign = -1.0f;
				}

				field[Index(i, j, k)] =
					sign * field[Index(
						interiorI,
						interiorJ,
						interiorK)];
			}
		}
	}
}

void StableFluids3D::Step(float stepDt, float diffusion, float viscosity)
{
	AddSources(m_U, m_USource, stepDt);
	AddSources(m_V, m_VSource, stepDt);
	AddSources(m_W, m_WSource, stepDt);

	std::swap(m_U, m_UWork);
	Diffuse(BoundaryType::VelocityX, m_U, m_UWork, viscosity, stepDt);

	std::swap(m_V, m_VWork);
	Diffuse(BoundaryType::VelocityY, m_V, m_VWork, viscosity, stepDt);

	std::swap(m_W, m_WWork);
	Diffuse(BoundaryType::VelocityZ, m_W, m_WWork, viscosity, stepDt);

	Project(
		m_U, m_V, m_W,
		m_Pressure, m_Divergence);

	std::swap(m_U, m_UWork);
	std::swap(m_V, m_VWork);
	std::swap(m_W, m_WWork);

	Advect(
		BoundaryType::VelocityX,
		m_U, m_UWork,
		m_UWork, m_VWork, m_WWork,
		stepDt);

	Advect(
		BoundaryType::VelocityY,
		m_V, m_VWork,
		m_UWork, m_VWork, m_WWork,
		stepDt);

	Advect(
		BoundaryType::VelocityZ,
		m_W, m_WWork,
		m_UWork, m_VWork, m_WWork,
		stepDt);

	Project(
		m_U, m_V, m_W,
		m_Pressure, m_Divergence);

	AddSources(m_Density, m_DensitySource, stepDt);

	std::swap(m_Density, m_DensityWork);
	Diffuse(
		BoundaryType::Scalar,
		m_Density,
		m_DensityWork,
		diffusion,
		stepDt);

	std::swap(m_Density, m_DensityWork);
	Advect(
		BoundaryType::Scalar,
		m_Density,
		m_DensityWork,
		m_U, m_V, m_W,
		stepDt);

	std::fill(
		m_DensitySource.begin(),
		m_DensitySource.end(),
		0.0f);

	std::fill(
		m_USource.begin(),
		m_USource.end(),
		0.0f);

	std::fill(
		m_VSource.begin(),
		m_VSource.end(),
		0.0f);

	std::fill(
		m_WSource.begin(),
		m_WSource.end(),
		0.0f);
}

void StableFluids3D::Reset()
{
	auto clear = [](std::vector<float>& field)
		{
			std::fill(field.begin(), field.end(), 0.0f);
		};

	clear(m_Density);
	clear(m_DensitySource);
	clear(m_DensityWork);

	clear(m_Temperature);
	clear(m_TemperatureSource);

	clear(m_U);
	clear(m_V);
	clear(m_W);

	clear(m_USource);
	clear(m_VSource);
	clear(m_WSource);

	clear(m_UWork);
	clear(m_VWork);
	clear(m_WWork);

	clear(m_Pressure);
	clear(m_Divergence);

}

void StableFluids3D::Project(
	std::vector<float>& velocityX,
	std::vector<float>& velocityY,
	std::vector<float>& velocityZ,
	std::vector<float>& pressure,
	std::vector<float>& divergence)
{
	const float hx = 1.0f / static_cast<float>(m_Width);
	const float hy = 1.0f / static_cast<float>(m_Height);
	const float hz = 1.0f / static_cast<float>(m_Depth);

	const float inverseTwoHx = 0.5f / hx;
	const float inverseTwoHy = 0.5f / hy;
	const float inverseTwoHz = 0.5f / hz;

	const float inverseHxSquared = 1.0f / (hx * hx);
	const float inverseHySquared = 1.0f / (hy * hy);
	const float inverseHzSquared = 1.0f / (hz * hz);

	const float pressureDenominator =
		2.0f *
		(
			inverseHxSquared +
			inverseHySquared +
			inverseHzSquared
			);

	// Measure divergence and initialise the pressure guess.
	for (int k = 1; k <= m_Depth; ++k)
	{
		for (int j = 1; j <= m_Height; ++j)
		{
			for (int i = 1; i <= m_Width; ++i)
			{
				divergence[Index(i, j, k)] =
					(
						velocityX[Index(i + 1, j, k)] -
						velocityX[Index(i - 1, j, k)]
						) * inverseTwoHx
					+
					(
						velocityY[Index(i, j + 1, k)] -
						velocityY[Index(i, j - 1, k)]
						) * inverseTwoHy
					+
					(
						velocityZ[Index(i, j, k + 1)] -
						velocityZ[Index(i, j, k - 1)]
						) * inverseTwoHz;

				pressure[Index(i, j, k)] = 0.0f;
			}
		}
	}

	SetBoundary(BoundaryType::Scalar, divergence);
	SetBoundary(BoundaryType::Scalar, pressure);

	constexpr int pressureIterations = 20;

	// Solve ∇²p = divergence using Gauss-Seidel relaxation.
	for (int iteration = 0;
		iteration < pressureIterations;
		++iteration)
	{
		for (int k = 1; k <= m_Depth; ++k)
		{
			for (int j = 1; j <= m_Height; ++j)
			{
				for (int i = 1; i <= m_Width; ++i)
				{
					const float neighbourContribution =
						inverseHxSquared *
						(
							pressure[Index(i - 1, j, k)] +
							pressure[Index(i + 1, j, k)]
							)
						+
						inverseHySquared *
						(
							pressure[Index(i, j - 1, k)] +
							pressure[Index(i, j + 1, k)]
							)
						+
						inverseHzSquared *
						(
							pressure[Index(i, j, k - 1)] +
							pressure[Index(i, j, k + 1)]
							);

					pressure[Index(i, j, k)] =
						(
							neighbourContribution -
							divergence[Index(i, j, k)]
							) / pressureDenominator;
				}
			}
		}

		SetBoundary(BoundaryType::Scalar, pressure);
	}

	// Subtract ∇p from all three velocity components.
	for (int k = 1; k <= m_Depth; ++k)
	{
		for (int j = 1; j <= m_Height; ++j)
		{
			for (int i = 1; i <= m_Width; ++i)
			{
				velocityX[Index(i, j, k)] -=
					(
						pressure[Index(i + 1, j, k)] -
						pressure[Index(i - 1, j, k)]
						) * inverseTwoHx;

				velocityY[Index(i, j, k)] -=
					(
						pressure[Index(i, j + 1, k)] -
						pressure[Index(i, j - 1, k)]
						) * inverseTwoHy;

				velocityZ[Index(i, j, k)] -=
					(
						pressure[Index(i, j, k + 1)] -
						pressure[Index(i, j, k - 1)]
						) * inverseTwoHz;
			}
		}
	}

	SetBoundary(BoundaryType::VelocityX, velocityX);
	SetBoundary(BoundaryType::VelocityY, velocityY);
	SetBoundary(BoundaryType::VelocityZ, velocityZ);
}



float StableFluids3D::SampleTrilinear(
	const std::vector<float>& field,
	float x,
	float y,
	float z) const
{
	// The ghost-cell layer occupies indices 0 and resolution + 1.
	x = std::clamp(x, 0.5f, static_cast<float>(m_Width) + 0.5f);
	y = std::clamp(y, 0.5f, static_cast<float>(m_Height) + 0.5f);
	z = std::clamp(z, 0.5f, static_cast<float>(m_Depth) + 0.5f);

	const int i0 = static_cast<int>(std::floor(x));
	const int j0 = static_cast<int>(std::floor(y));
	const int k0 = static_cast<int>(std::floor(z));

	const int i1 = i0 + 1;
	const int j1 = j0 + 1;
	const int k1 = k0 + 1;

	// Fractional position within the surrounding cell cube.
	const float tx = x - static_cast<float>(i0);
	const float ty = y - static_cast<float>(j0);
	const float tz = z - static_cast<float>(k0);

	const auto lerp = [](float a, float b, float t)
		{
			return a + t * (b - a);
		};

	// Eight corners of the surrounding cube.
	const float c000 = field[Index(i0, j0, k0)];
	const float c100 = field[Index(i1, j0, k0)];
	const float c010 = field[Index(i0, j1, k0)];
	const float c110 = field[Index(i1, j1, k0)];

	const float c001 = field[Index(i0, j0, k1)];
	const float c101 = field[Index(i1, j0, k1)];
	const float c011 = field[Index(i0, j1, k1)];
	const float c111 = field[Index(i1, j1, k1)];

	// Interpolate along X.
	const float c00 = lerp(c000, c100, tx);
	const float c10 = lerp(c010, c110, tx);
	const float c01 = lerp(c001, c101, tx);
	const float c11 = lerp(c011, c111, tx);

	// Interpolate the resulting values along Y.
	const float c0 = lerp(c00, c10, ty);
	const float c1 = lerp(c01, c11, ty);

	// Finally interpolate between the two Z planes.
	return lerp(c0, c1, tz);
}
