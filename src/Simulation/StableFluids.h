#pragma once

constexpr int N = 64;
static constexpr int CellCount = (N + 2) * (N + 2);
#define IX(i, j) ((i) + (N+2) * (j))
#define SWAP(x0, x) {float *tmp = x0; x0 = x; x = tmp;}

struct FluidDiagnostics
{
	// Measured immediately before and after the final projection.
	float rmsDivergenceBeforeProjection = 0.0f;
	float rmsDivergenceAfterProjection = 0.0f;

	// Measured from the final velocity field.
	float maximumAbsoluteDivergence = 0.0f;
	float maximumSpeed = 0.0f;

	// Integrals over the unit-square simulation domain.
	float kineticEnergy = 0.0f;
	float totalDye = 0.0f;
};

class StableFluids
{
public:
	StableFluids() = default;

	void add_source(int N, float* x, float* s, float dt);

	void diffuse(int N, int b, float* x, float* x0, float diff, float dt);

	void advect(int N, int b, float* d, float* d0, float* u, float* v, float dt);

	void density_step(int N, float* x, float* x0, float* u, float* v, float diff, float dt);

	void velocity_step(int N, float* u, float* v, float* u0, float* v0, float visc, float dt);

	void project(int N, float* u, float* v, float* p, float* div);

	void set_bnd(int N, int b, float* x);

	static constexpr int GridSize = N;

	const float* Density() const { return dens; }
	const float* VelocityX() const { return u; }
	const float* VelocityY() const { return v; }

	void AddSourceRates(int i, int j, float densityRate, float forceX, float forceY);

	void Step(float stepDt, float diffusion = 0.0f, float viscosity = 0.0f);

	void Reset();

	FluidDiagnostics GetDiagnostics() const;

private:
	float CalculateRmsDivergence(const float* velocityX, const float* velocityY) const;


	float u[CellCount];
	float u_prev[CellCount];
	float v[CellCount];
	float v_prev[CellCount];
	float dens[CellCount];
	float dens_prev[CellCount];

	float h = 1.0f / N;
	float dt = 1.0f / 60.0f;

	float m_RmsDivergenceBeforeProjection = 0.0f;
	float m_RmsDivergenceAfterProjection = 0.0f;
};