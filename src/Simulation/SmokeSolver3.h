#pragma once

#include "DataStructures/FaceCenteredVelocityGrid3.h"
#include <cassert>

struct SmokePhysicsParameters
{
    Real ambientTemperature = 0.0;

    // Upward acceleration generated per unit of excess temperature.
    Real temperatureBuoyancy = 0.5;

    // Downward acceleration generated per unit of smoke density.
    Real smokeWeight = 0.05;

    // Rates per second.
    Real densityDissipation = 0.10;
    Real temperatureCooling = 1.0;
};

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

    Real PressureResidualRms(Real dt) const;

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

	Real FluidDensity() const noexcept
	{
		return m_FluidDensity;
	}

    [[nodiscard]] Real ScaledPressureResidual() const noexcept
    {
        return m_LastScaledPressureResidual;
    }

    void ApplyPressureMatrix(
        const ScalarGrid3& input,
        ScalarGrid3& output);

    void ApplyJacobiPreconditioner(
        const ScalarGrid3& residual,
        ScalarGrid3& result);

    Real Dot(
        const ScalarGrid3& a,
        const ScalarGrid3& b)
    {
        const auto aData = a.Data();
        const auto bData = b.Data();

        assert(aData.size() == bData.size());

        Real result = 0.0;

        for (std::size_t i = 0; i < aData.size(); ++i)
            result += aData[i] * bData[i];

        return result;
    }

    Real Rms(const ScalarGrid3& grid)
    {
        const auto data = grid.Data();

        if (data.empty())
            return 0.0;

        return std::sqrt(
            Dot(grid, grid) /
            static_cast<Real>(data.size()));
    }

    Real Mean(const ScalarGrid3& grid)
    {
        const auto data = grid.Data();

        if (data.empty())
            return 0.0;

        Real sum = 0.0;

        for (const Real value : data)
            sum += value;

        return sum / static_cast<Real>(data.size());
    }

    void RemoveMean(ScalarGrid3& grid)
    {
        const Real mean = Mean(grid);

        for (Real& value : grid.Data())
            value -= mean;
    }

    void SmokeSolver3::SolvePressurePoissonPCG(
        const ScalarGrid3& divergence,
        ScalarGrid3& pressure,
        Real dt,
        Real fluidDensity);

    [[nodiscard]] std::size_t PressureIterations() const noexcept
    {
        return m_LastPressureIterations;
    }

    [[nodiscard]] Real MeanDivergence() const noexcept
    {
        return m_LastMeanDivergence;
    }

    SmokePhysicsParameters& PhysicsParameters() noexcept
    {
        return m_PhysicsParameters;
    }

    const SmokePhysicsParameters& PhysicsParameters() const noexcept
    {
        return m_PhysicsParameters;
    }

private:
    void ApplyExternalForces(Real dt);
    void AdvectVelocity(Real dt);
    void Project(Real dt);
    void AdvectScalars(Real dt);
    void ApplyScalarDissipation(Real dt);

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
	Real m_LastScaledPressureResidual = 0.0f;

    ScalarGrid3 m_PcgResidual;       // r
    ScalarGrid3 m_PcgPreconditioned; // z
    ScalarGrid3 m_PcgDirection;      // d
    ScalarGrid3 m_PcgMatrixDirection;// q = A*d

    std::size_t m_LastPressureIterations = 0;
    Real m_LastMeanDivergence = 0.0;

    SmokePhysicsParameters m_PhysicsParameters;
};