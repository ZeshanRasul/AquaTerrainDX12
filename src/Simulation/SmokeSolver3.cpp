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
		0.0),

	m_PcgResidual(
		resolution,
		gridSpacing,
		origin,
		0.0),

	m_PcgPreconditioned(
		resolution,
		gridSpacing,
		origin,
		0.0),

	m_PcgDirection(
		resolution,
		gridSpacing,
		origin,
		0.0),

	m_PcgMatrixDirection(
		resolution,
		gridSpacing,
		origin,
		0.0),
	m_PhysicsParameters(SmokePhysicsParameters())
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
	if (dt <= Real{ 0.0 })
		return;

	AdvectVelocity(dt);

	ApplyExternalForces(dt);

	Project(dt);

	AdvectScalars(dt);

	ApplyScalarDissipation(dt);
}

void SmokeSolver3::Reset() noexcept
{
	m_Velocity.Fill({ 0.0, 0.0, 0.0 });
	m_Density.Fill(0.0);
	m_Temperature.Fill(0.0);
	m_Pressure.Fill(0.0);
	m_Divergence.Fill(0.0);
	m_VelocityScratch.Fill({ 0.0, 0.0, 0.0 });
	m_ScalarScratch.Fill(0.0);
	m_PressureScratch.Fill(0.0);
	m_PcgResidual.Fill(0.0);
	m_PcgPreconditioned.Fill(0.0);
	m_PcgDirection.Fill(0.0);
	m_PcgMatrixDirection.Fill(0.0);

	m_LastRmsDivergenceBeforeProjection = 0.0;
	m_LastRmsDivergenceAfterProjection = 0.0;
	m_LastScaledPressureResidual = 0.0;
	m_LastPressureIterations = 0;
	m_LastMeanDivergence = 0.0;
}

void SmokeSolver3::ApplyPressureMatrix(
	const ScalarGrid3& input,
	ScalarGrid3& output)
{
	const Size3 resolution = input.Resolution();
	const Vector3 h = input.GridSpacing();

	const Real ax = 1.0 / (h.x * h.x);
	const Real ay = 1.0 / (h.y * h.y);
	const Real az = 1.0 / (h.z * h.z);

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				Real diagonal = 0.0;
				Real neighbourSum = 0.0;

				if (i > 0)
				{
					diagonal += ax;
					neighbourSum +=
						ax * input(i - 1, j, k);
				}

				if (i + 1 < resolution.x)
				{
					diagonal += ax;
					neighbourSum +=
						ax * input(i + 1, j, k);
				}

				if (j > 0)
				{
					diagonal += ay;
					neighbourSum +=
						ay * input(i, j - 1, k);
				}

				if (j + 1 < resolution.y)
				{
					diagonal += ay;
					neighbourSum +=
						ay * input(i, j + 1, k);
				}

				if (k > 0)
				{
					diagonal += az;
					neighbourSum +=
						az * input(i, j, k - 1);
				}

				if (k + 1 < resolution.z)
				{
					diagonal += az;
					neighbourSum +=
						az * input(i, j, k + 1);
				}

				output(i, j, k) =
					diagonal * input(i, j, k) -
					neighbourSum;
			}
		}
	}
}

void SmokeSolver3::ApplyJacobiPreconditioner(const ScalarGrid3& residual, ScalarGrid3& result)
{
	const Size3 resolution = residual.Resolution();
	const Vector3 h = residual.GridSpacing();

	const Real ax = 1.0 / (h.x * h.x);
	const Real ay = 1.0 / (h.y * h.y);
	const Real az = 1.0 / (h.z * h.z);

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				Real diagonal = 0.0;

				if (i > 0)
					diagonal += ax;
				if (i + 1 < resolution.x)
					diagonal += ax;

				if (j > 0)
					diagonal += ay;
				if (j + 1 < resolution.y)
					diagonal += ay;

				if (k > 0)
					diagonal += az;
				if (k + 1 < resolution.z)
					diagonal += az;

				result(i, j, k) =
					residual(i, j, k) / diagonal;
			}
		}
	}

	// The closed-box matrix cannot determine constant pressure.
	RemoveMean(result);
}

Real SmokeSolver3::PressureResidualRms(Real dt) const
{
	const Size3 resolution = m_Pressure.Resolution();
	const Vector3 h = m_Pressure.GridSpacing();

	const Real ax = 1.0 / (h.x * h.x);
	const Real ay = 1.0 / (h.y * h.y);
	const Real az = 1.0 / (h.z * h.z);

	Real sumSquared = 0.0;
	std::size_t count = 0;

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				Real neighbourSum = 0.0;
				Real diagonal = 0.0;

				if (i > 0)
				{
					neighbourSum +=
						ax * m_Pressure(i - 1, j, k);
					diagonal += ax;
				}

				if (i + 1 < resolution.x)
				{
					neighbourSum +=
						ax * m_Pressure(i + 1, j, k);
					diagonal += ax;
				}

				if (j > 0)
				{
					neighbourSum +=
						ay * m_Pressure(i, j - 1, k);
					diagonal += ay;
				}

				if (j + 1 < resolution.y)
				{
					neighbourSum +=
						ay * m_Pressure(i, j + 1, k);
					diagonal += ay;
				}

				if (k > 0)
				{
					neighbourSum +=
						az * m_Pressure(i, j, k - 1);
					diagonal += az;
				}

				if (k + 1 < resolution.z)
				{
					neighbourSum +=
						az * m_Pressure(i, j, k + 1);
					diagonal += az;
				}

				const Real laplacian =
					neighbourSum -
					diagonal * m_Pressure(i, j, k);

				const Real rhs =
					(m_FluidDensity / dt) *
					m_Divergence(i, j, k);

				const Real residual = rhs - laplacian;

				sumSquared += residual * residual;
				++count;
			}
		}
	}

	return std::sqrt(
		sumSquared / static_cast<Real>(count));
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
	if (dt <= Real{ 0.0 })
		return;

	const Size3 resolution = m_Density.Resolution();

	const Real ambient =
		m_PhysicsParameters.ambientTemperature;

	const Real temperatureBuoyancy =
		m_PhysicsParameters.temperatureBuoyancy;

	const Real smokeWeight =
		m_PhysicsParameters.smokeWeight;

	// j = 1 ... resolution.y - 1:
	// V(i,j,k) lies between cells (i,j-1,k) and (i,j,k).
	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 1; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				const Real densityBelow =
					std::max(m_Density(i, j - 1, k), Real{ 0.0 });

				const Real densityAbove =
					std::max(m_Density(i, j, k), Real{ 0.0 });

				const Real temperatureBelow =
					m_Temperature(i, j - 1, k);

				const Real temperatureAbove =
					m_Temperature(i, j, k);

				const Real densityAtFace =
					Real{ 0.5 } * (densityBelow + densityAbove);

				const Real temperatureAtFace =
					Real{ 0.5 } *
					(temperatureBelow + temperatureAbove);

				// This clamp gives intuitive game-smoke behaviour:
				// cooled smoke does not acquire downward thermal buoyancy.
				const Real excessTemperature =
					std::max(
						temperatureAtFace - ambient,
						Real{ 0.0 });

				const Real upwardAcceleration =
					temperatureBuoyancy * excessTemperature -
					smokeWeight * densityAtFace;

				m_Velocity.V(i, j, k) +=
					dt * upwardAcceleration;
			}
		}
	}
}

void SmokeSolver3::ApplyScalarDissipation(Real dt)
{
	if (dt <= Real{ 0.0 })
		return;

	const Real densityDecay = std::exp(
		-m_PhysicsParameters.densityDissipation * dt);

	const Real temperatureDecay = std::exp(
		-m_PhysicsParameters.temperatureCooling * dt);

	const Real ambient =
		m_PhysicsParameters.ambientTemperature;

	const Size3 resolution = m_Density.Resolution();

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				m_Density(i, j, k) = std::max(
					m_Density(i, j, k) * densityDecay,
					Real{ 0.0 });

				const Real excessTemperature =
					m_Temperature(i, j, k) - ambient;

				m_Temperature(i, j, k) =
					ambient +
					excessTemperature * temperatureDecay;
			}
		}
	}
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

	SolvePressurePoissonPCG(
		m_Divergence,
		m_Pressure,
		dt,
		m_FluidDensity);

	m_LastScaledPressureResidual =
		(dt / m_FluidDensity) *
		PressureResidualRms(dt);


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

	constexpr std::size_t pressureIterations = 400;

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

void SmokeSolver3::SolvePressurePoissonPCG(
	const ScalarGrid3& divergence,
	ScalarGrid3& pressure,
	Real dt,
	Real fluidDensity)
{
	assert(dt > 0.0);
	assert(fluidDensity > 0.0);

	constexpr std::size_t maximumIterations = 100;
	constexpr Real relativeTolerance = 0.01;
	constexpr Real absoluteTolerance = 1e-6;
	constexpr Real breakdownTolerance = 1e-30;

	const Real divergenceMean = Mean(divergence);
	const Real rhsScale = fluidDensity / dt;

	pressure.Fill(0.0);

	const auto divergenceData = divergence.Data();
	auto residualData = m_PcgResidual.Data();

	Real compatibleDivergenceSquared = 0.0;

	for (std::size_t i = 0;
		i < divergenceData.size();
		++i)
	{
		const Real compatibleDivergence =
			divergenceData[i] - divergenceMean;

		// b = -(rho / dt) * divergence
		residualData[i] =
			-rhsScale * compatibleDivergence;

		compatibleDivergenceSquared +=
			compatibleDivergence *
			compatibleDivergence;
	}

	RemoveMean(m_PcgResidual);

	const Real initialDivergenceRms = std::sqrt(
		compatibleDivergenceSquared /
		static_cast<Real>(divergenceData.size()));

	const Real targetDivergence = std::max(
		absoluteTolerance,
		relativeTolerance * initialDivergenceRms);

	ApplyJacobiPreconditioner(
		m_PcgResidual,
		m_PcgPreconditioned);

	// d = z
	std::copy(
		m_PcgPreconditioned.Data().begin(),
		m_PcgPreconditioned.Data().end(),
		m_PcgDirection.Data().begin());

	Real residualPreconditionedDot =
		Dot(m_PcgResidual, m_PcgPreconditioned);

	m_LastPressureIterations = 0;
	m_LastScaledPressureResidual =
		initialDivergenceRms;

	for (std::size_t iteration = 0;
		iteration < maximumIterations;
		++iteration)
	{
		ApplyPressureMatrix(
			m_PcgDirection,
			m_PcgMatrixDirection);

		const Real denominator =
			Dot(
				m_PcgDirection,
				m_PcgMatrixDirection);

		if (!(denominator > breakdownTolerance))
			break;

		const Real alpha =
			residualPreconditionedDot / denominator;

		auto pressureData = pressure.Data();
		auto r = m_PcgResidual.Data();
		const auto d = m_PcgDirection.Data();
		const auto q = m_PcgMatrixDirection.Data();

		for (std::size_t index = 0;
			index < pressureData.size();
			++index)
		{
			pressureData[index] +=
				alpha * d[index];

			r[index] -=
				alpha * q[index];
		}

		// Convert pressure-equation residual back into
		// equivalent velocity-divergence units.
		const Real scaledResidual =
			(dt / fluidDensity) *
			Rms(m_PcgResidual);

		m_LastScaledPressureResidual =
			scaledResidual;

		m_LastPressureIterations =
			iteration + 1;

		if (scaledResidual <= targetDivergence)
			break;

		ApplyJacobiPreconditioner(
			m_PcgResidual,
			m_PcgPreconditioned);

		const Real newDot =
			Dot(
				m_PcgResidual,
				m_PcgPreconditioned);

		if (!(newDot > breakdownTolerance))
			break;

		const Real beta =
			newDot / residualPreconditionedDot;

		auto directionData = m_PcgDirection.Data();
		const auto z = m_PcgPreconditioned.Data();

		for (std::size_t index = 0;
			index < directionData.size();
			++index)
		{
			directionData[index] =
				z[index] +
				beta * directionData[index];
		}

		RemoveMean(m_PcgDirection);

		residualPreconditionedDot = newDot;
	}

	// Select the unique zero-mean representative from the
	// closed-box pressure solutions.
	RemoveMean(pressure);

	m_LastMeanDivergence = divergenceMean;
}
