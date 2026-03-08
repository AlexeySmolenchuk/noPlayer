#include "waveformPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace
{
constexpr float EPSILON = 1e-6f;
constexpr float DEFAULT_LOG_FLOOR = -8.0f;
constexpr float SPREAD_CARDINAL = 0.45f;
constexpr float SPREAD_DIAGONAL = 0.20f;

float toLogValue(float value, float minLogValue)
{
	if (!std::isfinite(value) || value <= EPSILON)
		return minLogValue;
	return std::log2(value);
}

float computeIntensity(std::uint32_t count, std::uint32_t maxCount)
{
	if (count == 0 || maxCount == 0)
		return 0.0f;
	const float normalized = std::log1p(static_cast<float>(count)) / std::log1p(static_cast<float>(maxCount));
	return std::sqrt(std::clamp(normalized, 0.0f, 1.0f));
}

float computeYuvLuma(float red, float green, float blue)
{
	// Y' only: BT.709 luma from RGB. Chroma (U/V) is intentionally not part of this mode.
	return 0.2126f * red + 0.7152f * green + 0.0722f * blue;
}

bool isSingleChannelMode(WaveformPanel::PaintMode mode)
{
	return mode == WaveformPanel::PaintMode::Red
		|| mode == WaveformPanel::PaintMode::Green
		|| mode == WaveformPanel::PaintMode::Blue;
}

int singleChannelIndex(WaveformPanel::PaintMode mode)
{
	switch (mode)
	{
	case WaveformPanel::PaintMode::Red:
		return 0;
	case WaveformPanel::PaintMode::Green:
		return 1;
	case WaveformPanel::PaintMode::Blue:
		return 2;
	default:
		return 0;
	}
}

void colorizeSingleChannel(WaveformPanel::PaintMode mode,
						   float intensity,
						   float& red,
						   float& green,
						   float& blue)
{
	red = 0.0f;
	green = 0.0f;
	blue = 0.0f;
	switch (mode)
	{
	case WaveformPanel::PaintMode::Red:
		red = intensity;
		break;
	case WaveformPanel::PaintMode::Green:
		green = intensity;
		break;
	case WaveformPanel::PaintMode::Blue:
		blue = intensity;
		break;
	default:
		red = intensity;
		green = intensity;
		blue = intensity;
		break;
	}
}

std::vector<float> buildIntensityMap(const std::vector<std::uint32_t>& histogram,
									 int width,
									 int height,
									 std::uint32_t maxCount)
{
	const size_t histogramSize = static_cast<size_t>(width) * static_cast<size_t>(height);
	std::vector<float> base(histogramSize, 0.0f);
	std::vector<float> spread(histogramSize, 0.0f);
	for (size_t index = 0; index < histogramSize; index++)
		base[index] = computeIntensity(histogram[index], maxCount);

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
			const float value = base[index];
			if (value <= 0.0f)
				continue;

			spread[index] = std::max(spread[index], value);
			if (x > 0)
				spread[index - 1] = std::max(spread[index - 1], value * SPREAD_CARDINAL);
			if (x + 1 < width)
				spread[index + 1] = std::max(spread[index + 1], value * SPREAD_CARDINAL);
			if (y > 0)
				spread[index - static_cast<size_t>(width)] = std::max(spread[index - static_cast<size_t>(width)], value * SPREAD_CARDINAL);
			if (y + 1 < height)
				spread[index + static_cast<size_t>(width)] = std::max(spread[index + static_cast<size_t>(width)], value * SPREAD_CARDINAL);

			if (x > 0 && y > 0)
				spread[index - static_cast<size_t>(width) - 1] = std::max(spread[index - static_cast<size_t>(width) - 1], value * SPREAD_DIAGONAL);
			if (x + 1 < width && y > 0)
				spread[index - static_cast<size_t>(width) + 1] = std::max(spread[index - static_cast<size_t>(width) + 1], value * SPREAD_DIAGONAL);
			if (x > 0 && y + 1 < height)
				spread[index + static_cast<size_t>(width) - 1] = std::max(spread[index + static_cast<size_t>(width) - 1], value * SPREAD_DIAGONAL);
			if (x + 1 < width && y + 1 < height)
				spread[index + static_cast<size_t>(width) + 1] = std::max(spread[index + static_cast<size_t>(width) + 1], value * SPREAD_DIAGONAL);
		}
	}

	return spread;
}

void accumulateTexturePixel(std::vector<unsigned char>& imageData,
							int width,
							int x,
							int y,
							float red,
							float green,
							float blue,
							float alpha)
{
	const size_t pixelOffset = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4ull;
	const auto clampToByte = [](float value)
	{
		const int scaled = static_cast<int>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
		return static_cast<unsigned char>(std::clamp(scaled, 0, 255));
	};

	imageData[pixelOffset + 0] = std::max(imageData[pixelOffset + 0], clampToByte(red));
	imageData[pixelOffset + 1] = std::max(imageData[pixelOffset + 1], clampToByte(green));
	imageData[pixelOffset + 2] = std::max(imageData[pixelOffset + 2], clampToByte(blue));
	imageData[pixelOffset + 3] = std::max(imageData[pixelOffset + 3], clampToByte(alpha));
}
}


WaveformPanel::WaveformPanel()
	: workerThread(&WaveformPanel::workerLoop, this)
{
}


WaveformPanel::~WaveformPanel()
{
	shutdownWorker();
}


void WaveformPanel::invalidate()
{
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		latestRequestedSerial++;
		hasPendingTask = false;
		hasReadyResult = false;
		requestedPlaneIdx = -1;
		requestedMipIdx = -1;
		requestedGeneration = 0;
		requestedPlotWidth = 0;
		requestedPlotHeight = 0;
		requestedPaintMode = currentPaintMode;
		requestedSelectionActive = false;
		requestedSelectionMinX = 0;
		requestedSelectionMinY = 0;
		requestedSelectionMaxX = 0;
		requestedSelectionMaxY = 0;
	}

	valid = false;
	cachedPlaneIdx = -1;
	cachedMipIdx = -1;
	cachedGeneration = 0;
	cachedIsRgb = false;
	cachedMinValue = 0.0f;
	cachedMaxValue = 1.0f;
	cachedMinLogValue = DEFAULT_LOG_FLOOR;
	cachedMaxLogValue = 0.0f;
	cachedPlotWidth = 0;
	cachedPlotHeight = 0;
	cachedPaintMode = currentPaintMode;
	cachedSelectionActive = false;
	cachedSelectionMinX = 0;
	cachedSelectionMinY = 0;
	cachedSelectionMaxX = 0;
	cachedSelectionMaxY = 0;
	hoverInfo = HoverInfo();
	releaseGlResources();
}


void WaveformPanel::releaseGlResources()
{
	if (texture != 0)
	{
		glDeleteTextures(1, &texture);
		texture = 0;
	}
}


void WaveformPanel::shutdownWorker()
{
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		if (workerStop)
			return;
		workerStop = true;
		hasPendingTask = false;
		hasReadyResult = false;
	}

	workerCondition.notify_all();
	if (workerThread.joinable())
		workerThread.join();
}


bool WaveformPanel::isValidFor(int planeIdx,
							   int mipIdx,
							   std::uint64_t generation,
							   int plotWidth,
							   int plotHeight,
							   PaintMode paintMode,
							   bool selectionActive,
							   int selectionMinX,
							   int selectionMinY,
							   int selectionMaxX,
							   int selectionMaxY) const
{
	return valid
		&& cachedPlaneIdx == planeIdx
		&& cachedMipIdx == mipIdx
		&& cachedGeneration == generation
		&& cachedPlotWidth == plotWidth
		&& cachedPlotHeight == plotHeight
		&& cachedPaintMode == paintMode
		&& cachedSelectionActive == selectionActive
		&& cachedSelectionMinX == selectionMinX
		&& cachedSelectionMinY == selectionMinY
		&& cachedSelectionMaxX == selectionMaxX
		&& cachedSelectionMaxY == selectionMaxY;
}


WaveformPanel::BuildResult WaveformPanel::buildWaveform(const BuildTask& task)
{
	BuildResult result;
	result.isRgb = task.isRgb;
	result.planeIdx = task.planeIdx;
	result.mipIdx = task.mipIdx;
	result.generation = task.generation;
	result.plotWidth = std::max(1, task.plotWidth);
	result.plotHeight = std::max(1, task.plotHeight);
	result.paintMode = task.paintMode;
	result.serial = task.serial;
	result.selectionActive = task.selectionActive;
	result.selectionMinX = task.selectionMinX;
	result.selectionMinY = task.selectionMinY;
	result.selectionMaxX = task.selectionMaxX;
	result.selectionMaxY = task.selectionMaxY;

	if (!task.pixels || task.imageWidth <= 0 || task.imageHeight <= 0 || task.channelCount <= 0)
		return result;

	const bool showRgbChannels = task.isRgb && task.paintMode == PaintMode::Rgb;
	const bool showSingleChannel = task.isRgb && isSingleChannelMode(task.paintMode);
	const int singleChannel = singleChannelIndex(task.paintMode);
	const int scopeWidth = result.plotWidth;
	const int scopeHeight = result.plotHeight;
	const size_t histogramSize = static_cast<size_t>(scopeWidth) * static_cast<size_t>(scopeHeight);
	const int sourceMinX = task.selectionActive ? std::clamp(task.selectionMinX, 0, task.imageWidth - 1) : 0;
	const int sourceMinY = task.selectionActive ? std::clamp(task.selectionMinY, 0, task.imageHeight - 1) : 0;
	const int sourceMaxX = task.selectionActive ? std::clamp(task.selectionMaxX, sourceMinX, task.imageWidth - 1) : task.imageWidth - 1;
	const int sourceMaxY = task.selectionActive ? std::clamp(task.selectionMaxY, sourceMinY, task.imageHeight - 1) : task.imageHeight - 1;
	const int sourceWidth = sourceMaxX - sourceMinX + 1;

	std::vector<std::uint32_t> histogramRed(histogramSize, 0u);
	std::vector<std::uint32_t> histogramGreen(histogramSize, 0u);
	std::vector<std::uint32_t> histogramBlue(histogramSize, 0u);
	std::vector<std::uint32_t> histogramWhite(histogramSize, 0u);
	std::vector<int> xBins(static_cast<size_t>(sourceWidth), 0);

	for (int x = 0; x < sourceWidth; x++)
	{
		if (scopeWidth <= 1 || sourceWidth <= 1)
		{
			xBins[static_cast<size_t>(x)] = 0;
			continue;
		}

		const long long numerator = static_cast<long long>(x) * static_cast<long long>(scopeWidth - 1);
		xBins[static_cast<size_t>(x)] = static_cast<int>(numerator / static_cast<long long>(sourceWidth - 1));
	}

	const precision* pixels = task.pixels.get();
	float minValue = std::numeric_limits<float>::max();
	float maxValue = std::numeric_limits<float>::lowest();
	float minPositiveValue = std::numeric_limits<float>::max();
	float maxPositiveValue = std::numeric_limits<float>::lowest();

	for (int y = sourceMinY; y <= sourceMaxY; y++)
	{
		for (int x = sourceMinX; x <= sourceMaxX; x++)
		{
			const size_t pixelOffset = (static_cast<size_t>(y) * static_cast<size_t>(task.imageWidth) + static_cast<size_t>(x))
									 * static_cast<size_t>(task.channelCount);
			if (showRgbChannels)
			{
				for (int channel = 0; channel < 3; channel++)
				{
					const float value = static_cast<float>(pixels[pixelOffset + static_cast<size_t>(channel)]);
					if (!std::isfinite(value))
						continue;
					minValue = std::min(minValue, value);
					maxValue = std::max(maxValue, value);
					if (value > EPSILON)
					{
						minPositiveValue = std::min(minPositiveValue, value);
						maxPositiveValue = std::max(maxPositiveValue, value);
					}
				}
			}
			else if (showSingleChannel)
			{
				const float value = static_cast<float>(pixels[pixelOffset + static_cast<size_t>(singleChannel)]);
				if (!std::isfinite(value))
					continue;
				minValue = std::min(minValue, value);
				maxValue = std::max(maxValue, value);
				if (value > EPSILON)
				{
					minPositiveValue = std::min(minPositiveValue, value);
					maxPositiveValue = std::max(maxPositiveValue, value);
				}
			}
			else
			{
				float value = static_cast<float>(pixels[pixelOffset]);
				if (task.isRgb)
				{
					const float red = static_cast<float>(pixels[pixelOffset + 0]);
					const float green = static_cast<float>(pixels[pixelOffset + 1]);
					const float blue = static_cast<float>(pixels[pixelOffset + 2]);
					if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue))
						continue;
					value = computeYuvLuma(red, green, blue);
				}

				if (!std::isfinite(value))
					continue;
				minValue = std::min(minValue, value);
				maxValue = std::max(maxValue, value);
				if (value > EPSILON)
				{
					minPositiveValue = std::min(minPositiveValue, value);
					maxPositiveValue = std::max(maxPositiveValue, value);
				}
			}
		}
	}

	if (!std::isfinite(minValue) || !std::isfinite(maxValue))
	{
		minValue = 0.0f;
		maxValue = 1.0f;
	}

	float displayMinValue = minValue;
	float displayMaxValue = maxValue;
	if (minValue >= 0.0f && maxValue <= 1.0f)
	{
		displayMinValue = 0.0f;
		displayMaxValue = 1.0f;
	}

	float minLogValue = DEFAULT_LOG_FLOOR;
	float maxLogValue = 0.0f;
	if (std::isfinite(minPositiveValue) && std::isfinite(maxPositiveValue))
	{
		minLogValue = std::floor(std::log2(std::max(minPositiveValue, EPSILON)));
		maxLogValue = std::ceil(std::log2(std::max(maxPositiveValue, EPSILON)));
	}
	if (displayMaxValue <= 1.0f && displayMinValue >= 0.0f)
		maxLogValue = 0.0f;
	if (std::fabs(maxLogValue - minLogValue) < EPSILON)
		maxLogValue = minLogValue + 1.0f;
	const float logValueRange = std::max(EPSILON, maxLogValue - minLogValue);
	const int lastScopeRow = scopeHeight - 1;

	std::uint32_t maxCountRed = 0u;
	std::uint32_t maxCountGreen = 0u;
	std::uint32_t maxCountBlue = 0u;
	std::uint32_t maxCountWhite = 0u;

	for (int y = sourceMinY; y <= sourceMaxY; y++)
	{
		for (int x = sourceMinX; x <= sourceMaxX; x++)
		{
			const int localX = x - sourceMinX;
			const size_t pixelOffset = (static_cast<size_t>(y) * static_cast<size_t>(task.imageWidth) + static_cast<size_t>(x))
									 * static_cast<size_t>(task.channelCount);
			if (showRgbChannels)
			{
				for (int channel = 0; channel < 3; channel++)
				{
					const float value = static_cast<float>(pixels[pixelOffset + static_cast<size_t>(channel)]);
					if (!std::isfinite(value))
						continue;

					const float normalized = std::clamp((toLogValue(value, minLogValue) - minLogValue) / logValueRange, 0.0f, 1.0f);
					const int binY = std::clamp(static_cast<int>(std::round(normalized * static_cast<float>(lastScopeRow))),
												0, lastScopeRow);
						const int binX = xBins[static_cast<size_t>(localX)];
					const size_t index = static_cast<size_t>(binY) * static_cast<size_t>(scopeWidth) + static_cast<size_t>(binX);

					if (channel == 0)
					{
						histogramRed[index]++;
						maxCountRed = std::max(maxCountRed, histogramRed[index]);
					}
					else if (channel == 1)
					{
						histogramGreen[index]++;
						maxCountGreen = std::max(maxCountGreen, histogramGreen[index]);
					}
					else
					{
						histogramBlue[index]++;
						maxCountBlue = std::max(maxCountBlue, histogramBlue[index]);
					}
				}
			}
			else if (showSingleChannel)
			{
				const float value = static_cast<float>(pixels[pixelOffset + static_cast<size_t>(singleChannel)]);
				if (!std::isfinite(value))
					continue;

				const float normalized = std::clamp((toLogValue(value, minLogValue) - minLogValue) / logValueRange, 0.0f, 1.0f);
				const int binY = std::clamp(static_cast<int>(std::round(normalized * static_cast<float>(lastScopeRow))),
											0, lastScopeRow);
					const int binX = xBins[static_cast<size_t>(localX)];
				const size_t index = static_cast<size_t>(binY) * static_cast<size_t>(scopeWidth) + static_cast<size_t>(binX);
				histogramWhite[index]++;
				maxCountWhite = std::max(maxCountWhite, histogramWhite[index]);
			}
			else
			{
				float value = static_cast<float>(pixels[pixelOffset]);
				if (task.isRgb)
				{
					const float red = static_cast<float>(pixels[pixelOffset + 0]);
					const float green = static_cast<float>(pixels[pixelOffset + 1]);
					const float blue = static_cast<float>(pixels[pixelOffset + 2]);
					if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue))
						continue;
					value = computeYuvLuma(red, green, blue);
				}

				if (!std::isfinite(value))
					continue;

				const float normalized = std::clamp((toLogValue(value, minLogValue) - minLogValue) / logValueRange, 0.0f, 1.0f);
				const int binY = std::clamp(static_cast<int>(std::round(normalized * static_cast<float>(lastScopeRow))),
											0, lastScopeRow);
					const int binX = xBins[static_cast<size_t>(localX)];
				const size_t index = static_cast<size_t>(binY) * static_cast<size_t>(scopeWidth) + static_cast<size_t>(binX);
				histogramWhite[index]++;
				maxCountWhite = std::max(maxCountWhite, histogramWhite[index]);
			}
		}
	}

	const std::vector<float> intensityRed = showRgbChannels
		? buildIntensityMap(histogramRed, scopeWidth, scopeHeight, maxCountRed)
		: std::vector<float>();
	const std::vector<float> intensityGreen = showRgbChannels
		? buildIntensityMap(histogramGreen, scopeWidth, scopeHeight, maxCountGreen)
		: std::vector<float>();
	const std::vector<float> intensityBlue = showRgbChannels
		? buildIntensityMap(histogramBlue, scopeWidth, scopeHeight, maxCountBlue)
		: std::vector<float>();
		const std::vector<float> intensityWhite = !showRgbChannels
		? buildIntensityMap(histogramWhite, scopeWidth, scopeHeight, maxCountWhite)
		: std::vector<float>();

	result.imageData.assign(histogramSize * 4ull, 0u);
	for (int binY = 0; binY < scopeHeight; binY++)
	{
		const int texY = scopeHeight - 1 - binY;
		for (int x = 0; x < scopeWidth; x++)
		{
			const size_t index = static_cast<size_t>(binY) * static_cast<size_t>(scopeWidth) + static_cast<size_t>(x);
			if (showRgbChannels)
			{
				const float red = intensityRed[index];
				const float green = intensityGreen[index];
				const float blue = intensityBlue[index];
				const float alpha = std::clamp(std::max({red, green, blue}), 0.0f, 1.0f);
				accumulateTexturePixel(result.imageData, scopeWidth, x, texY, red, green, blue, alpha);
			}
			else if (showSingleChannel)
			{
				const float mono = intensityWhite[index];
				float red = 0.0f;
				float green = 0.0f;
				float blue = 0.0f;
				colorizeSingleChannel(task.paintMode, mono, red, green, blue);
				accumulateTexturePixel(result.imageData, scopeWidth, x, texY, red, green, blue, mono);
			}
			else
			{
				const float mono = intensityWhite[index];
				accumulateTexturePixel(result.imageData, scopeWidth, x, texY, mono, mono, mono, mono);
			}
		}
	}

	result.minValue = minValue;
	result.maxValue = maxValue;
	result.minLogValue = minLogValue;
	result.maxLogValue = maxLogValue;
	return result;
}


void WaveformPanel::workerLoop()
{
	while (true)
	{
		BuildTask task;
		{
			std::unique_lock<std::mutex> lock(workerMutex);
			workerCondition.wait(lock, [this]()
			{
				return workerStop || hasPendingTask;
			});

			if (workerStop && !hasPendingTask)
				return;

			task = std::move(pendingTask);
			hasPendingTask = false;
		}

		BuildResult result = buildWaveform(task);

		std::lock_guard<std::mutex> lock(workerMutex);
		if (workerStop)
			return;

		if (task.serial != latestRequestedSerial
			|| task.planeIdx != requestedPlaneIdx
			|| task.mipIdx != requestedMipIdx
			|| task.generation != requestedGeneration
			|| task.plotWidth != requestedPlotWidth
			|| task.plotHeight != requestedPlotHeight
			|| task.paintMode != requestedPaintMode
			|| task.selectionActive != requestedSelectionActive
			|| task.selectionMinX != requestedSelectionMinX
			|| task.selectionMinY != requestedSelectionMinY
			|| task.selectionMaxX != requestedSelectionMaxX
			|| task.selectionMaxY != requestedSelectionMaxY)
		{
			continue;
		}

		readyResult = std::move(result);
		hasReadyResult = true;
	}
}


void WaveformPanel::requestBuild(const ImagePlaneData& planeData,
								 int planeIdx,
								 int mipIdx,
								 std::uint64_t generation,
								 int plotWidth,
								 int plotHeight,
								 bool selectionActive,
								 int selectionMinX,
								 int selectionMinY,
								 int selectionMaxX,
								 int selectionMaxY)
{
	if (!planeData.pixels || planeData.imageWidth == 0 || planeData.imageHeight == 0 || planeData.len <= 0)
		return;

	if (valid && (cachedPlaneIdx != planeIdx
		|| cachedMipIdx != mipIdx
		|| cachedGeneration != generation
		|| cachedPaintMode != currentPaintMode
		|| cachedSelectionActive != selectionActive
		|| cachedSelectionMinX != selectionMinX
		|| cachedSelectionMinY != selectionMinY
		|| cachedSelectionMaxX != selectionMaxX
		|| cachedSelectionMaxY != selectionMaxY))
	{
		valid = false;
		releaseGlResources();
	}

	if (isValidFor(planeIdx, mipIdx, generation, plotWidth, plotHeight, currentPaintMode,
				selectionActive, selectionMinX, selectionMinY, selectionMaxX, selectionMaxY))
		return;

	BuildTask task;
	task.pixels = planeData.pixels;
	task.imageWidth = static_cast<int>(planeData.imageWidth);
	task.imageHeight = static_cast<int>(planeData.imageHeight);
	task.channelCount = planeData.len;
	task.isRgb = isRgbChannels(planeData.channels) && planeData.len >= 3;
	task.selectionActive = selectionActive;
	task.selectionMinX = selectionMinX;
	task.selectionMinY = selectionMinY;
	task.selectionMaxX = selectionMaxX;
	task.selectionMaxY = selectionMaxY;
	task.planeIdx = planeIdx;
	task.mipIdx = mipIdx;
	task.generation = generation;
	task.plotWidth = plotWidth;
	task.plotHeight = plotHeight;
	task.paintMode = currentPaintMode;

	{
		std::lock_guard<std::mutex> lock(workerMutex);
		if (requestedPlaneIdx == planeIdx
			&& requestedMipIdx == mipIdx
			&& requestedGeneration == generation
			&& requestedPlotWidth == plotWidth
			&& requestedPlotHeight == plotHeight
			&& requestedPaintMode == currentPaintMode
			&& requestedSelectionActive == selectionActive
			&& requestedSelectionMinX == selectionMinX
			&& requestedSelectionMinY == selectionMinY
			&& requestedSelectionMaxX == selectionMaxX
			&& requestedSelectionMaxY == selectionMaxY)
		{
			return;
		}

		latestRequestedSerial++;
		task.serial = latestRequestedSerial;
		requestedPlaneIdx = planeIdx;
		requestedMipIdx = mipIdx;
		requestedGeneration = generation;
		requestedPlotWidth = plotWidth;
		requestedPlotHeight = plotHeight;
		requestedPaintMode = currentPaintMode;
		requestedSelectionActive = selectionActive;
		requestedSelectionMinX = selectionMinX;
		requestedSelectionMinY = selectionMinY;
		requestedSelectionMaxX = selectionMaxX;
		requestedSelectionMaxY = selectionMaxY;
		pendingTask = std::move(task);
		hasPendingTask = true;
	}

	workerCondition.notify_one();
}


void WaveformPanel::consumeReadyResult()
{
	BuildResult result;
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		if (!hasReadyResult)
			return;

		if (readyResult.serial != latestRequestedSerial
			|| readyResult.planeIdx != requestedPlaneIdx
			|| readyResult.mipIdx != requestedMipIdx
			|| readyResult.generation != requestedGeneration
			|| readyResult.plotWidth != requestedPlotWidth
			|| readyResult.plotHeight != requestedPlotHeight
			|| readyResult.paintMode != requestedPaintMode
			|| readyResult.selectionActive != requestedSelectionActive
			|| readyResult.selectionMinX != requestedSelectionMinX
			|| readyResult.selectionMinY != requestedSelectionMinY
			|| readyResult.selectionMaxX != requestedSelectionMaxX
			|| readyResult.selectionMaxY != requestedSelectionMaxY)
		{
			hasReadyResult = false;
			return;
		}

		result = std::move(readyResult);
		hasReadyResult = false;
	}

	if (result.imageData.empty())
		return;

	if (texture == 0)
		glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D,
				0,
				GL_RGBA8,
				result.plotWidth,
				result.plotHeight,
				0,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				result.imageData.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	valid = true;
	cachedPlaneIdx = result.planeIdx;
	cachedMipIdx = result.mipIdx;
	cachedGeneration = result.generation;
	cachedIsRgb = result.isRgb;
	cachedMinValue = result.minValue;
	cachedMaxValue = result.maxValue;
	cachedMinLogValue = result.minLogValue;
	cachedMaxLogValue = result.maxLogValue;
	cachedPlotWidth = result.plotWidth;
	cachedPlotHeight = result.plotHeight;
	cachedPaintMode = result.paintMode;
	cachedSelectionActive = result.selectionActive;
	cachedSelectionMinX = result.selectionMinX;
	cachedSelectionMinY = result.selectionMinY;
	cachedSelectionMaxX = result.selectionMaxX;
	cachedSelectionMaxY = result.selectionMaxY;
}


void WaveformPanel::draw(int panelWidth,
						 int panelHeight,
						 float unit,
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
						 const SampleOverlayInfo* sampleOverlayInfo)
{
	(void)unit;

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(static_cast<float>(panelWidth), static_cast<float>(panelHeight)), ImGuiCond_Always);
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove
								| ImGuiWindowFlags_NoResize
								| ImGuiWindowFlags_NoCollapse
								| ImGuiWindowFlags_NoTitleBar
								| ImGuiWindowFlags_NoSavedSettings;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.07f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
	ImGui::Begin("Waveform", nullptr, windowFlags);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##paint_mode", paintModeLabel(currentPaintMode)))
		{
			for (int modeIndex = 0; modeIndex < 5; modeIndex++)
			{
				const PaintMode mode = static_cast<PaintMode>(modeIndex);
				const bool selected = currentPaintMode == mode;
				if (ImGui::Selectable(paintModeLabel(mode), selected))
				{
					currentPaintMode = mode;
					invalidate();
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		const float axisWidth = 44.0f;
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 plotButtonSize(std::max(1.0f, available.x), std::max(80.0f, available.y));
		ImGui::InvisibleButton("WaveformPlot", plotButtonSize);

		const ImVec2 frameMin = ImGui::GetItemRectMin();
		const ImVec2 frameMax = ImGui::GetItemRectMax();
		const ImVec2 plotMin = frameMin;
		const ImVec2 plotMax(frameMax.x - axisWidth, frameMax.y);
		const float plotWidth = std::max(1.0f, plotMax.x - plotMin.x);
		const float plotHeight = std::max(1.0f, plotMax.y - plotMin.y);
	const int plotTextureWidth = std::max(1, static_cast<int>(std::round(plotWidth)));
	const int plotTextureHeight = std::max(1, static_cast<int>(std::round(plotHeight)));

	if (sourceReady && planeData != nullptr)
		requestBuild(*planeData, planeIdx, mipIdx, generation,
					plotTextureWidth, plotTextureHeight,
					selectionActive, selectionMinX, selectionMinY, selectionMaxX, selectionMaxY);
	consumeReadyResult();

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(frameMin, frameMax, IM_COL32(15, 15, 15, 255));
	drawList->AddRectFilled(plotMin, plotMax, IM_COL32(14, 14, 14, 255));

	const int verticalDivisions = 8;
	const int horizontalDivisions = 8;
	for (int division = 1; division < verticalDivisions; division++)
	{
		const float x = plotMin.x + (plotWidth * static_cast<float>(division) / static_cast<float>(verticalDivisions));
		drawList->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), IM_COL32(42, 48, 36, 130), 1.0f);
	}

	if (valid)
	{
		const float logSpan = std::max(EPSILON, cachedMaxLogValue - cachedMinLogValue);
		const int firstStop = static_cast<int>(std::ceil(cachedMinLogValue));
		const int lastStop = static_cast<int>(std::floor(cachedMaxLogValue));
		const int stopStep = std::max(1, static_cast<int>(std::ceil(logSpan / static_cast<float>(horizontalDivisions))));
		for (int stop = firstStop; stop <= lastStop; stop += stopStep)
		{
			const float normalized = std::clamp((static_cast<float>(stop) - cachedMinLogValue) / logSpan, 0.0f, 1.0f);
			const float y = plotMax.y - normalized * plotHeight;
			drawList->AddLine(ImVec2(plotMin.x, y), ImVec2(frameMax.x, y), IM_COL32(78, 88, 66, 110), 1.0f);
		}
	}
	else
	{
		for (int division = 0; division <= horizontalDivisions; division++)
		{
			const float y = plotMin.y + (plotHeight * static_cast<float>(division) / static_cast<float>(horizontalDivisions));
			drawList->AddLine(ImVec2(plotMin.x, y), ImVec2(frameMax.x, y), IM_COL32(42, 48, 36, 130), 1.0f);
		}
	}

	if (texture != 0)
	{
		drawList->AddImage(static_cast<ImTextureID>(static_cast<uintptr_t>(texture)),
						plotMin, plotMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
	}
	else
	{
		const char* statusMsg = sourceReady
			? (selectionActive ? "Building selection waveform..." : "Building waveform...")
			: "Loading waveform...";
		const ImVec2 statusSize = ImGui::CalcTextSize(statusMsg);
		drawList->AddText(ImVec2(plotMin.x + (plotWidth - statusSize.x) * 0.5f,
								plotMin.y + (plotHeight - statusSize.y) * 0.5f),
							IM_COL32(170, 170, 170, 255), statusMsg);
	}

	hoverInfo = HoverInfo();
	if (valid && planeData != nullptr && planeData->imageWidth > 0 && planeData->imageHeight > 0)
	{
		const ImVec2 mousePos = ImGui::GetIO().MousePos;
		const bool hoveredPlot = mousePos.x >= plotMin.x
			&& mousePos.x < plotMax.x
			&& mousePos.y >= plotMin.y
			&& mousePos.y < plotMax.y;
		if (hoveredPlot)
		{
			const int sourceMinX = cachedSelectionActive ? std::clamp(cachedSelectionMinX, 0, static_cast<int>(planeData->imageWidth) - 1) : 0;
			const int sourceMinY = cachedSelectionActive ? std::clamp(cachedSelectionMinY, 0, static_cast<int>(planeData->imageHeight) - 1) : 0;
			const int sourceMaxX = cachedSelectionActive ? std::clamp(cachedSelectionMaxX, sourceMinX, static_cast<int>(planeData->imageWidth) - 1) : static_cast<int>(planeData->imageWidth) - 1;
			const int sourceMaxY = cachedSelectionActive ? std::clamp(cachedSelectionMaxY, sourceMinY, static_cast<int>(planeData->imageHeight) - 1) : static_cast<int>(planeData->imageHeight) - 1;
			const int sourceWidth = std::max(1, sourceMaxX - sourceMinX + 1);
			const float normalizedX = std::clamp((mousePos.x - plotMin.x) / plotWidth, 0.0f, 1.0f);
			const float normalizedY = std::clamp((plotMax.y - mousePos.y) / plotHeight, 0.0f, 1.0f);
			const int sourceX = sourceMinX + static_cast<int>(std::round(normalizedX * static_cast<float>(sourceWidth - 1)));
			const float targetLogValue = cachedMinLogValue + normalizedY * std::max(EPSILON, cachedMaxLogValue - cachedMinLogValue);

			hoverInfo.active = true;
			hoverInfo.paintMode = cachedPaintMode;
			hoverInfo.sourceX = std::clamp(sourceX, sourceMinX, sourceMaxX);
			hoverInfo.sourceMinY = sourceMinY;
			hoverInfo.sourceMaxY = sourceMaxY;
			hoverInfo.minLogValue = cachedMinLogValue;
			hoverInfo.targetLogValue = targetLogValue;
		}
	}

	if (valid && sampleOverlayInfo != nullptr && sampleOverlayInfo->active && planeData != nullptr && planeData->imageWidth > 0)
	{
		const int sourceMinX = cachedSelectionActive ? std::clamp(cachedSelectionMinX, 0, static_cast<int>(planeData->imageWidth) - 1) : 0;
		const int sourceMaxX = cachedSelectionActive ? std::clamp(cachedSelectionMaxX, sourceMinX, static_cast<int>(planeData->imageWidth) - 1) : static_cast<int>(planeData->imageWidth) - 1;
		if (sampleOverlayInfo->sourceX >= sourceMinX && sampleOverlayInfo->sourceX <= sourceMaxX)
		{
			const float normalizedX = (sourceMaxX > sourceMinX)
				? static_cast<float>(sampleOverlayInfo->sourceX - sourceMinX) / static_cast<float>(sourceMaxX - sourceMinX)
				: 0.0f;
			const float plotX = plotMin.x + normalizedX * plotWidth;
			const float logSpan = std::max(EPSILON, cachedMaxLogValue - cachedMinLogValue);
			const bool showRgbOverlay = cachedPaintMode == PaintMode::Rgb && sampleOverlayInfo->isRgb && sampleOverlayInfo->channelCount >= 3;

			auto drawHorizontalMarker = [&](float value, ImU32 color)
			{
				if (!std::isfinite(value))
					return;
				const float normalized = std::clamp((toLogValue(value, cachedMinLogValue) - cachedMinLogValue) / logSpan, 0.0f, 1.0f);
				const float y = plotMax.y - normalized * plotHeight;
				drawList->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), IM_COL32(0, 0, 0, 180), 1.0f);
				drawList->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), color, 0.5f);
				drawList->AddCircleFilled(ImVec2(plotX, y), 1.2f, color);
			};

			ImU32 verticalColor = IM_COL32(240, 240, 220, 220);
			if (!showRgbOverlay)
			{
				if (sampleOverlayInfo->isRgb && sampleOverlayInfo->channelCount >= 3)
				{
					if (cachedPaintMode == PaintMode::Red)
						verticalColor = IM_COL32(255, 96, 96, 220);
					else if (cachedPaintMode == PaintMode::Green)
						verticalColor = IM_COL32(96, 255, 96, 220);
					else if (cachedPaintMode == PaintMode::Blue)
						verticalColor = IM_COL32(96, 160, 255, 220);
				}
			}

			drawList->PushClipRect(plotMin, plotMax, true);
			drawList->AddLine(ImVec2(plotX, plotMin.y), ImVec2(plotX, plotMax.y), IM_COL32(0, 0, 0, 180), 1.0f);
			drawList->AddLine(ImVec2(plotX, plotMin.y), ImVec2(plotX, plotMax.y), verticalColor, 0.5f);

			if (showRgbOverlay)
			{
				drawHorizontalMarker(sampleOverlayInfo->values[0], IM_COL32(255, 96, 96, 220));
				drawHorizontalMarker(sampleOverlayInfo->values[1], IM_COL32(96, 255, 96, 220));
				drawHorizontalMarker(sampleOverlayInfo->values[2], IM_COL32(96, 160, 255, 220));
			}
			else if (sampleOverlayInfo->isRgb && sampleOverlayInfo->channelCount >= 3)
			{
				float value = computeYuvLuma(sampleOverlayInfo->values[0], sampleOverlayInfo->values[1], sampleOverlayInfo->values[2]);
				if (cachedPaintMode == PaintMode::Red)
					value = sampleOverlayInfo->values[0];
				else if (cachedPaintMode == PaintMode::Green)
					value = sampleOverlayInfo->values[1];
				else if (cachedPaintMode == PaintMode::Blue)
					value = sampleOverlayInfo->values[2];
				drawHorizontalMarker(value, verticalColor);
			}
			else if (sampleOverlayInfo->channelCount > 0)
			{
				drawHorizontalMarker(sampleOverlayInfo->values[0], verticalColor);
			}
			drawList->PopClipRect();
		}
	}

	if (valid)
	{
		const float logSpan = std::max(EPSILON, cachedMaxLogValue - cachedMinLogValue);
		const int firstStop = static_cast<int>(std::ceil(cachedMinLogValue));
		const int lastStop = static_cast<int>(std::floor(cachedMaxLogValue));
		const int stopStep = std::max(1, static_cast<int>(std::ceil(logSpan / static_cast<float>(horizontalDivisions))));
		for (int stop = firstStop; stop <= lastStop; stop += stopStep)
		{
			const float normalized = std::clamp((static_cast<float>(stop) - cachedMinLogValue) / logSpan, 0.0f, 1.0f);
			const float y = plotMax.y - normalized * plotHeight;
			char label[32];
			const float value = std::exp2(static_cast<float>(stop));
			std::snprintf(label, sizeof(label), "%.3f", value);
			const ImVec2 labelSize = ImGui::CalcTextSize(label);
				drawList->AddText(ImVec2(frameMax.x - labelSize.x - 6.0f, y - labelSize.y * 0.5f),
								IM_COL32(128, 136, 126, 220), label);
		}
	}

	drawList->AddRect(plotMin, plotMax, IM_COL32(90, 96, 84, 180), 0.0f, 0, 1.0f);
	drawList->AddLine(ImVec2(plotMax.x, plotMin.y), ImVec2(plotMax.x, plotMax.y), IM_COL32(90, 96, 84, 180), 1.0f);
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(2);
}


const char* WaveformPanel::paintModeLabel(PaintMode mode)
{
	switch (mode)
	{
	case PaintMode::LuminanceYuv:
		return "Luminance - YUV";
	case PaintMode::Rgb:
		return "RGB";
	case PaintMode::Red:
		return "R";
	case PaintMode::Green:
		return "G";
	case PaintMode::Blue:
		return "B";
	}
	return "Luminance - YUV";
}


bool WaveformPanel::isRgbChannels(const std::string& channels)
{
	if (channels.size() < 3)
		return false;

	auto isChannel = [](char value, char expected)
	{
		return value == expected || value == static_cast<char>(expected - 'a' + 'A');
	};

	return isChannel(channels[0], 'r')
		&& isChannel(channels[1], 'g')
		&& isChannel(channels[2], 'b');
}
