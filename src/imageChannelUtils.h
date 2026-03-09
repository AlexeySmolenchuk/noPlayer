#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ImageChannelUtils
{
constexpr float Bt709LumaRed = 0.2126f;
constexpr float Bt709LumaGreen = 0.7152f;
constexpr float Bt709LumaBlue = 0.0722f;

enum class WaveformColorInterpretation
{
	Mono = 0,
	Rgb,
	Xyz,
	ScalarReplicated,
};

inline bool matchesChannels(std::string_view channels, char first, char second, char third)
{
	if (channels.size() < 3)
		return false;

	auto isChannel = [](char value, char expected)
	{
		return value == expected || value == static_cast<char>(expected - 'a' + 'A');
	};

	return isChannel(channels[0], first)
		&& isChannel(channels[1], second)
		&& isChannel(channels[2], third);
}

inline bool isChannelChar(char value, char expected)
{
	return value == expected || value == static_cast<char>(expected - 'a' + 'A');
}

inline int findChannelIndex(std::string_view channels, char expected)
{
	for (size_t index = 0; index < channels.size(); index++)
	{
		if (isChannelChar(channels[index], expected))
			return static_cast<int>(index);
	}

	return -1;
}

inline bool isRgbChannels(std::string_view channels)
{
	return matchesChannels(channels, 'r', 'g', 'b');
}

inline bool isXyzChannels(std::string_view channels)
{
	return matchesChannels(channels, 'x', 'y', 'z');
}

inline WaveformColorInterpretation waveformColorInterpretation(std::string_view channels, int channelCount)
{
	if (channelCount >= 3)
	{
		if (isRgbChannels(channels))
			return WaveformColorInterpretation::Rgb;
		if (isXyzChannels(channels))
			return WaveformColorInterpretation::Xyz;
	}

	if (channelCount == 1)
		return WaveformColorInterpretation::ScalarReplicated;

	return WaveformColorInterpretation::Mono;
}

inline bool hasWaveformRgbTriplet(WaveformColorInterpretation interpretation)
{
	return interpretation != WaveformColorInterpretation::Mono;
}

inline bool isReplicatedWaveformRgb(WaveformColorInterpretation interpretation)
{
	return interpretation == WaveformColorInterpretation::ScalarReplicated;
}

inline float computeBt709Luma(float red, float green, float blue)
{
	return Bt709LumaRed * red + Bt709LumaGreen * green + Bt709LumaBlue * blue;
}

inline void xyzToLinearRgb(float x, float y, float z, float& red, float& green, float& blue)
{
	red = (3.2406f * x) + (-1.5372f * y) + (-0.4986f * z);
	green = (-0.9689f * x) + (1.8758f * y) + (0.0415f * z);
	blue = (0.0557f * x) + (-0.2040f * y) + (1.0570f * z);
}

template <typename T>
inline bool sampleWaveformRgbTriplet(const T* values,
									 WaveformColorInterpretation interpretation,
									 float& red,
									 float& green,
									 float& blue)
{
	if (values == nullptr)
		return false;

	switch (interpretation)
	{
	case WaveformColorInterpretation::Rgb:
		red = static_cast<float>(values[0]);
		green = static_cast<float>(values[1]);
		blue = static_cast<float>(values[2]);
		break;
	case WaveformColorInterpretation::Xyz:
		xyzToLinearRgb(static_cast<float>(values[0]),
					   static_cast<float>(values[1]),
					   static_cast<float>(values[2]),
					   red,
					   green,
					   blue);
		break;
	case WaveformColorInterpretation::ScalarReplicated:
		red = static_cast<float>(values[0]);
		green = red;
		blue = red;
		break;
	case WaveformColorInterpretation::Mono:
	default:
		return false;
	}

	return std::isfinite(red) && std::isfinite(green) && std::isfinite(blue);
}

template <typename T>
inline bool sampleWaveformRgbChannel(const T* values,
									 WaveformColorInterpretation interpretation,
									 int channelIndex,
									 float& value)
{
	if (values == nullptr || channelIndex < 0 || channelIndex > 2)
		return false;

	switch (interpretation)
	{
	case WaveformColorInterpretation::Rgb:
		value = static_cast<float>(values[channelIndex]);
		return std::isfinite(value);
	case WaveformColorInterpretation::Xyz:
	case WaveformColorInterpretation::ScalarReplicated:
	{
		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;
		if (!sampleWaveformRgbTriplet(values, interpretation, red, green, blue))
			return false;

		value = (channelIndex == 0) ? red : (channelIndex == 1 ? green : blue);
		return true;
	}
	case WaveformColorInterpretation::Mono:
	default:
		return false;
	}
}

template <typename T>
inline bool sampleWaveformLuma(const T* values,
							   WaveformColorInterpretation interpretation,
							   float& value)
{
	if (values == nullptr)
		return false;

	float red = 0.0f;
	float green = 0.0f;
	float blue = 0.0f;
	if (!sampleWaveformRgbTriplet(values, interpretation, red, green, blue))
		return false;

	value = computeBt709Luma(red, green, blue);
	return std::isfinite(value);
}

inline float toLog2OrFloor(float value, float minLogValue, float epsilon)
{
	if (!std::isfinite(value) || value <= epsilon)
		return minLogValue;
	return std::log2(value);
}
}
