#pragma once

#include "imagePlane.h"

#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/**
 * @file waveformPanel.h
 * @brief Split-view waveform analysis panel state and rendering helper.
 */
class WaveformPanel
{
public:
	WaveformPanel();
	~WaveformPanel();

	enum class PaintMode
	{
		White = 0,
		Rgb,
		Red,
		Green,
		Blue,
	};

	/**
	 * @brief Release cached waveform content and GPU resources.
	 */
	void invalidate();

	/**
	 * @brief Release GL texture resources owned by the panel.
	 *
	 * Must be called while an OpenGL context is current.
	 */
	void releaseGlResources();

	/**
	 * @brief Stop the background waveform worker and discard queued CPU results.
	 */
	void shutdownWorker();

	/**
	 * @brief Draw the left analysis panel.
	 */
	void draw(int panelWidth,
			int panelHeight,
			float unit,
			bool sourceReady,
			const ImagePlaneData* planeData,
			int planeIdx,
			int mipIdx,
			std::uint64_t generation);

private:
	struct BuildTask
	{
		std::shared_ptr<precision[]> pixels;
		int imageWidth = 0;
		int imageHeight = 0;
		int channelCount = 0;
		bool isRgb = false;
		int planeIdx = -1;
		int mipIdx = -1;
		std::uint64_t generation = 0;
		int plotWidth = 0;
		int plotHeight = 0;
		PaintMode paintMode = PaintMode::Rgb;
		std::uint64_t serial = 0;
	};

	struct BuildResult
	{
		std::vector<unsigned char> imageData;
		bool isRgb = false;
		float minValue = 0.0f;
		float maxValue = 1.0f;
		float minLogValue = -8.0f;
		float maxLogValue = 0.0f;
		int planeIdx = -1;
		int mipIdx = -1;
		std::uint64_t generation = 0;
		int plotWidth = 0;
		int plotHeight = 0;
		PaintMode paintMode = PaintMode::Rgb;
		std::uint64_t serial = 0;
	};

	static bool isRgbChannels(const std::string& channels);
	static const char* paintModeLabel(PaintMode mode);
	static BuildResult buildWaveform(const BuildTask& task);

	void workerLoop();
	void requestBuild(const ImagePlaneData& planeData,
					int planeIdx,
					int mipIdx,
					std::uint64_t generation,
					int plotWidth,
					int plotHeight);
	void consumeReadyResult();
	bool isValidFor(int planeIdx,
					int mipIdx,
					std::uint64_t generation,
					int plotWidth,
					int plotHeight,
					PaintMode paintMode) const;

private:
	bool valid = false;
	int cachedPlaneIdx = -1;
	int cachedMipIdx = -1;
	std::uint64_t cachedGeneration = 0;
	bool cachedIsRgb = false;
	float cachedMinValue = 0.0f;
	float cachedMaxValue = 1.0f;
	float cachedMinLogValue = -8.0f;
	float cachedMaxLogValue = 0.0f;
	int cachedPlotWidth = 0;
	int cachedPlotHeight = 0;
	PaintMode currentPaintMode = PaintMode::Rgb;
	PaintMode cachedPaintMode = PaintMode::Rgb;

	GLuint texture = 0;

	std::mutex workerMutex;
	std::condition_variable workerCondition;
	std::thread workerThread;
	bool workerStop = false;
	bool hasPendingTask = false;
	bool hasReadyResult = false;
	BuildTask pendingTask;
	BuildResult readyResult;
	std::uint64_t latestRequestedSerial = 0;
	int requestedPlaneIdx = -1;
	int requestedMipIdx = -1;
	std::uint64_t requestedGeneration = 0;
	int requestedPlotWidth = 0;
	int requestedPlotHeight = 0;
	PaintMode requestedPaintMode = PaintMode::Rgb;
};
