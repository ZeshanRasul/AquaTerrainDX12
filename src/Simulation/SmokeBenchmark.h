#pragma once

#include "SmokeSolver3.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct SmokeBenchmarkConfig
{
	std::string implementation = "cpu_pcg_reference";
	std::string scenario = "buoyant_plume_closed_box_v1";
	std::string runLabel = "baseline";

	std::size_t totalSteps = 480;
	std::size_t emitterSteps = 240;
	std::size_t performanceWarmupSteps = 10;
	Real timeStep = 1.0 / 60.0;

	Size3 emitterCell{};
	Real densityRate = 30.0;
	Real temperatureRate = 10.0;
	Vector3 emitterAcceleration{};

	bool renderingEnabledDuringRun = false;
	std::filesystem::path outputRoot = "diagnostics/runs";
};

struct SmokeBenchmarkSample
{
	std::size_t stepIndex = 0;
	Real simulationTimeSeconds = 0.0;
	bool emitterEnabled = false;

	double solverCpuMilliseconds = 0.0;
	double diagnosticsCpuMilliseconds = 0.0;
	double millionCellsPerSecond = 0.0;
	double realtimeFactor = 0.0;

	std::size_t pressureIterations = 0;
	Real rmsDivergenceBefore = 0.0;
	Real rmsDivergenceAfter = 0.0;
	Real divergenceRemainingFraction = 0.0;
	Real scaledPressureResidual = 0.0;
	Real meanDivergence = 0.0;

	Real densityMinimum = 0.0;
	Real densityMaximum = 0.0;
	Real densityMean = 0.0;
	Real densitySum = 0.0;
	Real densityIntegral = 0.0;

	Real temperatureMinimum = 0.0;
	Real temperatureMaximum = 0.0;
	Real temperatureMean = 0.0;
	Real temperatureExcessIntegral = 0.0;

	Real velocityRms = 0.0;
	Real velocityMaximum = 0.0;
	Real verticalVelocityMinimum = 0.0;
	Real verticalVelocityMaximum = 0.0;
	Real kineticEnergy = 0.0;

	Vector3 densityCentre{};
	Vector3 densitySpread{};
};

class SmokeBenchmarkRecorder
{
public:
	bool Begin(
		const SmokeBenchmarkConfig& config,
		const SmokeSolver3& solver);

	void RecordStep(
		const SmokeSolver3& solver,
		double solverCpuMilliseconds);

	void StopAndSave();

	[[nodiscard]] bool IsRunning() const noexcept
	{
		return m_IsRunning;
	}

	[[nodiscard]] bool ShouldEmit() const noexcept
	{
		return m_IsRunning &&
			m_Samples.size() < m_Config.emitterSteps;
	}

	[[nodiscard]] std::size_t RecordedSteps() const noexcept
	{
		return m_Samples.size();
	}

	[[nodiscard]] std::size_t TotalSteps() const noexcept
	{
		return m_Config.totalSteps;
	}

	[[nodiscard]] const SmokeBenchmarkConfig& Config() const noexcept
	{
		return m_Config;
	}

	[[nodiscard]] const std::filesystem::path& LastOutputDirectory() const noexcept
	{
		return m_LastOutputDirectory;
	}

	[[nodiscard]] const std::string& LastError() const noexcept
	{
		return m_LastError;
	}

	[[nodiscard]] const std::string& StatusMessage() const noexcept
	{
		return m_StatusMessage;
	}

private:
	SmokeBenchmarkSample CaptureSample(
		const SmokeSolver3& solver,
		double solverCpuMilliseconds,
		bool emitterEnabled,
		std::size_t stepIndex) const;

	bool Save(bool completed);
	bool WriteStepsCsv();
	bool WriteSummaryCsv(bool completed);
	bool WriteManifestJson(bool completed);

	SmokeBenchmarkConfig m_Config;
	Size3 m_Resolution{};
	Vector3 m_GridSpacing{};
	Vector3 m_Origin{};
	SmokePhysicsParameters m_PhysicsParameters{};
	Real m_FluidDensity = 1.0;
	std::vector<SmokeBenchmarkSample> m_Samples;

	std::string m_RunId;
	std::string m_CreatedUtc;
	std::filesystem::path m_OutputDirectory;
	std::filesystem::path m_LastOutputDirectory;
	std::string m_LastError;
	std::string m_StatusMessage = "No benchmark recorded yet.";
	bool m_IsRunning = false;
};
