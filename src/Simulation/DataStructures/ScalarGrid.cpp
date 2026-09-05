#include "ScalarGrid.h"
#include <algorithm>
#include <cassert>

void ScalarGrid3::Resize(
    Size3 resolution,
    Vector3 gridSpacing,
    Vector3 origin,
    Real initialValue)
{
    assert(gridSpacing.x > 0.0);
    assert(gridSpacing.y > 0.0);
    assert(gridSpacing.z > 0.0);

    m_Resolution = resolution;
    m_GridSpacing = gridSpacing;
    m_Origin = origin;

    m_Data.assign(
        resolution.x * resolution.y * resolution.z,
        initialValue);
}

Real& ScalarGrid3::operator()(
    std::size_t i,
    std::size_t j,
    std::size_t k)
{
    assert(i < m_Resolution.x);
    assert(j < m_Resolution.y);
    assert(k < m_Resolution.z);
    return m_Data[Index(i, j, k)];
}

const Real& ScalarGrid3::operator()(
    std::size_t i,
    std::size_t j,
    std::size_t k) const
{
    assert(i < m_Resolution.x);
    assert(j < m_Resolution.y);
    assert(k < m_Resolution.z);
    return m_Data[Index(i, j, k)];
}

void ScalarGrid3::Fill(Real value)
{
    std::fill(m_Data.begin(), m_Data.end(), value);
}

Vector3 ScalarGrid3::DataPosition(
    std::size_t i,
    std::size_t j,
    std::size_t k) const noexcept
{
    return {
        m_Origin.x + (static_cast<Real>(i) + 0.5) * m_GridSpacing.x,
        m_Origin.y + (static_cast<Real>(j) + 0.5) * m_GridSpacing.y,
        m_Origin.z + (static_cast<Real>(k) + 0.5) * m_GridSpacing.z
    };
}

Real ScalarGrid3::Sample(const Vector3& position) const
{
    const Real gx =
        (position.x - m_Origin.x) / m_GridSpacing.x - 0.5;
    const Real gy =
        (position.y - m_Origin.y) / m_GridSpacing.y - 0.5;
    const Real gz =
        (position.z - m_Origin.z) / m_GridSpacing.z - 0.5;

    const auto x = GetBarycentric(gx, m_Resolution.x);
    const auto y = GetBarycentric(gy, m_Resolution.y);
    const auto z = GetBarycentric(gz, m_Resolution.z);

    const Real c00 = Lerp(
        (*this)(x.lower, y.lower, z.lower),
        (*this)(x.upper, y.lower, z.lower),
        x.fraction);

    const Real c10 = Lerp(
        (*this)(x.lower, y.upper, z.lower),
        (*this)(x.upper, y.upper, z.lower),
        x.fraction);

    const Real c01 = Lerp(
        (*this)(x.lower, y.lower, z.upper),
        (*this)(x.upper, y.lower, z.upper),
        x.fraction);

    const Real c11 = Lerp(
        (*this)(x.lower, y.upper, z.upper),
        (*this)(x.upper, y.upper, z.upper),
        x.fraction);

    return Lerp(
        Lerp(c00, c10, y.fraction),
        Lerp(c01, c11, y.fraction),
        z.fraction);
}

Vector3 ScalarGrid3::GradientAtDataPoint(
    std::size_t i,
    std::size_t j,
    std::size_t k) const
{
    const auto derivative = [](Real lower,
        Real centre,
        Real upper,
        bool hasLower,
        bool hasUpper,
        Real spacing)
        {
            if (hasLower && hasUpper)
                return (upper - lower) / (2.0 * spacing);

            if (hasUpper)
                return (upper - centre) / spacing;

            if (hasLower)
                return (centre - lower) / spacing;

            return 0.0;
        };

    const bool hasLeft = i > 0;
    const bool hasRight = i + 1 < m_Resolution.x;
    const bool hasDown = j > 0;
    const bool hasUp = j + 1 < m_Resolution.y;
    const bool hasBack = k > 0;
    const bool hasFront = k + 1 < m_Resolution.z;

    const Real centre = (*this)(i, j, k);

    return {
        derivative(
            hasLeft ? (*this)(i - 1, j, k) : centre,
            centre,
            hasRight ? (*this)(i + 1, j, k) : centre,
            hasLeft, hasRight, m_GridSpacing.x),

        derivative(
            hasDown ? (*this)(i, j - 1, k) : centre,
            centre,
            hasUp ? (*this)(i, j + 1, k) : centre,
            hasDown, hasUp, m_GridSpacing.y),

        derivative(
            hasBack ? (*this)(i, j, k - 1) : centre,
            centre,
            hasFront ? (*this)(i, j, k + 1) : centre,
            hasBack, hasFront, m_GridSpacing.z)
    };
}

Real ScalarGrid3::LaplacianAtDataPoint(std::size_t i, std::size_t j, std::size_t k) const
{
    assert(i > 0 && i + 1 < m_Resolution.x);
    assert(j > 0 && j + 1 < m_Resolution.y);
    assert(k > 0 && k + 1 < m_Resolution.z);

    const Real centre = (*this)(i, j, k);

    const Real dxx =
        ((*this)(i + 1, j, k) - 2.0 * centre +
            (*this)(i - 1, j, k))
        / (m_GridSpacing.x * m_GridSpacing.x);

    const Real dyy =
        ((*this)(i, j + 1, k) - 2.0 * centre +
            (*this)(i, j - 1, k))
        / (m_GridSpacing.y * m_GridSpacing.y);

    const Real dzz =
        ((*this)(i, j, k + 1) - 2.0 * centre +
            (*this)(i, j, k - 1))
        / (m_GridSpacing.z * m_GridSpacing.z);

    return dxx + dyy + dzz;
}
