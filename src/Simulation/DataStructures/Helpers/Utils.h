#pragma once

#include <cmath>
#include <algorithm>
#include <cstddef>

using Real = double;

struct Size3
{
    std::size_t x = 0;
    std::size_t y = 0;
    std::size_t z = 0;
};

struct Vector3
{
    Real x = 0.0;
    Real y = 0.0;
    Real z = 0.0;
};

struct BarycentricCoordinate
{
    std::size_t lower;
    std::size_t upper;
    Real fraction;
};

inline BarycentricCoordinate GetBarycentric(
    Real coordinate,
    std::size_t size)
{
    if (size <= 1)
        return { 0, 0, 0.0 };

    coordinate = std::clamp(
        coordinate,
        0.0,
        static_cast<Real>(size - 1));

    const auto lower =
        static_cast<std::size_t>(std::floor(coordinate));

    const auto upper = std::min(lower + 1, size - 1);

    return {
        lower,
        upper,
        coordinate - static_cast<Real>(lower)
    };
}

inline Real Lerp(Real a, Real b, Real t)
{
    return a + t * (b - a);
}