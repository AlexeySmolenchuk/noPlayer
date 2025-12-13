
#include "imagePlane.h"
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <float.h>

void ImagePlaneData::load()
{
	if (ready != ISSUED)
		return;

	ready = LOADING_STARTED;

	using namespace OIIO;

	float missing[4] = { 0.0, 0.0, 0.0, 0.0 };
	OIIO::attribute ("missingcolor", TypeDesc("float[4]"), &missing);

	auto inp = ImageInput::open (imageFileName);
	if (! inp)
	{
		std::cerr << "Could not open " << imageFileName
				<< ", error = " << OIIO::geterror() << "\n";
		return;
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

	ready = LOADED;
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
		minimum[i] = FLT_MAX;
		maximum[i] = FLT_MIN;
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


void ImagePlaneData::generateGlTexture()
{
	if (ready != LOADED)
		return;

	glPixelStorei(GL_PACK_ALIGNMENT, 2);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

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
	ready = TEXTURE_GENERATED;
}
