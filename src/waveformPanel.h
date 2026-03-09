#pragma once

#include "imageChannelUtils.h"
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
		LuminanceYuv = 0,
		Rgb,
		Red,
		Green,
		Blue,
	};

	struct HoverInfo
	{
		bool active = false;
		PaintMode paintMode = PaintMode::LuminanceYuv;
		int sourceX = 0;
		int sourceMinY = 0;
		int sourceMaxY = 0;
		float minLogValue = -8.0f;
		float targetLogValue = 0.0f;
	};

	struct SampleOverlayInfo
	{
		bool active = false;
		ImageChannelUtils::WaveformColorInterpretation colorInterpretation = ImageChannelUtils::WaveformColorInterpretation::Mono;
		int channelCount = 0;
		int sourceX = 0;
		float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	};

	/**
	 * @brief Release cached waveform content.
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
			bool deferSizeRebuild,
			bool sourceReady,
			const ImagePlaneData* planeData,
			int planeIdx,
			int mipIdx,
			std::uint64_t generation,
			bool selectionActive,
			int selectionMinX,
			int selectionMinY,
			int selectionMaxX,
			int selectionMaxY,
			const SampleOverlayInfo* sampleOverlayInfo);

	const HoverInfo& getHoverInfo() const { return hoverInfo; }

private:
	struct WaveformRequestKey
	{
		int planeIdx = -1;
		int mipIdx = -1;
		std::uint64_t generation = 0;
		int plotWidth = 0;
		int plotHeight = 0;
		PaintMode paintMode = PaintMode::LuminanceYuv;
		bool selectionActive = false;
		int selectionMinX = 0;
		int selectionMinY = 0;
		int selectionMaxX = 0;
		int selectionMaxY = 0;

		bool operator==(const WaveformRequestKey& other) const
		{
			return planeIdx == other.planeIdx
				&& mipIdx == other.mipIdx
				&& generation == other.generation
				&& plotWidth == other.plotWidth
				&& plotHeight == other.plotHeight
				&& paintMode == other.paintMode
				&& selectionActive == other.selectionActive
				&& selectionMinX == other.selectionMinX
				&& selectionMinY == other.selectionMinY
				&& selectionMaxX == other.selectionMaxX
				&& selectionMaxY == other.selectionMaxY;
		}

		bool operator!=(const WaveformRequestKey& other) const
		{
			return !(*this == other);
		}
	};

	struct BuildTask
	{
		std::shared_ptr<precision[]> pixels;
		int imageWidth = 0;
		int imageHeight = 0;
		int channelCount = 0;
		ImageChannelUtils::WaveformColorInterpretation colorInterpretation = ImageChannelUtils::WaveformColorInterpretation::Mono;
		WaveformRequestKey key;
		std::uint64_t serial = 0;
	};

	struct BuildResult
	{
		std::vector<unsigned char> imageData;
		float minValue = 0.0f;
		float maxValue = 1.0f;
		float minLogValue = -8.0f;
		float maxLogValue = 0.0f;
		WaveformRequestKey key;
		std::uint64_t serial = 0;
	};

	struct ScratchBuffers
	{
		std::vector<std::uint32_t> histogramRed;
		std::vector<std::uint32_t> histogramGreen;
		std::vector<std::uint32_t> histogramBlue;
		std::vector<std::uint32_t> histogramWhite;
		std::vector<float> intensityRed;
		std::vector<float> intensityGreen;
		std::vector<float> intensityBlue;
		std::vector<float> intensityWhite;
		std::vector<float> tempBase;
		std::vector<int> xBins;
	};

	static const char* paintModeLabel(PaintMode mode);
	static BuildResult buildWaveform(const BuildTask& task, ScratchBuffers& scratch);

	void workerLoop();
	WaveformRequestKey makeRequestKey(int planeIdx,
									int mipIdx,
									std::uint64_t generation,
									int plotWidth,
									int plotHeight,
									bool selectionActive,
									int selectionMinX,
									int selectionMinY,
									int selectionMaxX,
									int selectionMaxY) const;
	void requestBuild(const ImagePlaneData& planeData, const WaveformRequestKey& key);
	void consumeReadyResult();
	bool isValidFor(const WaveformRequestKey& key) const;
	static bool matchesContentIgnoringPlotSize(const WaveformRequestKey& left,
												 const WaveformRequestKey& right);
	void resetCachedState();

private:
	bool valid = false;
	WaveformRequestKey cachedKey;
	float cachedMinValue = 0.0f;
	float cachedMaxValue = 1.0f;
	float cachedMinLogValue = -8.0f;
	float cachedMaxLogValue = 0.0f;
	PaintMode currentPaintMode = PaintMode::LuminanceYuv;
	HoverInfo hoverInfo;

	GLuint texture = 0;
	int textureWidth = 0;
	int textureHeight = 0;
	ScratchBuffers workerScratch;

	std::mutex workerMutex;
	std::condition_variable workerCondition;
	std::thread workerThread;
	bool workerStop = false;
	bool hasPendingTask = false;
	bool hasReadyResult = false;
	BuildTask pendingTask;
	BuildResult readyResult;
	std::uint64_t latestRequestedSerial = 0;
	WaveformRequestKey requestedKey;
};
