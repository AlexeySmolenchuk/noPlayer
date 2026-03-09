#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ImageChannelUtils
{
constexpr float Bt709LumaRed = 0.2126f;
constexpr float Bt709LumaGreen = 0.7152f;
constexpr float Bt709LumaBlue = 0.0722f;

inline bool isRgbChannels(std::string_view channels)
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

inline float computeBt709Luma(float red, float green, float blue)
{
	return Bt709LumaRed * red + Bt709LumaGreen * green + Bt709LumaBlue * blue;
}

inline float toLog2OrFloor(float value, float minLogValue, float epsilon)
{
	if (!std::isfinite(value) || value <= epsilon)
		return minLogValue;
	return std::log2(value);
}
}
