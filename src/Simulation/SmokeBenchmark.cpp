#include "SmokeBenchmark.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <thread>

namespace
{
	constexpr int kSchemaVersion = 1;

	std::string UtcTimestamp(const char* format)
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t time =
			std::chrono::system_clock::to_time_t(now);
		std::tm utc{};
		gmtime_s(&utc, &time);

		std::ostringstream stream;
		stream << std::put_time(&utc, format);
		return stream.str();
	}

	std::string JsonEscape(const std::string& value)
	{
		std::string escaped;
		escaped.reserve(value.size());

		for (const char character : value)
		{
			switch (character)
			{
			case '\\': escaped += "\\\\"; break;
			case '"': escaped += "\\\""; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			case '\t': escaped += "\\t"; break;
			default: escaped += character; break;
			}
		}

		return escaped;
	}

	std::string CsvEscape(const std::string& value)
	{
		if (value.find_first_of(",\"\r\n") == std::string::npos)
			return value;

		std::string escaped = "\"";
		for (const char character : value)
		{
			if (character == '"')
				escaped += "\"\"";
			else
				escaped += character;
		}
		escaped += '"';
		return escaped;
	}

	double Percentile(std::vector<double> values, double percentile)
	{
		if (values.empty())
			return 0.0;

		std::sort(values.begin(), values.end());
		const double position =
			percentile * static_cast<double>(values.size() - 1);
		const std::size_t lower =
			static_cast<std::size_t>(std::floor(position));
		const std::size_t upper =
			static_cast<std::size_t>(std::ceil(position));
		const double fraction = position - static_cast<double>(lower);
		return values[lower] + fraction * (values[upper] - values[lower]);
	}

	double Mean(const std::vector<double>& values)
	{
		if (values.empty())
			return 0.0;
		return std::accumulate(values.begin(), values.end(), 0.0) /
			static_cast<double>(values.size());
	}

	double StandardDeviation(
		const std::vector<double>& values,
		double mean)
	{
		if (values.empty())
			return 0.0;

		double squaredDifferenceSum = 0.0;
		for (const double value : values)
		{
			const double difference = value - mean;
			squaredDifferenceSum += difference * difference;
		}

		return std::sqrt(
			squaredDifferenceSum / static_cast<double>(values.size()));
	}

	std::string BuildConfiguration()
	{
#if defined(_DEBUG)
		return "Debug";
#else
		return "Release";
#endif
	}

	std::string CompilerDescription()
	{
#if defined(_MSC_VER)
		return "MSVC " + std::to_string(_MSC_VER);
#else
		return "unknown";
#endif
	}
}

bool SmokeBenchmarkRecorder::Begin(
	const SmokeBenchmarkConfig& config,
	const SmokeSolver3& solver)
{
	m_LastError.clear();

	if (m_IsRunning)
	{
		m_LastError = "A benchmark is already running.";
		return false;
	}

	if (config.totalSteps == 0 ||
		config.emitterSteps > config.totalSteps ||
		config.timeStep <= 0.0)
	{
		m_LastError = "Benchmark configuration is invalid.";
		return false;
	}

	m_Config = config;
	m_Resolution = solver.Density().Resolution();
	m_GridSpacing = solver.Density().GridSpacing();
	m_Origin = solver.Density().Origin();
	m_PhysicsParameters = solver.PhysicsParameters();
	m_FluidDensity = solver.FluidDensity();

	if (m_Config.emitterCell.x >= m_Resolution.x ||
		m_Config.emitterCell.y >= m_Resolution.y ||
		m_Config.emitterCell.z >= m_Resolution.z)
	{
		m_LastError = "Emitter cell is outside the smoke grid.";
		return false;
	}

	m_CreatedUtc = UtcTimestamp("%Y-%m-%dT%H:%M:%SZ");
	m_RunId = UtcTimestamp("%Y%m%dT%H%M%SZ");
	const std::string directoryStem =
		m_RunId + "_" + m_Config.implementation;
	m_OutputDirectory = m_Config.outputRoot / directoryStem;
	for (std::size_t suffix = 2;
		std::filesystem::exists(m_OutputDirectory);
		++suffix)
	{
		m_OutputDirectory = m_Config.outputRoot /
			(directoryStem + "_" + std::to_string(suffix));
	}

	std::error_code error;
	std::filesystem::create_directories(m_OutputDirectory, error);
	if (error)
	{
		m_LastError = "Could not create benchmark directory: " +
			error.message();
		return false;
	}

	m_Samples.clear();
	m_Samples.reserve(m_Config.totalSteps);
	m_IsRunning = true;
	m_StatusMessage = "Recording deterministic benchmark...";
	return true;
}

void SmokeBenchmarkRecorder::RecordStep(
	const SmokeSolver3& solver,
	double solverCpuMilliseconds)
{
	if (!m_IsRunning)
		return;

	const bool emitterEnabled =
		m_Samples.size() < m_Config.emitterSteps;
	const std::size_t stepIndex = m_Samples.size() + 1;
	m_Samples.push_back(CaptureSample(
		solver,
		solverCpuMilliseconds,
		emitterEnabled,
		stepIndex));

	if (m_Samples.size() >= m_Config.totalSteps)
		Save(true);
}

void SmokeBenchmarkRecorder::StopAndSave()
{
	if (m_IsRunning)
		Save(false);
}

SmokeBenchmarkSample SmokeBenchmarkRecorder::CaptureSample(
	const SmokeSolver3& solver,
	double solverCpuMilliseconds,
	bool emitterEnabled,
	std::size_t stepIndex) const
{
	const auto diagnosticsStart = std::chrono::steady_clock::now();

	SmokeBenchmarkSample sample;
	sample.stepIndex = stepIndex;
	sample.simulationTimeSeconds =
		static_cast<Real>(stepIndex) * m_Config.timeStep;
	sample.emitterEnabled = emitterEnabled;
	sample.solverCpuMilliseconds = solverCpuMilliseconds;

	const ScalarGrid3& density = solver.Density();
	const ScalarGrid3& temperature = solver.Temperature();
	const FaceCenteredVelocityGrid3& velocity = solver.Velocity();
	const Size3 resolution = density.Resolution();
	const Vector3 spacing = density.GridSpacing();
	const Real cellVolume = spacing.x * spacing.y * spacing.z;
	const std::size_t cellCount =
		resolution.x * resolution.y * resolution.z;

	sample.pressureIterations = solver.PressureIterations();
	sample.rmsDivergenceBefore =
		solver.RmsDivergenceBeforeProjection();
	sample.rmsDivergenceAfter =
		solver.RmsDivergenceAfterProjection();
	sample.divergenceRemainingFraction =
		solver.DivergenceReductionFactor();
	sample.scaledPressureResidual = solver.ScaledPressureResidual();
	sample.meanDivergence = solver.MeanDivergence();

	sample.densityMinimum = std::numeric_limits<Real>::max();
	sample.densityMaximum = std::numeric_limits<Real>::lowest();
	sample.temperatureMinimum = std::numeric_limits<Real>::max();
	sample.temperatureMaximum = std::numeric_limits<Real>::lowest();
	sample.verticalVelocityMinimum = std::numeric_limits<Real>::max();
	sample.verticalVelocityMaximum = std::numeric_limits<Real>::lowest();

	Real temperatureSum = 0.0;
	Real speedSquaredSum = 0.0;
	Real densityWeightedX = 0.0;
	Real densityWeightedY = 0.0;
	Real densityWeightedZ = 0.0;
	Real densityWeightedX2 = 0.0;
	Real densityWeightedY2 = 0.0;
	Real densityWeightedZ2 = 0.0;

	for (std::size_t k = 0; k < resolution.z; ++k)
	{
		for (std::size_t j = 0; j < resolution.y; ++j)
		{
			for (std::size_t i = 0; i < resolution.x; ++i)
			{
				const Real densityValue = density(i, j, k);
				const Real temperatureValue = temperature(i, j, k);
				const Vector3 velocityValue =
					velocity.ValueAtCellCenter(i, j, k);
				const Vector3 position = density.DataPosition(i, j, k);
				const Real speedSquared =
					velocityValue.x * velocityValue.x +
					velocityValue.y * velocityValue.y +
					velocityValue.z * velocityValue.z;

				sample.densityMinimum =
					std::min(sample.densityMinimum, densityValue);
				sample.densityMaximum =
					std::max(sample.densityMaximum, densityValue);
				sample.temperatureMinimum =
					std::min(sample.temperatureMinimum, temperatureValue);
				sample.temperatureMaximum =
					std::max(sample.temperatureMaximum, temperatureValue);
				sample.verticalVelocityMinimum =
					std::min(sample.verticalVelocityMinimum, velocityValue.y);
				sample.verticalVelocityMaximum =
					std::max(sample.verticalVelocityMaximum, velocityValue.y);
				sample.velocityMaximum =
					std::max(sample.velocityMaximum, std::sqrt(speedSquared));

				sample.densitySum += densityValue;
				temperatureSum += temperatureValue;
				speedSquaredSum += speedSquared;
				sample.temperatureExcessIntegral +=
					std::max(
						temperatureValue -
						m_PhysicsParameters.ambientTemperature,
						Real{ 0.0 }) * cellVolume;

				const Real positiveDensity =
					std::max(densityValue, Real{ 0.0 });
				densityWeightedX += positiveDensity * position.x;
				densityWeightedY += positiveDensity * position.y;
				densityWeightedZ += positiveDensity * position.z;
				densityWeightedX2 +=
					positiveDensity * position.x * position.x;
				densityWeightedY2 +=
					positiveDensity * position.y * position.y;
				densityWeightedZ2 +=
					positiveDensity * position.z * position.z;
			}
		}
	}

	if (cellCount > 0)
	{
		const Real inverseCellCount =
			1.0 / static_cast<Real>(cellCount);
		sample.densityMean = sample.densitySum * inverseCellCount;
		sample.temperatureMean = temperatureSum * inverseCellCount;
		sample.velocityRms =
			std::sqrt(speedSquaredSum * inverseCellCount);
	}

	sample.densityIntegral = sample.densitySum * cellVolume;
	sample.kineticEnergy =
		0.5 * solver.FluidDensity() * speedSquaredSum * cellVolume;

	if (sample.densitySum > std::numeric_limits<Real>::epsilon())
	{
		const Real inverseDensitySum = 1.0 / sample.densitySum;
		sample.densityCentre = {
			densityWeightedX * inverseDensitySum,
			densityWeightedY * inverseDensitySum,
			densityWeightedZ * inverseDensitySum
		};

		const Real varianceX = std::max(
			densityWeightedX2 * inverseDensitySum -
			sample.densityCentre.x * sample.densityCentre.x,
			Real{ 0.0 });
		const Real varianceY = std::max(
			densityWeightedY2 * inverseDensitySum -
			sample.densityCentre.y * sample.densityCentre.y,
			Real{ 0.0 });
		const Real varianceZ = std::max(
			densityWeightedZ2 * inverseDensitySum -
			sample.densityCentre.z * sample.densityCentre.z,
			Real{ 0.0 });

		sample.densitySpread = {
			std::sqrt(varianceX),
			std::sqrt(varianceY),
			std::sqrt(varianceZ)
		};
	}

	if (solverCpuMilliseconds > 0.0)
	{
		sample.millionCellsPerSecond =
			static_cast<double>(cellCount) /
			(solverCpuMilliseconds * 1000.0);
		sample.realtimeFactor =
			(static_cast<double>(m_Config.timeStep) * 1000.0) /
			solverCpuMilliseconds;
	}

	const auto diagnosticsEnd = std::chrono::steady_clock::now();
	sample.diagnosticsCpuMilliseconds =
		std::chrono::duration<double, std::milli>(
			diagnosticsEnd - diagnosticsStart).count();

	return sample;
}

bool SmokeBenchmarkRecorder::Save(bool completed)
{
	m_IsRunning = false;
	m_LastError.clear();

	const bool success =
		WriteStepsCsv() &&
		WriteSummaryCsv(completed) &&
		WriteManifestJson(completed);

	if (success)
	{
		m_LastOutputDirectory =
			std::filesystem::absolute(m_OutputDirectory);
		m_StatusMessage = completed
			? "Benchmark complete and saved."
			: "Partial benchmark saved.";
	}
	else
	{
		m_StatusMessage = "Benchmark could not be saved.";
	}

	return success;
}

bool SmokeBenchmarkRecorder::WriteStepsCsv()
{
	std::ofstream output(m_OutputDirectory / "steps.csv");
	if (!output)
	{
		m_LastError = "Could not write steps.csv.";
		return false;
	}

	output << std::setprecision(17);
	output <<
		"step_index,simulation_time_s,phase,emitter_enabled,"
		"solver_cpu_ms,diagnostics_cpu_ms,million_cells_per_s,realtime_factor,"
		"pressure_iterations,rms_divergence_before,rms_divergence_after,"
		"divergence_remaining_fraction,scaled_pressure_residual,mean_divergence,"
		"density_min,density_max,density_mean,density_sum,density_integral,"
		"temperature_min,temperature_max,temperature_mean,temperature_excess_integral,"
		"velocity_rms,velocity_max,vertical_velocity_min,vertical_velocity_max,"
		"kinetic_energy,density_centre_x,density_centre_y,density_centre_z,"
		"density_spread_x,density_spread_y,density_spread_z\n";

	for (const SmokeBenchmarkSample& sample : m_Samples)
	{
		output << sample.stepIndex << ','
			<< sample.simulationTimeSeconds << ','
			<< (sample.emitterEnabled ? "emitting" : "decay") << ','
			<< (sample.emitterEnabled ? 1 : 0) << ','
			<< sample.solverCpuMilliseconds << ','
			<< sample.diagnosticsCpuMilliseconds << ','
			<< sample.millionCellsPerSecond << ','
			<< sample.realtimeFactor << ','
			<< sample.pressureIterations << ','
			<< sample.rmsDivergenceBefore << ','
			<< sample.rmsDivergenceAfter << ','
			<< sample.divergenceRemainingFraction << ','
			<< sample.scaledPressureResidual << ','
			<< sample.meanDivergence << ','
			<< sample.densityMinimum << ','
			<< sample.densityMaximum << ','
			<< sample.densityMean << ','
			<< sample.densitySum << ','
			<< sample.densityIntegral << ','
			<< sample.temperatureMinimum << ','
			<< sample.temperatureMaximum << ','
			<< sample.temperatureMean << ','
			<< sample.temperatureExcessIntegral << ','
			<< sample.velocityRms << ','
			<< sample.velocityMaximum << ','
			<< sample.verticalVelocityMinimum << ','
			<< sample.verticalVelocityMaximum << ','
			<< sample.kineticEnergy << ','
			<< sample.densityCentre.x << ','
			<< sample.densityCentre.y << ','
			<< sample.densityCentre.z << ','
			<< sample.densitySpread.x << ','
			<< sample.densitySpread.y << ','
			<< sample.densitySpread.z << '\n';
	}

	return output.good();
}

bool SmokeBenchmarkRecorder::WriteSummaryCsv(bool completed)
{
	std::ofstream output(m_OutputDirectory / "summary.csv");
	if (!output)
	{
		m_LastError = "Could not write summary.csv.";
		return false;
	}

	const std::size_t warmup = std::min(
		m_Config.performanceWarmupSteps,
		m_Samples.size() > m_Config.performanceWarmupSteps
			? m_Config.performanceWarmupSteps
			: std::size_t{ 0 });

	std::vector<double> solverTimes;
	std::vector<double> pressureIterations;
	std::vector<double> remainingDivergence;
	for (std::size_t i = warmup; i < m_Samples.size(); ++i)
	{
		solverTimes.push_back(m_Samples[i].solverCpuMilliseconds);
		pressureIterations.push_back(
			static_cast<double>(m_Samples[i].pressureIterations));
		remainingDivergence.push_back(
			static_cast<double>(
				m_Samples[i].divergenceRemainingFraction));
	}

	const double solverMean = Mean(solverTimes);
	const double solverStandardDeviation =
		StandardDeviation(solverTimes, solverMean);
	const double totalSolverMilliseconds =
		std::accumulate(solverTimes.begin(), solverTimes.end(), 0.0);
	const double simulatedSeconds =
		static_cast<double>(solverTimes.size()) *
		static_cast<double>(m_Config.timeStep);
	const double overallRealtimeFactor = totalSolverMilliseconds > 0.0
		? simulatedSeconds / (totalSolverMilliseconds / 1000.0)
		: 0.0;
	const double cellCount = static_cast<double>(
		m_Resolution.x * m_Resolution.y * m_Resolution.z);
	const double overallMillionCellsPerSecond =
		totalSolverMilliseconds > 0.0
		? cellCount * static_cast<double>(solverTimes.size()) /
			(totalSolverMilliseconds * 1000.0)
		: 0.0;

	Real peakDensity = 0.0;
	Real maximumPostProjectionDivergence = 0.0;
	for (const SmokeBenchmarkSample& sample : m_Samples)
	{
		peakDensity = std::max(peakDensity, sample.densityMaximum);
		maximumPostProjectionDivergence = std::max(
			maximumPostProjectionDivergence,
			sample.rmsDivergenceAfter);
	}

	const SmokeBenchmarkSample finalSample = m_Samples.empty()
		? SmokeBenchmarkSample{}
		: m_Samples.back();

	output << std::setprecision(17);
	output <<
		"schema_version,run_id,implementation,scenario,run_label,status,"
		"steps_recorded,warmup_steps_excluded,solver_cpu_ms_mean,"
		"solver_cpu_ms_stddev,solver_cpu_ms_min,solver_cpu_ms_p50,"
		"solver_cpu_ms_p95,solver_cpu_ms_p99,solver_cpu_ms_max,"
		"overall_realtime_factor,overall_million_cells_per_s,"
		"pressure_iterations_mean,pressure_iterations_max,"
		"divergence_remaining_fraction_mean,divergence_remaining_fraction_max,"
		"maximum_post_projection_divergence,peak_density,final_density_integral,"
		"final_temperature_excess_integral,final_kinetic_energy,"
		"final_density_centre_x,final_density_centre_y,final_density_centre_z\n";

	output << kSchemaVersion << ','
		<< CsvEscape(m_RunId) << ','
		<< CsvEscape(m_Config.implementation) << ','
		<< CsvEscape(m_Config.scenario) << ','
		<< CsvEscape(m_Config.runLabel) << ','
		<< (completed ? "complete" : "partial") << ','
		<< m_Samples.size() << ','
		<< warmup << ','
		<< solverMean << ','
		<< solverStandardDeviation << ','
		<< (solverTimes.empty()
			? 0.0
			: *std::min_element(solverTimes.begin(), solverTimes.end())) << ','
		<< Percentile(solverTimes, 0.50) << ','
		<< Percentile(solverTimes, 0.95) << ','
		<< Percentile(solverTimes, 0.99) << ','
		<< (solverTimes.empty()
			? 0.0
			: *std::max_element(solverTimes.begin(), solverTimes.end())) << ','
		<< overallRealtimeFactor << ','
		<< overallMillionCellsPerSecond << ','
		<< Mean(pressureIterations) << ','
		<< (pressureIterations.empty()
			? 0.0
			: *std::max_element(
				pressureIterations.begin(), pressureIterations.end())) << ','
		<< Mean(remainingDivergence) << ','
		<< (remainingDivergence.empty()
			? 0.0
			: *std::max_element(
				remainingDivergence.begin(), remainingDivergence.end())) << ','
		<< maximumPostProjectionDivergence << ','
		<< peakDensity << ','
		<< finalSample.densityIntegral << ','
		<< finalSample.temperatureExcessIntegral << ','
		<< finalSample.kineticEnergy << ','
		<< finalSample.densityCentre.x << ','
		<< finalSample.densityCentre.y << ','
		<< finalSample.densityCentre.z << '\n';

	return output.good();
}

bool SmokeBenchmarkRecorder::WriteManifestJson(bool completed)
{
	std::ofstream output(m_OutputDirectory / "run.json");
	if (!output)
	{
		m_LastError = "Could not write run.json.";
		return false;
	}

	output << std::setprecision(17);
	output << "{\n"
		<< "  \"schema_version\": " << kSchemaVersion << ",\n"
		<< "  \"run_id\": \"" << JsonEscape(m_RunId) << "\",\n"
		<< "  \"created_utc\": \"" << JsonEscape(m_CreatedUtc) << "\",\n"
		<< "  \"status\": \"" << (completed ? "complete" : "partial") << "\",\n"
		<< "  \"implementation\": \"" << JsonEscape(m_Config.implementation) << "\",\n"
		<< "  \"scenario\": \"" << JsonEscape(m_Config.scenario) << "\",\n"
		<< "  \"run_label\": \"" << JsonEscape(m_Config.runLabel) << "\",\n"
		<< "  \"build\": {\n"
		<< "    \"configuration\": \"" << BuildConfiguration() << "\",\n"
		<< "    \"compiler\": \"" << CompilerDescription() << "\",\n"
		<< "    \"real_precision_bits\": " << sizeof(Real) * 8 << ",\n"
		<< "    \"hardware_threads\": " << std::thread::hardware_concurrency() << "\n"
		<< "  },\n"
		<< "  \"algorithm\": {\n"
		<< "    \"advection\": \"semi_lagrangian\",\n"
		<< "    \"pressure_solver\": \"pcg_jacobi_preconditioner\",\n"
		<< "    \"boundary_condition\": \"closed_no_flow\"\n"
		<< "  },\n"
		<< "  \"schedule\": {\n"
		<< "    \"fixed_time_step_s\": " << m_Config.timeStep << ",\n"
		<< "    \"steps_requested\": " << m_Config.totalSteps << ",\n"
		<< "    \"steps_recorded\": " << m_Samples.size() << ",\n"
		<< "    \"emitter_steps\": " << m_Config.emitterSteps << ",\n"
		<< "    \"performance_warmup_steps\": " << m_Config.performanceWarmupSteps << "\n"
		<< "  },\n"
		<< "  \"grid\": {\n"
		<< "    \"resolution\": [" << m_Resolution.x << ", " << m_Resolution.y << ", " << m_Resolution.z << "],\n"
		<< "    \"spacing\": [" << m_GridSpacing.x << ", " << m_GridSpacing.y << ", " << m_GridSpacing.z << "],\n"
		<< "    \"origin\": [" << m_Origin.x << ", " << m_Origin.y << ", " << m_Origin.z << "]\n"
		<< "  },\n"
		<< "  \"emitter\": {\n"
		<< "    \"cell\": [" << m_Config.emitterCell.x << ", " << m_Config.emitterCell.y << ", " << m_Config.emitterCell.z << "],\n"
		<< "    \"density_rate_per_s\": " << m_Config.densityRate << ",\n"
		<< "    \"temperature_rate_per_s\": " << m_Config.temperatureRate << ",\n"
		<< "    \"acceleration\": [" << m_Config.emitterAcceleration.x << ", " << m_Config.emitterAcceleration.y << ", " << m_Config.emitterAcceleration.z << "]\n"
		<< "  },\n"
		<< "  \"physics\": {\n"
		<< "    \"fluid_density\": " << m_FluidDensity << ",\n"
		<< "    \"ambient_temperature\": " << m_PhysicsParameters.ambientTemperature << ",\n"
		<< "    \"temperature_buoyancy\": " << m_PhysicsParameters.temperatureBuoyancy << ",\n"
		<< "    \"smoke_weight\": " << m_PhysicsParameters.smokeWeight << ",\n"
		<< "    \"density_dissipation_per_s\": " << m_PhysicsParameters.densityDissipation << ",\n"
		<< "    \"temperature_cooling_per_s\": " << m_PhysicsParameters.temperatureCooling << "\n"
		<< "  },\n"
		<< "  \"measurement\": {\n"
		<< "    \"rendering_enabled_during_run\": "
		<< (m_Config.renderingEnabledDuringRun ? "true" : "false") << ",\n"
		<< "    \"solver_timing\": \"steady_clock around source injection and SmokeSolver3::Step\",\n"
		<< "    \"diagnostics_timing_excluded_from_solver_cpu_ms\": true\n"
		<< "  },\n"
		<< "  \"files\": {\n"
		<< "    \"steps\": \"steps.csv\",\n"
		<< "    \"summary\": \"summary.csv\"\n"
		<< "  }\n"
		<< "}\n";

	return output.good();
}
