#include "SmokeSolver3.h"
#include "DataStructures/GridOperators3.h"
#include <cassert>

SmokeSolver3::SmokeSolver3(
	Size3 resolution,
	Vector3 gridSpacing,
	Vector3 origin)
	: m_Velocity(
		resolution,
		gridSpacing,
		origin,
		{ 0.0, 0.0, 0.0 }),

	m_Density(
		resolution,
		gridSpacing,
		origin,
		0.0),

	m_Temperature(
		resolution,
		gridSpacing,
		origin,
		0.0),

	m_Pressure(
		resolution,
		gridSpacing,
		origin,
		0.0),

	m_Divergence(
		resolution,
		gridSpacing,
		origin,
		0.0),

	m_VelocityScratch(
		resolution,
		gridSpacing,
		origin,
		{ 0.0, 0.0, 0.0 }),

	m_ScalarScratch(
		resolution,
		gridSpacing,
		origin,
		0.0),

	m_PressureScratch(
		resolution,
		gridSpacing,
		origin,
		0.0)
{
	assert(resolution.x > 1);
	assert(resolution.y > 1);
	assert(resolution.z > 1);

	assert(gridSpacing.x > 0.0);
	assert(gridSpacing.y > 0.0);
	assert(gridSpacing.z > 0.0);
}

void SmokeSolver3::Step(Real dt)
{
	ApplyExternalForces(dt);

	AdvectVelocity(dt);

	Project(dt);

	AdvectScalars(dt);
}

void SmokeSolver3::AddSourceRates(std::size_t i, std::size_t j, std::size_t k, Real densityRate, Real temperatureRate, Vector3 acceleration, Real dt)
{
	const Size3 resolution = m_Density.Resolution();

	if (i >= resolution.x ||
		j >= resolution.y ||
		k >= resolution.z)
	{
		return;
	}

	m_Density(i, j, k) += densityRate * dt;
	m_Temperature(i, j, k) += temperatureRate * dt;

	// Give both faces surrounding the cell the same acceleration.
	m_Velocity.U(i, j, k) += acceleration.x * dt;
	m_Velocity.U(i + 1, j, k) += acceleration.x * dt;

	m_Velocity.V(i, j, k) += acceleration.y * dt;
	m_Velocity.V(i, j + 1, k) += acceleration.y * dt;

	m_Velocity.W(i, j, k) += acceleration.z * dt;
	m_Velocity.W(i, j, k + 1) += acceleration.z * dt;

}

void SmokeSolver3::ApplyExternalForces(Real dt)
{
}

void SmokeSolver3::AdvectVelocity(Real dt)
{
	GridOperators3::Advect(
		m_Velocity,
		dt,
		m_VelocityScratch);

	std::swap(m_Velocity, m_VelocityScratch);
}

void SmokeSolver3::Project(Real dt)
{
	GridOperators3::SetClosedDomainBoundary(m_Velocity);

	const Real divergenceBefore = GridOperators3::CalculateRmsDivergence(m_Velocity);

	GridOperators3::ComputeDivergence(
		m_Velocity,
		m_Divergence);

	SolvePressurePoisson(
		m_Divergence,
		m_Pressure,
		dt,
		m_FluidDensity);

	GridOperators3::SubtractPressureGradient(
		m_Velocity,
		m_Pressure,
		dt / m_FluidDensity);

	GridOperators3::SetClosedDomainBoundary(m_Velocity);

	const Real divergenceAfter =
		GridOperators3::CalculateRmsDivergence(m_Velocity);

	m_LastRmsDivergenceBeforeProjection = divergenceBefore;
	m_LastRmsDivergenceAfterProjection = divergenceAfter;


}

void SmokeSolver3::AdvectScalars(Real dt)
{
	GridOperators3::Advect(
		m_Density,
		m_Velocity,
		dt,
		m_ScalarScratch);

	std::swap(m_Density, m_ScalarScratch);

	GridOperators3::Advect(
		m_Temperature,
		m_Velocity,
		dt,
		m_ScalarScratch);

	std::swap(m_Temperature, m_ScalarScratch);
}

void SmokeSolver3::SolvePressurePoisson(const ScalarGrid3& divergence, ScalarGrid3& pressure, Real dt, Real fluidDensity)
{
	assert(dt > 0.0);
	assert(fluidDensity > 0.0);

	const Size3 resolution = pressure.Resolution();
	const Vector3 h = pressure.GridSpacing();

	const Real ax = 1.0 / (h.x * h.x);
	const Real ay = 1.0 / (h.y * h.y);
	const Real az = 1.0 / (h.z * h.z);

	const Real rhsScale = fluidDensity / dt;

	pressure.Fill(0.0);
	m_PressureScratch.Fill(0.0);

	constexpr std::size_t pressureIterations = 80;

	for (std::size_t iteration = 0;
		iteration < pressureIterations;
		++iteration)
	{
		for (std::size_t k = 0; k < resolution.z; ++k)
		{
			for (std::size_t j = 0; j < resolution.y; ++j)
			{
				for (std::size_t i = 0;
					i < resolution.x;
					++i)
				{
					Real neighbourSum = 0.0;
					Real diagonal = 0.0;

					if (i > 0)
					{
						neighbourSum +=
							ax * pressure(i - 1, j, k);
						diagonal += ax;
					}

					if (i + 1 < resolution.x)
					{
						neighbourSum +=
							ax * pressure(i + 1, j, k);
						diagonal += ax;
					}

					if (j > 0)
					{
						neighbourSum +=
							ay * pressure(i, j - 1, k);
						diagonal += ay;
					}

					if (j + 1 < resolution.y)
					{
						neighbourSum +=
							ay * pressure(i, j + 1, k);
						diagonal += ay;
					}

					if (k > 0)
					{
						neighbourSum +=
							az * pressure(i, j, k - 1);
						diagonal += az;
					}

					if (k + 1 < resolution.z)
					{
						neighbourSum +=
							az * pressure(i, j, k + 1);
						diagonal += az;
					}

					const Real rhs =
						rhsScale * divergence(i, j, k);

					m_PressureScratch(i, j, k) =
						(neighbourSum - rhs) / diagonal;
				}
			}
		}

		std::swap(pressure, m_PressureScratch);
	}

}