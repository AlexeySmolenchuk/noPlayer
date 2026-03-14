
#include "imagePlane.h"
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <limits>

bool ImagePlaneData::load()
{
	using namespace OIIO;

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

	buffer = ImageBuf(imageFileName, subimage, mip, cache);

	// Preload first imagePlane
	if (begin==0)
	{
		bool ok = buffer.read(subimage, mip, begin, begin + len, true, TypeDesc::PRECISION);
		if (!ok)
			std::cout << buffer.geterror() << std::endl;
	}


	ROI roi = buffer.spec().roi();
	roi.chbegin = begin;
	roi.chend = begin + len;
	
	bool ok = buffer.get_pixels(roi, TypeDesc::PRECISION, &pixels[0]);
	if (!ok)
		std::cout << buffer.geterror() << std::endl;

	// cache->get_pixels(ustring(imageFileName), subimage, mip, 0, imageWidth, 0, imageHeight, 0, 1, begin, begin + len, TypeDesc::PRECISION, &pixels[0]);

	// if (! inp->read_image( subimage,
	// 						mip,
	// 						begin,
	// 						begin + len,
	// 						TypeDesc::PRECISION,
	// 						&pixels[0]))
	// {
	// 	std::cerr << "Could not read pixels from " << imageFileName
	// 			<< ", error = " << inp->geterror() << "\n";
	// 	return;
	// }
	// if (! inp->close ())
	// {
	// 	std::cerr << "Error closing " << imageFileName
	// 			<< ", error = " << inp->geterror() << "\n";
	// 	return;
	// }

	return ok;
}


template<typename BUFT>
static void getRange_impl(ImagePlaneData *plane, float *minimum, float *maximum)
{
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


template<typename BUFT>
static void getAverage_impl(ImagePlaneData *plane, const int (&coords)[4])
{
	plane->pixelAverage[0] = 0;
	plane->pixelAverage[1] = 0;
	plane->pixelAverage[2] = 0;
	plane->pixelAverage[3] = 0;

	OIIO::ROI roi = plane->buffer.roi();
	roi.xbegin = std::min(coords[0],coords[2]);
	roi.xend   = std::max(coords[0],coords[2]);
	roi.ybegin = std::min(coords[1],coords[3]);
	roi.yend   = std::max(coords[1],coords[3]);

	// Iterate over all pixels in the region...
	for (OIIO::ImageBuf::ConstIterator<BUFT> it(plane->buffer, roi); !it.done(); ++it)
	{
		if (! it.exists())   // Make sure the iterator is pointing
			continue;        //   to a pixel in the data window

		int i = 0;
		for (int c = plane->begin; c < plane->begin+plane->len;  ++c)
		{
			plane->pixelAverage[i] += float(it[c]);
			i++;
		}
	}

	int i = 0;
	for (int c = plane->begin; c < plane->begin+plane->len;  ++c)
	{
		plane->pixelAverage[i] /= float(roi.npixels());
		i++;
	}
}


void ImagePlaneData::getAverage(const int (&coords)[4])
{
	float missing[4] = { 0.0, 0.0, 0.0, 0.0 };
	OIIO::attribute ("missingcolor", OIIO::TypeDesc("float[4]"), &missing);

	if (buffer.spec().format == OIIO::TypeDesc::FLOAT)
		getAverage_impl<float> (this, coords);
	else if (buffer.spec().format == OIIO::TypeDesc::HALF)
		getAverage_impl<half> (this, coords);
	else if (buffer.spec().format == OIIO::TypeDesc::UINT8)
		getAverage_impl<unsigned char> (this, coords);
	else if (buffer.spec().format == OIIO::TypeDesc::UINT16)
		getAverage_impl<unsigned short> (this, coords);

	averageIsValid = true;
}


bool ImagePlaneData::generateGlTexture()
{
	if (len < 1 || len > 4 || pixels == nullptr)
	{
		return false;
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 2);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

	if (glTexture != 0)
		glDeleteTextures(1, &glTexture);

	// Create an OpenGL texture identifier
	glGenTextures(1, &glTexture);
	glBindTexture(GL_TEXTURE_2D, glTexture);

	// Setup filtering parameters for display
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	int dataFormats[] = {0, GL_RED, GL_RG, GL_RGB, GL_RGBA};

	glTexImage2D(GL_TEXTURE_2D, 0, internalFormats[len],
					imageWidth, imageHeight, 0,
					dataFormats[len], PRECISION_GL,
					&pixels[0]);

	// std::cout << glGetError() << std::endl;	
	return true;
}
