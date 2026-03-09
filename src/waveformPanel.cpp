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
	return ImageChannelUtils::toLog2OrFloor(value, minLogValue, EPSILON);
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

template <typename T>
void resizeAndFill(std::vector<T>& values, size_t size, const T& fillValue)
{
	values.assign(size, fillValue);
}

void buildIntensityMap(const std::vector<std::uint32_t>& histogram,
					   int width,
					   int height,
					   std::uint32_t maxCount,
					   std::vector<float>& base,
					   std::vector<float>& spread)
{
	const size_t histogramSize = static_cast<size_t>(width) * static_cast<size_t>(height);
	resizeAndFill(base, histogramSize, 0.0f);
	resizeAndFill(spread, histogramSize, 0.0f);
	if (maxCount != 0)
	{
		const float logMaxCount = std::log1p(static_cast<float>(maxCount));
		for (size_t index = 0; index < histogramSize; index++)
		{
			const std::uint32_t count = histogram[index];
			if (count == 0)
				continue;

			const float normalized = std::log1p(static_cast<float>(count)) / logMaxCount;
			base[index] = std::sqrt(std::clamp(normalized, 0.0f, 1.0f));
		}
	}

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

}

void setTexturePixel(std::vector<unsigned char>& imageData,
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

	imageData[pixelOffset + 0] = clampToByte(red);
	imageData[pixelOffset + 1] = clampToByte(green);
	imageData[pixelOffset + 2] = clampToByte(blue);
	imageData[pixelOffset + 3] = clampToByte(alpha);
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


void WaveformPanel::resetCachedState()
{
	valid = false;
	cachedKey = WaveformRequestKey();
	cachedIsRgb = false;
	cachedMinValue = 0.0f;
	cachedMaxValue = 1.0f;
	cachedMinLogValue = DEFAULT_LOG_FLOOR;
	cachedMaxLogValue = 0.0f;
	hoverInfo = HoverInfo();
}


void WaveformPanel::invalidate()
{
	{
		std::lock_guard<std::mutex> lock(workerMutex);
		latestRequestedSerial++;
		hasPendingTask = false;
		hasReadyResult = false;
		requestedKey = WaveformRequestKey();
	}

	resetCachedState();
}


void WaveformPanel::releaseGlResources()
{
	if (texture != 0)
	{
		glDeleteTextures(1, &texture);
		texture = 0;
	}
	textureWidth = 0;
	textureHeight = 0;
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


WaveformPanel::WaveformRequestKey WaveformPanel::makeRequestKey(int planeIdx,
																int mipIdx,
																std::uint64_t generation,
																int plotWidth,
																int plotHeight,
																bool selectionActive,
																int selectionMinX,
																int selectionMinY,
																int selectionMaxX,
																int selectionMaxY) const
{
	WaveformRequestKey key;
	key.planeIdx = planeIdx;
	key.mipIdx = mipIdx;
	key.generation = generation;
	key.plotWidth = plotWidth;
	key.plotHeight = plotHeight;
	key.paintMode = currentPaintMode;
	key.selectionActive = selectionActive;
	key.selectionMinX = selectionMinX;
	key.selectionMinY = selectionMinY;
	key.selectionMaxX = selectionMaxX;
	key.selectionMaxY = selectionMaxY;
	return key;
}


bool WaveformPanel::isValidFor(const WaveformRequestKey& key) const
{
	return valid && cachedKey == key;
}


bool WaveformPanel::matchesContentIgnoringPlotSize(const WaveformRequestKey& left,
												   const WaveformRequestKey& right)
{
	return left.planeIdx == right.planeIdx
		&& left.mipIdx == right.mipIdx
		&& left.generation == right.generation
		&& left.paintMode == right.paintMode
		&& left.selectionActive == right.selectionActive
		&& left.selectionMinX == right.selectionMinX
		&& left.selectionMinY == right.selectionMinY
		&& left.selectionMaxX == right.selectionMaxX
		&& left.selectionMaxY == right.selectionMaxY;
}


WaveformPanel::BuildResult WaveformPanel::buildWaveform(const BuildTask& task, ScratchBuffers& scratch)
{
	BuildResult result;
	result.isRgb = task.isRgb;
	result.key = task.key;
	result.key.plotWidth = std::max(1, result.key.plotWidth);
	result.key.plotHeight = std::max(1, result.key.plotHeight);
	result.serial = task.serial;

	if (!task.pixels || task.imageWidth <= 0 || task.imageHeight <= 0 || task.channelCount <= 0)
		return result;

	const bool showRgbChannels = task.isRgb && task.key.paintMode == PaintMode::Rgb;
	const bool showSingleChannel = task.isRgb && isSingleChannelMode(task.key.paintMode);
	const int singleChannel = singleChannelIndex(task.key.paintMode);
	const int scopeWidth = result.key.plotWidth;
	const int scopeHeight = result.key.plotHeight;
	const size_t histogramSize = static_cast<size_t>(scopeWidth) * static_cast<size_t>(scopeHeight);
	const int sourceMinX = task.key.selectionActive ? std::clamp(task.key.selectionMinX, 0, task.imageWidth - 1) : 0;
	const int sourceMinY = task.key.selectionActive ? std::clamp(task.key.selectionMinY, 0, task.imageHeight - 1) : 0;
	const int sourceMaxX = task.key.selectionActive ? std::clamp(task.key.selectionMaxX, sourceMinX, task.imageWidth - 1) : task.imageWidth - 1;
	const int sourceMaxY = task.key.selectionActive ? std::clamp(task.key.selectionMaxY, sourceMinY, task.imageHeight - 1) : task.imageHeight - 1;
	const int sourceWidth = sourceMaxX - sourceMinX + 1;

	if (showRgbChannels)
	{
		resizeAndFill(scratch.histogramRed, histogramSize, 0u);
		resizeAndFill(scratch.histogramGreen, histogramSize, 0u);
		resizeAndFill(scratch.histogramBlue, histogramSize, 0u);
	}
	else
	{
		resizeAndFill(scratch.histogramWhite, histogramSize, 0u);
	}
	scratch.xBins.resize(static_cast<size_t>(sourceWidth));

	for (int x = 0; x < sourceWidth; x++)
	{
		if (scopeWidth <= 1 || sourceWidth <= 1)
		{
			scratch.xBins[static_cast<size_t>(x)] = 0;
			continue;
		}

		const long long numerator = static_cast<long long>(x) * static_cast<long long>(scopeWidth - 1);
		scratch.xBins[static_cast<size_t>(x)] = static_cast<int>(numerator / static_cast<long long>(sourceWidth - 1));
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
					value = ImageChannelUtils::computeBt709Luma(red, green, blue);
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
						const int binX = scratch.xBins[static_cast<size_t>(localX)];
					const size_t index = static_cast<size_t>(binY) * static_cast<size_t>(scopeWidth) + static_cast<size_t>(binX);

					if (channel == 0)
					{
						scratch.histogramRed[index]++;
						maxCountRed = std::max(maxCountRed, scratch.histogramRed[index]);
					}
					else if (channel == 1)
					{
						scratch.histogramGreen[index]++;
						maxCountGreen = std::max(maxCountGreen, scratch.histogramGreen[index]);
					}
					else
					{
						scratch.histogramBlue[index]++;
						maxCountBlue = std::max(maxCountBlue, scratch.histogramBlue[index]);
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
					const int binX = scratch.xBins[static_cast<size_t>(localX)];
				const size_t index = static_cast<size_t>(binY) * static_cast<size_t>(scopeWidth) + static_cast<size_t>(binX);
				scratch.histogramWhite[index]++;
				maxCountWhite = std::max(maxCountWhite, scratch.histogramWhite[index]);
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
					value = ImageChannelUtils::computeBt709Luma(red, green, blue);
				}

				if (!std::isfinite(value))
					continue;

				const float normalized = std::clamp((toLogValue(value, minLogValue) - minLogValue) / logValueRange, 0.0f, 1.0f);
				const int binY = std::clamp(static_cast<int>(std::round(normalized * static_cast<float>(lastScopeRow))),
											0, lastScopeRow);
					const int binX = scratch.xBins[static_cast<size_t>(localX)];
				const size_t index = static_cast<size_t>(binY) * static_cast<size_t>(scopeWidth) + static_cast<size_t>(binX);
				scratch.histogramWhite[index]++;
				maxCountWhite = std::max(maxCountWhite, scratch.histogramWhite[index]);
			}
		}
	}

	if (showRgbChannels)
	{
		buildIntensityMap(scratch.histogramRed, scopeWidth, scopeHeight, maxCountRed, scratch.tempBase, scratch.intensityRed);
		buildIntensityMap(scratch.histogramGreen, scopeWidth, scopeHeight, maxCountGreen, scratch.tempBase, scratch.intensityGreen);
		buildIntensityMap(scratch.histogramBlue, scopeWidth, scopeHeight, maxCountBlue, scratch.tempBase, scratch.intensityBlue);
	}
	else
	{
		buildIntensityMap(scratch.histogramWhite, scopeWidth, scopeHeight, maxCountWhite, scratch.tempBase, scratch.intensityWhite);
	}

	result.imageData.resize(histogramSize * 4ull);
	for (int binY = 0; binY < scopeHeight; binY++)
	{
		const int texY = scopeHeight - 1 - binY;
		for (int x = 0; x < scopeWidth; x++)
		{
			const size_t index = static_cast<size_t>(binY) * static_cast<size_t>(scopeWidth) + static_cast<size_t>(x);
			if (showRgbChannels)
			{
				const float red = scratch.intensityRed[index];
				const float green = scratch.intensityGreen[index];
				const float blue = scratch.intensityBlue[index];
				const float alpha = std::clamp(std::max({red, green, blue}), 0.0f, 1.0f);
				setTexturePixel(result.imageData, scopeWidth, x, texY, red, green, blue, alpha);
			}
			else if (showSingleChannel)
			{
				const float mono = scratch.intensityWhite[index];
				float red = 0.0f;
				float green = 0.0f;
				float blue = 0.0f;
				colorizeSingleChannel(task.key.paintMode, mono, red, green, blue);
				setTexturePixel(result.imageData, scopeWidth, x, texY, red, green, blue, mono);
			}
			else
			{
				const float mono = scratch.intensityWhite[index];
				setTexturePixel(result.imageData, scopeWidth, x, texY, mono, mono, mono, mono);
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

		BuildResult result = buildWaveform(task, workerScratch);

		std::lock_guard<std::mutex> lock(workerMutex);
		if (workerStop)
			return;

		if (task.serial != latestRequestedSerial || task.key != requestedKey)
		{
			continue;
		}

		readyResult = std::move(result);
		hasReadyResult = true;
	}
}


void WaveformPanel::requestBuild(const ImagePlaneData& planeData, const WaveformRequestKey& key)
{
	if (!planeData.pixels || planeData.imageWidth == 0 || planeData.imageHeight == 0 || planeData.len <= 0)
		return;

	if (valid && cachedKey != key && !matchesContentIgnoringPlotSize(cachedKey, key))
	{
		resetCachedState();
	}

	if (isValidFor(key))
		return;

	BuildTask task;
	task.pixels = planeData.pixels;
	task.imageWidth = static_cast<int>(planeData.imageWidth);
	task.imageHeight = static_cast<int>(planeData.imageHeight);
	task.channelCount = planeData.len;
	task.isRgb = ImageChannelUtils::isRgbChannels(planeData.channels) && planeData.len >= 3;
	task.key = key;

	{
		std::lock_guard<std::mutex> lock(workerMutex);
		if (requestedKey == key)
		{
			return;
		}

		latestRequestedSerial++;
		task.serial = latestRequestedSerial;
		requestedKey = key;
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

		if (readyResult.serial != latestRequestedSerial || readyResult.key != requestedKey)
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
	{
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	else
	{
		glBindTexture(GL_TEXTURE_2D, texture);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if (textureWidth == result.key.plotWidth && textureHeight == result.key.plotHeight)
	{
		glTexSubImage2D(GL_TEXTURE_2D,
						0,
						0,
						0,
						result.key.plotWidth,
						result.key.plotHeight,
						GL_RGBA,
						GL_UNSIGNED_BYTE,
						result.imageData.data());
	}
	else
	{
		glTexImage2D(GL_TEXTURE_2D,
					0,
					GL_RGBA8,
					result.key.plotWidth,
					result.key.plotHeight,
					0,
					GL_RGBA,
					GL_UNSIGNED_BYTE,
					result.imageData.data());
		textureWidth = result.key.plotWidth;
		textureHeight = result.key.plotHeight;
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	valid = true;
	cachedKey = result.key;
	cachedIsRgb = result.isRgb;
	cachedMinValue = result.minValue;
	cachedMaxValue = result.maxValue;
	cachedMinLogValue = result.minLogValue;
	cachedMaxLogValue = result.maxLogValue;
}


void WaveformPanel::draw(int panelWidth,
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
		const WaveformRequestKey requestKey = makeRequestKey(planeIdx, mipIdx, generation,
																plotTextureWidth, plotTextureHeight,
																selectionActive, selectionMinX, selectionMinY,
																selectionMaxX, selectionMaxY);
		bool deferBuildForSizeChange = false;
		if (deferSizeRebuild)
		{
			const bool cachedSizeMismatch = valid
				&& matchesContentIgnoringPlotSize(cachedKey, requestKey)
				&& cachedKey != requestKey;
			bool requestedSizeMismatch = false;
			{
				std::lock_guard<std::mutex> lock(workerMutex);
				requestedSizeMismatch = matchesContentIgnoringPlotSize(requestedKey, requestKey)
					&& requestedKey != requestKey;
			}
			deferBuildForSizeChange = cachedSizeMismatch || requestedSizeMismatch;
		}

		if (!sourceReady || planeData == nullptr)
		{
			if (valid
				&& !isValidFor(requestKey)
				&& !matchesContentIgnoringPlotSize(cachedKey, requestKey))
				resetCachedState();
		}
		else if (!deferBuildForSizeChange)
		{
			requestBuild(*planeData, requestKey);
		}
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

	if (valid && texture != 0)
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
			const int sourceMinX = cachedKey.selectionActive ? std::clamp(cachedKey.selectionMinX, 0, static_cast<int>(planeData->imageWidth) - 1) : 0;
			const int sourceMinY = cachedKey.selectionActive ? std::clamp(cachedKey.selectionMinY, 0, static_cast<int>(planeData->imageHeight) - 1) : 0;
			const int sourceMaxX = cachedKey.selectionActive ? std::clamp(cachedKey.selectionMaxX, sourceMinX, static_cast<int>(planeData->imageWidth) - 1) : static_cast<int>(planeData->imageWidth) - 1;
			const int sourceMaxY = cachedKey.selectionActive ? std::clamp(cachedKey.selectionMaxY, sourceMinY, static_cast<int>(planeData->imageHeight) - 1) : static_cast<int>(planeData->imageHeight) - 1;
			const int sourceWidth = std::max(1, sourceMaxX - sourceMinX + 1);
			const float normalizedX = std::clamp((mousePos.x - plotMin.x) / plotWidth, 0.0f, 1.0f);
			const float normalizedY = std::clamp((plotMax.y - mousePos.y) / plotHeight, 0.0f, 1.0f);
			const int sourceX = sourceMinX + static_cast<int>(std::round(normalizedX * static_cast<float>(sourceWidth - 1)));
			const float targetLogValue = cachedMinLogValue + normalizedY * std::max(EPSILON, cachedMaxLogValue - cachedMinLogValue);

			hoverInfo.active = true;
			hoverInfo.paintMode = cachedKey.paintMode;
			hoverInfo.sourceX = std::clamp(sourceX, sourceMinX, sourceMaxX);
			hoverInfo.sourceMinY = sourceMinY;
			hoverInfo.sourceMaxY = sourceMaxY;
			hoverInfo.minLogValue = cachedMinLogValue;
			hoverInfo.targetLogValue = targetLogValue;
		}
	}

	if (valid && sampleOverlayInfo != nullptr && sampleOverlayInfo->active && planeData != nullptr && planeData->imageWidth > 0)
	{
		const int sourceMinX = cachedKey.selectionActive ? std::clamp(cachedKey.selectionMinX, 0, static_cast<int>(planeData->imageWidth) - 1) : 0;
		const int sourceMaxX = cachedKey.selectionActive ? std::clamp(cachedKey.selectionMaxX, sourceMinX, static_cast<int>(planeData->imageWidth) - 1) : static_cast<int>(planeData->imageWidth) - 1;
		if (sampleOverlayInfo->sourceX >= sourceMinX && sampleOverlayInfo->sourceX <= sourceMaxX)
		{
			const float normalizedX = (sourceMaxX > sourceMinX)
				? static_cast<float>(sampleOverlayInfo->sourceX - sourceMinX) / static_cast<float>(sourceMaxX - sourceMinX)
				: 0.0f;
			const float plotX = plotMin.x + normalizedX * plotWidth;
			const float logSpan = std::max(EPSILON, cachedMaxLogValue - cachedMinLogValue);
			const bool showRgbOverlay = cachedKey.paintMode == PaintMode::Rgb && sampleOverlayInfo->isRgb && sampleOverlayInfo->channelCount >= 3;

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
					if (cachedKey.paintMode == PaintMode::Red)
						verticalColor = IM_COL32(255, 96, 96, 220);
					else if (cachedKey.paintMode == PaintMode::Green)
						verticalColor = IM_COL32(96, 255, 96, 220);
					else if (cachedKey.paintMode == PaintMode::Blue)
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
				float value = ImageChannelUtils::computeBt709Luma(sampleOverlayInfo->values[0], sampleOverlayInfo->values[1], sampleOverlayInfo->values[2]);
				if (cachedKey.paintMode == PaintMode::Red)
					value = sampleOverlayInfo->values[0];
				else if (cachedKey.paintMode == PaintMode::Green)
					value = sampleOverlayInfo->values[1];
				else if (cachedKey.paintMode == PaintMode::Blue)
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
