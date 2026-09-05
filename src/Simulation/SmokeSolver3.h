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

    float CalculateRmsDivergence(const float* velocityX, const float* velocityY, const float* velocityZ, int N, float h) const;

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

};