#pragma once
#include "Helpers/Utils.h"
#include <span>
#include <vector>

constexpr int Nx = 32;
constexpr int Ny = 32;
constexpr int Nz = 32;
constexpr Real spacingX = 1.0;
constexpr Real spacingY = 1.0;
constexpr Real spacingZ = 1.0;

class ScalarGrid3
{
public:
    ScalarGrid3() = default;

    ScalarGrid3(
        Size3 resolution,
        Vector3 gridSpacing = { 1.0, 1.0, 1.0 },
        Vector3 origin = {},
        Real initialValue = 0.0)
    {
        Resize(resolution, gridSpacing, origin, initialValue);
    }

    void Resize(
        Size3 resolution,
        Vector3 gridSpacing,
        Vector3 origin,
        Real initialValue = 0.0);

    [[nodiscard]] Size3 Resolution() const noexcept
    {
        return m_Resolution;
    }

    [[nodiscard]] Vector3 GridSpacing() const noexcept
    {
        return m_GridSpacing;
    }

    [[nodiscard]] Vector3 Origin() const noexcept
    {
        return m_Origin;
    }

    [[nodiscard]] Vector3 DataPosition(
        std::size_t i,
        std::size_t j,
        std::size_t k) const noexcept;

    Real& operator()(
        std::size_t i,
        std::size_t j,
        std::size_t k);

    const Real& operator()(
        std::size_t i,
        std::size_t j,
        std::size_t k) const;

    void Fill(Real value);

    [[nodiscard]] Real Sample(const Vector3& position) const;

    [[nodiscard]] Vector3 GradientAtDataPoint(
        std::size_t i,
        std::size_t j,
        std::size_t k) const;

    [[nodiscard]] Real LaplacianAtDataPoint(
        std::size_t i,
        std::size_t j,
        std::size_t k) const;

    [[nodiscard]] std::span<Real> Data() noexcept
    {
        return m_Data;
    }

    [[nodiscard]] std::span<const Real> Data() const noexcept
    {
        return m_Data;
    }

private:
    [[nodiscard]] std::size_t Index(
        std::size_t i,
        std::size_t j,
        std::size_t k) const noexcept
    {
        // X is contiguous, matching your existing StableFluids3D layout.
        return i + m_Resolution.x * (j + m_Resolution.y * k);
    }

    Size3 m_Resolution{};
    Vector3 m_GridSpacing{ 1.0, 1.0, 1.0 };
    Vector3 m_Origin{};
    std::vector<Real> m_Data;
};