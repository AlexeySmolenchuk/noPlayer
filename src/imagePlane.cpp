
#include "imagePlane.h"

#include <OpenImageIO/imagebuf.h>

#include <algorithm>
#include <limits>

bool ImagePlaneData::load()
{
	using namespace OIIO;

	pixels = std::shared_ptr<precision[]>(new precision[imageWidth * imageHeight * len]);
	rangeCacheValid = false;
	selectionAverageValid = false;

	ImageBuf buffer(imageFileName, subimage, mip, cache);
	if (!buffer.initialized())
	{
		std::cerr << "Could not open " << imageFileName
				<< ", error = " << buffer.geterror() << "\n";
		return false;
	}

	if (begin == 0)
	{
		// Preload the first channel group so ImageBuf has source-precision data.
		bool ok = buffer.read(subimage, mip, begin, begin + len, true, TypeDesc::UNKNOWN);
		if (!ok)
			std::cout << buffer.geterror() << std::endl;
	}

	ROI roi = buffer.spec().roi();
	roi.chbegin = begin;
	roi.chend = begin + len;

	// Extract the selected channels into a contiguous buffer for GL upload.
	bool ok = buffer.get_pixels(roi, TypeDesc::PRECISION, &pixels[0]);
	if (!ok)
		std::cout << buffer.geterror() << std::endl;

	return ok;
}


void ImagePlaneData::getRange(float* minimum, float* maximum)
{
	if (rangeCacheValid)
	{
		for (int i = 0; i < len; i++)
		{
			minimum[i] = cachedMinimum[static_cast<size_t>(i)];
			maximum[i] = cachedMaximum[static_cast<size_t>(i)];
		}
		return;
	}

	// Initialize range accumulators before scanning the image.
	for (int i = 0; i < len; i++)
	{
		minimum[i] = std::numeric_limits<float>::max();
		maximum[i] = std::numeric_limits<float>::lowest();
	}

	if (!pixels || imageWidth == 0 || imageHeight == 0 || len <= 0)
		return;

	const precision* sourcePixels = pixels.get();
	const size_t pixelCount = static_cast<size_t>(imageWidth) * static_cast<size_t>(imageHeight);
	for (size_t pixelIndex = 0; pixelIndex < pixelCount; pixelIndex++)
	{
		const size_t base = pixelIndex * static_cast<size_t>(len);
		for (int channel = 0; channel < len; channel++)
		{
			const float value = static_cast<float>(sourcePixels[base + static_cast<size_t>(channel)]);
			minimum[channel] = std::min(minimum[channel], value);
			maximum[channel] = std::max(maximum[channel], value);
		}
	}

	for (int i = 0; i < len; i++)
	{
		cachedMinimum[static_cast<size_t>(i)] = minimum[i];
		cachedMaximum[static_cast<size_t>(i)] = maximum[i];
	}
	rangeCacheValid = true;
}


bool ImagePlaneData::samplePixel(int x, int y, float* values, int maxValues) const
{
	if (!pixels || values == nullptr || maxValues <= 0 || len <= 0)
		return false;

	if (x < 0 || y < 0 || x >= static_cast<int>(imageWidth) || y >= static_cast<int>(imageHeight))
		return false;

	const int channelCount = std::min(len, maxValues);
	const size_t pixelOffset = (static_cast<size_t>(y) * static_cast<size_t>(imageWidth)
							 + static_cast<size_t>(x)) * static_cast<size_t>(len);
	const precision* sourcePixels = pixels.get();
	for (int channel = 0; channel < channelCount; channel++)
		values[channel] = static_cast<float>(sourcePixels[pixelOffset + static_cast<size_t>(channel)]);
	return true;
}


bool ImagePlaneData::getSelectionAverage(int minX, int minY, int maxX, int maxY, float* values, int maxValues, int& sampledPixels)
{
	sampledPixels = 0;
	if (!pixels || values == nullptr || maxValues <= 0 || len <= 0 || imageWidth == 0 || imageHeight == 0)
		return false;

	minX = std::clamp(minX, 0, static_cast<int>(imageWidth) - 1);
	minY = std::clamp(minY, 0, static_cast<int>(imageHeight) - 1);
	maxX = std::clamp(maxX, minX, static_cast<int>(imageWidth) - 1);
	maxY = std::clamp(maxY, minY, static_cast<int>(imageHeight) - 1);

	const int channelCount = std::min(len, maxValues);
	if (selectionAverageValid
		&& selectionAverageMinX == minX
		&& selectionAverageMinY == minY
		&& selectionAverageMaxX == maxX
		&& selectionAverageMaxY == maxY)
	{
		sampledPixels = selectionAverageSampledPixels;
		for (int channel = 0; channel < channelCount; channel++)
			values[channel] = selectionAverageValues[static_cast<size_t>(channel)];
		return true;
	}

	double sums[4] = {0.0, 0.0, 0.0, 0.0};
	const precision* sourcePixels = pixels.get();
	for (int y = minY; y <= maxY; y++)
	{
		for (int x = minX; x <= maxX; x++)
		{
			const size_t pixelOffset = (static_cast<size_t>(y) * static_cast<size_t>(imageWidth)
									 + static_cast<size_t>(x)) * static_cast<size_t>(len);
			for (int channel = 0; channel < channelCount; channel++)
				sums[channel] += static_cast<double>(sourcePixels[pixelOffset + static_cast<size_t>(channel)]);
		}
	}

	sampledPixels = (maxX - minX + 1) * (maxY - minY + 1);
	if (sampledPixels <= 0)
		return false;

	selectionAverageValid = true;
	selectionAverageMinX = minX;
	selectionAverageMinY = minY;
	selectionAverageMaxX = maxX;
	selectionAverageMaxY = maxY;
	selectionAverageSampledPixels = sampledPixels;
	for (int channel = 0; channel < channelCount; channel++)
	{
		const float average = static_cast<float>(sums[channel] / static_cast<double>(sampledPixels));
		values[channel] = average;
		selectionAverageValues[static_cast<size_t>(channel)] = average;
	}
	return true;
}


bool ImagePlaneData::generateGlTexture()
{
	if (len < 1 || len > 4 || pixels == nullptr)
	{
		return false;
	}

	// Configure alignment for half/float texture uploads.
	glPixelStorei(GL_PACK_ALIGNMENT, 2);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

	if (glTexture != 0)
		glDeleteTextures(1, &glTexture);

	// Create and configure a texture object for this plane payload.
	glGenTextures(1, &glTexture);
	glBindTexture(GL_TEXTURE_2D, glTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	int dataFormats[] = {0, GL_RED, GL_RG, GL_RGB, GL_RGBA};

	// Upload packed channel data using the chosen internal precision mode.
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormats[len],
					imageWidth, imageHeight, 0,
					dataFormats[len], PRECISION_GL,
					&pixels[0]);

	return true;
}
