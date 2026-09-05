#pragma once

#include "DataStructures/FaceCenteredVelocityGrid3.h"

class SmokeSolver3
{
public:
    SmokeSolver3(
        Size3 resolution,
        Vector3 gridSpacing,
        Vector3 origin);

    void Step(Real dt);

    [[nodiscard]]
    const ScalarGrid3& Density() const noexcept
    {
        return m_Density;
    }

    [[nodiscard]]
    const FaceCenteredVelocityGrid3& Velocity() const noexcept
    {
        return m_Velocity;
    }

	Real RmsDivergenceBeforeProjection() const noexcept
	{
		return m_LastRmsDivergenceBeforeProjection;
	}

	Real RmsDivergenceAfterProjection() const noexcept
	{
		return m_LastRmsDivergenceAfterProjection;
	}

    [[nodiscard]] Real DivergenceReductionFactor() const noexcept
    {
        constexpr Real epsilon = 1e-12;

        if (m_LastRmsDivergenceBeforeProjection <= epsilon)
            return 0.0;

        return
            m_LastRmsDivergenceAfterProjection /
            m_LastRmsDivergenceBeforeProjection;
    }

    void AddSourceRates(
        std::size_t i,
        std::size_t j,
        std::size_t k,
        Real densityRate,
        Real temperatureRate,
        Vector3 acceleration,
        Real dt);

private:
    void ApplyExternalForces(Real dt);
    void AdvectVelocity(Real dt);
    void Project(Real dt);
    void AdvectScalars(Real dt);

    void SolvePressurePoisson(
        const ScalarGrid3& divergence,
        ScalarGrid3& pressure,
        Real dt,
        Real fluidDensity);

    FaceCenteredVelocityGrid3 m_Velocity;

    ScalarGrid3 m_Density;
    ScalarGrid3 m_Temperature;

    // Projection working fields.
    ScalarGrid3 m_Pressure;
    ScalarGrid3 m_Divergence;

    // Reusable temporary storage.
    FaceCenteredVelocityGrid3 m_VelocityScratch;
    ScalarGrid3 m_ScalarScratch;
    ScalarGrid3 m_PressureScratch;

    Real m_FluidDensity = 1.0;

	Real m_LastRmsDivergenceBeforeProjection = 0.0f;
	Real m_LastRmsDivergenceAfterProjection = 0.0f;

};