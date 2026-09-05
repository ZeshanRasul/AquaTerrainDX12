#include "GridOperators3.h"
#include <cmath>

namespace
{
	Vector3 BackTrace(
		const FaceCenteredVelocityGrid3& flow,
		const Vector3& position,
		Real dt)
	{
		const Vector3 velocity0 = flow.Sample(position);

		const Vector3 midpoint{
			position.x - 0.5 * dt * velocity0.x,
			position.y - 0.5 * dt * velocity0.y,
			position.z - 0.5 * dt * velocity0.z
		};

		const Vector3 midpointVelocity = flow.Sample(midpoint);

		return {
			position.x - dt * midpointVelocity.x,
			position.y - dt * midpointVelocity.y,
			position.z - dt * midpointVelocity.z
		};
	}
}

void GridOperators3::ComputeDivergence(const FaceCenteredVelocityGrid3& velocity, ScalarGrid3& divergence)
{
	const size_t resX = divergence.Resolution().x;
	const size_t resY = divergence.Resolution().y;
	const size_t resZ = divergence.Resolution().z;

	for (std::size_t k = 0; k < resZ; ++k)
	{
		for (std::size_t j = 0; j < resY; ++j)
		{
			for (std::size_t i = 0; i < resX; ++i)
			{
				divergence(i, j, k) = velocity.DivergenceAtCellCenter(i, j, k);
			}
		}
	}
}

void GridOperators3::SubtractPressureGradient(FaceCenteredVelocityGrid3& velocity, const ScalarGrid3& pressure, Real scale)
{
	const Size3 resolution = velocity.Resolution();
	const Vector3 h = velocity.GridSpacing();

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 1; i < resolution.x; ++i)
			{
				velocity.U(i, j, k) -= scale *
					(pressure(i, j, k) -
						pressure(i - 1, j, k)) / h.x;
			}
		}
	}

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 1; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				velocity.V(i, j, k) -= scale *
					(pressure(i, j, k) -
						pressure(i, j - 1, k)) / h.y;
			}
		}
	}

	for (std::size_t k = 1; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				velocity.W(i, j, k) -= scale *
					(pressure(i, j, k) -
						pressure(i, j, k - 1)) / h.z;
			}
		}
	}
}

void GridOperators3::SetClosedDomainBoundary(FaceCenteredVelocityGrid3& velocity)
{
	for (std::size_t k = 0; k < velocity.Resolution().z; ++k)
	{
		for (std::size_t j = 0; j < velocity.Resolution().y; ++j)
		{
			velocity.U(0, j, k) = 0.0;
			velocity.U(velocity.Resolution().x, j, k) = 0.0;
		}
	}

	for (std::size_t k = 0; k < velocity.Resolution().z; ++k)
	{
		for (std::size_t i = 0; i < velocity.Resolution().x; ++i)
		{
			velocity.V(i, 0, k) = 0.0;
			velocity.V(i, velocity.Resolution().y, k) = 0.0;
		}
	}

	for (std::size_t j = 0; j < velocity.Resolution().y; ++j)
	{
		for (std::size_t i = 0; i < velocity.Resolution().x; ++i)
		{
			velocity.W(i, j, 0) = 0.0;
			velocity.W(i, j, velocity.Resolution().z) = 0.0;
		}
	}
}

void GridOperators3::Advect(const FaceCenteredVelocityGrid3& source, Real dt, FaceCenteredVelocityGrid3& destination)
{

	const Size3 uSize = source.UResolution();

	for (std::size_t k = 0; k < uSize.z; ++k)
	{
		for (std::size_t j = 0; j < uSize.y; ++j)
		{
			for (std::size_t i = 0; i < uSize.x; ++i)
			{
				const Vector3 departure = BackTrace(
					source,
					source.UPosition(i, j, k),
					dt);

				destination.U(i, j, k) =
					source.SampleU(departure);
			}
		}
	}

	const Size3 vSize = source.VResolution();

	for (std::size_t k = 0; k < vSize.z; ++k)
	{
		for (std::size_t j = 0; j < vSize.y; ++j)
		{
			for (std::size_t i = 0; i < vSize.x; ++i)
			{
				const Vector3 departure = BackTrace(
					source,
					source.VPosition(i, j, k),
					dt);

				destination.V(i, j, k) =
					source.SampleV(departure);
			}
		}
	}

	const Size3 wSize = source.WResolution();

	for (std::size_t k = 0; k < wSize.z; ++k)
	{
		for (std::size_t j = 0; j < wSize.y; ++j)
		{
			for (std::size_t i = 0; i < wSize.x; ++i)
			{
				const Vector3 departure = BackTrace(
					source,
					source.WPosition(i, j, k),
					dt);

				destination.W(i, j, k) =
					source.SampleW(departure);
			}
		}
	}

	SetClosedDomainBoundary(destination);

}

void GridOperators3::Advect(
	const ScalarGrid3& source,
	const FaceCenteredVelocityGrid3& flow,
	Real dt,
	ScalarGrid3& destination)
{
	const Size3 resolution = source.Resolution();

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				const Vector3 position =
					source.DataPosition(i, j, k);

				const Vector3 departure =
					BackTrace(flow, position, dt);

				destination(i, j, k) =
					source.Sample(departure);
			}
		}
	}
}

Real GridOperators3::CalculateRmsDivergence(
	const FaceCenteredVelocityGrid3& velocity)
{
	const Size3 resolution = velocity.Resolution();

	const std::size_t cellCount =
		resolution.x * resolution.y * resolution.z;

	if (cellCount == 0)
		return 0.0;

	Real sumSquaredDivergence = 0.0;

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				const Real divergence =
					velocity.DivergenceAtCellCenter(i, j, k);

				sumSquaredDivergence +=
					divergence * divergence;
			}
		}
	}

	return std::sqrt(
		sumSquaredDivergence /
		static_cast<Real>(cellCount));
}