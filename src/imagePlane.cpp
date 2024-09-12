
#include "imagePlane.h"

void ImagePlaneData::load()
{
	using namespace OIIO;

	auto inp = ImageInput::open (imageFileName);
	if (! inp)
	{
		std::cerr << "Could not open " << imageFileName
				<< ", error = " << OIIO::geterror() << "\n";
		return;
	}

	pixels = new precision[ imageWidth
							* imageHeight
							* len];

	if (! inp->read_image( subimage,
							mip,
							begin,
							begin + len,
							TypeDesc::PRECISION,
							&pixels[0]))
	{
		std::cerr << "Could not read pixels from " << imageFileName
				<< ", error = " << inp->geterror() << "\n";
		return;
	}
	if (! inp->close ())
	{
		std::cerr << "Error closing " << imageFileName
				<< ", error = " << inp->geterror() << "\n";
		return;
	}

	ready = 2;
}


void ImagePlaneData::generateGlTexture()
{
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

}
