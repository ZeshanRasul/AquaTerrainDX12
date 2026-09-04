#pragma once
#include <vector>

class StableFluids3D
{
public:
    StableFluids3D(int width, int height, int depth);

    enum class BoundaryType
    {
        Scalar,
        VelocityX,
        VelocityY,
        VelocityZ
    };

    void Diffuse(
        BoundaryType boundary,
        std::vector<float>& destination,
        const std::vector<float>& source,
        float diffusion,
        float dt);

    void Advect(
        BoundaryType boundaryType,
        std::vector<float>& destination,
        const std::vector<float>& source,
        const std::vector<float>& velocityX,
        const std::vector<float>& velocityY,
        const std::vector<float>& velocityZ,
        float dt);

    void Project(
        std::vector<float>& velocityX,
        std::vector<float>& velocityY,
        std::vector<float>& velocityZ,
        std::vector<float>& pressure,
        std::vector<float>& divergence);

    void SetBoundary(
        BoundaryType boundary,
        std::vector<float>& field);

    static constexpr int GridSize = 32;

    [[nodiscard]] int Width() const noexcept { return m_Width; }
    [[nodiscard]] int Height() const noexcept { return m_Height; }
    [[nodiscard]] int Depth() const noexcept { return m_Depth; }

    [[nodiscard]] float DensityAt(int i, int j, int k) const
    {
        return m_Density[Index(i, j, k)];
    }

    [[nodiscard]] float VelocityXAt(int i, int j, int k) const
    {
        return m_U[Index(i, j, k)];
    }

    [[nodiscard]] float VelocityYAt(int i, int j, int k) const
    {
        return m_V[Index(i, j, k)];
    }

    [[nodiscard]] float VelocityZAt(int i, int j, int k) const
    {
        return m_W[Index(i, j, k)];
    }

    [[nodiscard]] const std::vector<float>& Density() const noexcept
    {
        return m_Density;
    }

    [[nodiscard]] const std::vector<float>& VelocityX() const noexcept
    {
        return m_U;
    }

    [[nodiscard]] const std::vector<float>& VelocityY() const noexcept
    {
        return m_V;
    }

    [[nodiscard]] const std::vector<float>& VelocityZ() const noexcept
    {
        return m_W;
    }
    void AddSources(
        std::vector<float>& destination,
        const std::vector<float>& source,
        float dt);

    void AddSourceRates(
        int i,
        int j,
        int k,
        float densityRate,
        float forceX,
        float forceY,
        float forceZ);

    void Step(float stepDt, float diffusion = 0.0f, float viscosity = 0.0f);

    void Reset();

private:
    int m_Width;
    int m_Height;
    int m_Depth;

    int m_StrideY;
    int m_StrideZ;

    std::vector<float> m_Density;
    std::vector<float> m_DensitySource;
    std::vector<float> m_DensityWork;

    std::vector<float> m_Temperature;
    std::vector<float> m_TemperatureSource;

    std::vector<float> m_U;
    std::vector<float> m_V;
    std::vector<float> m_W;

    std::vector<float> m_UWork;
    std::vector<float> m_VWork;
    std::vector<float> m_WWork;

    std::vector<float> m_Pressure;
    std::vector<float> m_Divergence;

    [[nodiscard]] std::size_t Index(int i, int j, int k) const
    {
        return static_cast<std::size_t>(i)
            + static_cast<std::size_t>(m_StrideY) * j
            + static_cast<std::size_t>(m_StrideZ) * k;
    }

    [[nodiscard]] float SampleTrilinear(
        const std::vector<float>& field,
        float x,
        float y,
        float z) const;

    std::vector<float> m_USource;
    std::vector<float> m_VSource;
    std::vector<float> m_WSource;
};
