
#include "imagePlane.h"
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <limits>

bool ImagePlaneData::load()
{
	using namespace OIIO;

	// Define fallback pixel value for missing tiles/regions.
	float missing[4] = { 0.0, 0.0, 0.0, 0.0 };
	OIIO::attribute ("missingcolor", TypeDesc("float[4]"), &missing);

	auto inp = ImageInput::open (imageFileName);
	if (! inp)
	{
		std::cerr << "Could not open " << imageFileName
				<< ", error = " << OIIO::geterror() << "\n";
		return false;
	}

	pixels = std::shared_ptr<precision[]>(new precision[imageWidth * imageHeight * len]);

	// Keep an ImageBuf for raw-value inspection and range calculations.
	buffer = ImageBuf(imageFileName, subimage, mip, cache);

	if (begin==0)
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


template<typename BUFT>
static void getRange_impl(ImagePlaneData *plane, float *minimum, float *maximum)
{
	// Walk source pixels and update per-channel min/max values.
	for (OIIO::ImageBuf::ConstIterator<BUFT> it(plane->buffer); !it.done(); ++it)
	{
		int i = 0;
		for (int c = plane->begin; c < plane->begin+plane->len;  ++c)
		{
			minimum[i] = std::min(minimum[i], float(it[c]));
			maximum[i] = std::max(maximum[i], float(it[c]));
			i++;
		}
	}
}


void ImagePlaneData::getRange(float *minimum, float *maximum)
{
	// Initialize range accumulators before scanning the image.
	for (int i = 0; i < len; i++)
	{
		minimum[i] = std::numeric_limits<float>::max();
		maximum[i] = std::numeric_limits<float>::lowest();
	}

	float missing[4] = { 0.0, 0.0, 0.0, 0.0 };
	OIIO::attribute ("missingcolor", OIIO::TypeDesc("float[4]"), &missing);

    if (buffer.spec().format == OIIO::TypeDesc::FLOAT)
        getRange_impl<float> (this, minimum, maximum);
    else if (buffer.spec().format == OIIO::TypeDesc::HALF)
        getRange_impl<half> (this, minimum, maximum);
    else if (buffer.spec().format == OIIO::TypeDesc::UINT8)
        getRange_impl<unsigned char> (this, minimum, maximum);
    else if (buffer.spec().format == OIIO::TypeDesc::UINT16)
        getRange_impl<unsigned short> (this, minimum, maximum);

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
