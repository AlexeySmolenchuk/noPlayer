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

float computeLuminance(float red, float green, float blue)
{
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
							   PaintMode paintMode) const
{
	return valid
		&& cachedPlaneIdx == planeIdx
		&& cachedMipIdx == mipIdx
		&& cachedGeneration == generation
		&& cachedPlotWidth == plotWidth
		&& cachedPlotHeight == plotHeight
		&& cachedPaintMode == paintMode;
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

	if (!task.pixels || task.imageWidth <= 0 || task.imageHeight <= 0 || task.channelCount <= 0)
		return result;

	const bool showRgbChannels = task.isRgb && task.paintMode == PaintMode::Rgb;
	const bool showSingleChannel = task.isRgb && isSingleChannelMode(task.paintMode);
	const int singleChannel = singleChannelIndex(task.paintMode);
	const int scopeWidth = result.plotWidth;
	const int scopeHeight = result.plotHeight;
	const size_t histogramSize = static_cast<size_t>(scopeWidth) * static_cast<size_t>(scopeHeight);

	std::vector<std::uint32_t> histogramRed(histogramSize, 0u);
	std::vector<std::uint32_t> histogramGreen(histogramSize, 0u);
	std::vector<std::uint32_t> histogramBlue(histogramSize, 0u);
	std::vector<std::uint32_t> histogramWhite(histogramSize, 0u);
	std::vector<int> xBins(static_cast<size_t>(task.imageWidth), 0);

	for (int x = 0; x < task.imageWidth; x++)
	{
		if (scopeWidth <= 1 || task.imageWidth <= 1)
		{
			xBins[static_cast<size_t>(x)] = 0;
			continue;
		}

		const long long numerator = static_cast<long long>(x) * static_cast<long long>(scopeWidth - 1);
		xBins[static_cast<size_t>(x)] = static_cast<int>(numerator / static_cast<long long>(task.imageWidth - 1));
	}

	const precision* pixels = task.pixels.get();
	float minValue = std::numeric_limits<float>::max();
	float maxValue = std::numeric_limits<float>::lowest();
	float minPositiveValue = std::numeric_limits<float>::max();
	float maxPositiveValue = std::numeric_limits<float>::lowest();

	for (int y = 0; y < task.imageHeight; y++)
	{
		for (int x = 0; x < task.imageWidth; x++)
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
					value = computeLuminance(red, green, blue);
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

	for (int y = 0; y < task.imageHeight; y++)
	{
		for (int x = 0; x < task.imageWidth; x++)
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

					const float normalized = std::clamp((toLogValue(value, minLogValue) - minLogValue) / logValueRange, 0.0f, 1.0f);
					const int binY = std::clamp(static_cast<int>(std::round(normalized * static_cast<float>(lastScopeRow))),
												0, lastScopeRow);
					const int binX = xBins[static_cast<size_t>(x)];
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
				const int binX = xBins[static_cast<size_t>(x)];
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
					value = computeLuminance(red, green, blue);
				}

				if (!std::isfinite(value))
					continue;

				const float normalized = std::clamp((toLogValue(value, minLogValue) - minLogValue) / logValueRange, 0.0f, 1.0f);
				const int binY = std::clamp(static_cast<int>(std::round(normalized * static_cast<float>(lastScopeRow))),
											0, lastScopeRow);
				const int binX = xBins[static_cast<size_t>(x)];
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
			|| task.paintMode != requestedPaintMode)
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
								 int plotHeight)
{
	if (!planeData.pixels || planeData.imageWidth == 0 || planeData.imageHeight == 0 || planeData.len <= 0)
		return;

	if (valid && (cachedPlaneIdx != planeIdx
		|| cachedMipIdx != mipIdx
		|| cachedGeneration != generation
		|| cachedPaintMode != currentPaintMode))
	{
		valid = false;
		releaseGlResources();
	}

	if (isValidFor(planeIdx, mipIdx, generation, plotWidth, plotHeight, currentPaintMode))
		return;

	BuildTask task;
	task.pixels = planeData.pixels;
	task.imageWidth = static_cast<int>(planeData.imageWidth);
	task.imageHeight = static_cast<int>(planeData.imageHeight);
	task.channelCount = planeData.len;
	task.isRgb = isRgbChannels(planeData.channels) && planeData.len >= 3;
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
			&& requestedPaintMode == currentPaintMode)
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
			|| readyResult.paintMode != requestedPaintMode)
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
}


void WaveformPanel::draw(int panelWidth,
						 int panelHeight,
						 float unit,
						 bool sourceReady,
						 const ImagePlaneData* planeData,
						 int planeIdx,
						 int mipIdx,
						 std::uint64_t generation)
{
	(void)unit;

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(static_cast<float>(panelWidth), static_cast<float>(panelHeight)), ImGuiCond_Always);
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove
								| ImGuiWindowFlags_NoResize
								| ImGuiWindowFlags_NoCollapse
								| ImGuiWindowFlags_NoSavedSettings;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.07f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
	ImGui::Begin("Waveform", nullptr, windowFlags);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Paint mode:");
		ImGui::SameLine();
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

		ImGui::Separator();

		const float tabsHeight = ImGui::GetFrameHeightWithSpacing() + 2.0f;
		const float axisWidth = 44.0f;
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 plotButtonSize(std::max(1.0f, available.x), std::max(80.0f, available.y - tabsHeight));
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
			requestBuild(*planeData, planeIdx, mipIdx, generation, plotTextureWidth, plotTextureHeight);
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
			const char* statusMsg = sourceReady ? "Building waveform..." : "Loading waveform...";
			const ImVec2 statusSize = ImGui::CalcTextSize(statusMsg);
			drawList->AddText(ImVec2(plotMin.x + (plotWidth - statusSize.x) * 0.5f,
									plotMin.y + (plotHeight - statusSize.y) * 0.5f),
							IM_COL32(170, 170, 170, 255), statusMsg);
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

		if (valid)
			ImGui::Text("Range %.3f .. %.3f  Log", cachedMinValue, cachedMaxValue);
		else
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));

		ImGui::Dummy(ImVec2(0.0f, 4.0f));
		ImGui::Selectable("Waveform", true, 0, ImVec2(0.0f, 0.0f));
		ImGui::SameLine();
		ImGui::BeginDisabled();
		ImGui::Selectable("RGB Parade", false, 0, ImVec2(0.0f, 0.0f));
		ImGui::EndDisabled();
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(2);
}


const char* WaveformPanel::paintModeLabel(PaintMode mode)
{
	switch (mode)
	{
	case PaintMode::White:
		return "White";
	case PaintMode::Rgb:
		return "RGB";
	case PaintMode::Red:
		return "R";
	case PaintMode::Green:
		return "G";
	case PaintMode::Blue:
		return "B";
	}
	return "White";
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
