#pragma once
#include "ScalarGrid.h"
#include "FaceCenteredVelocityGrid3.h"

namespace GridOperators3
{
    void ComputeDivergence(
        const FaceCenteredVelocityGrid3& velocity,
        ScalarGrid3& divergence);

    void SubtractPressureGradient(
        FaceCenteredVelocityGrid3& velocity,
        const ScalarGrid3& pressure,
        Real scale);

    void SetClosedDomainBoundary(
        FaceCenteredVelocityGrid3& velocity);

    void Advect(
        const ScalarGrid3& source,
        const FaceCenteredVelocityGrid3& flow,
        Real dt,
        ScalarGrid3& destination);

    void Advect(
        const FaceCenteredVelocityGrid3& source,
        Real dt,
        FaceCenteredVelocityGrid3& destination);
}