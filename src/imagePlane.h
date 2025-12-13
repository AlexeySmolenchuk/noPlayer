#pragma once
#include <string>
#include <vector>
#include <GL/glew.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagecache.h>

// #define PRECISION_FLOAT

#ifdef PRECISION_FLOAT
	#define precision float
	#define PRECISION FLOAT
	#define PRECISION_GL GL_FLOAT
	static int internalFormats[] = {0, GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F};
#else
	#include <Imath/half.h>
	#define precision half
	#define PRECISION HALF
	#define PRECISION_GL GL_HALF_FLOAT
	static int internalFormats[] = {0, GL_R16F, GL_RG16F, GL_RGB16F, GL_RGBA16F};
#endif


// Represent image data and properties of certain level of MIP
struct ImagePlaneData
{
	void load();
	void generateGlTexture();
	void getRange(float*, float*);

	std::string imageFileName;
	unsigned int subimage;	// index for async loading
	unsigned int mip;		// index for async loading

	std::string name;		// name of sub-image from metadata
	std::string groupName;	// group name if exists
	std::string channels;	// channels names after dot concatenated for corresponding group
	std::string format;		// string representation of data type

	std::string compression;
	int quality;
	int tile_width;
	int tile_height;

	// Data Window
	unsigned int imageWidth;
	unsigned int imageHeight;
	int imageOffsetX;
	int imageOffsetY;

	// Display Window
	unsigned int windowWidth;
	unsigned int windowHeight;
	int windowOffsetX;
	int windowOffsetY;

	bool windowMatchData; // is Data Window match Display Window
	float pixelAspect;

	int begin; // index in oiio spec
	int len;
	std::shared_ptr<precision[]> pixels;	// fill deferred
	GLuint glTexture;	// fill deferred

	enum state
	{
		NOT_ISSUED,
		ISSUED,
		LOADING_STARTED,
		LOADED,
		TEXTURE_GENERATED,
	};

	state ready = NOT_ISSUED;

	OIIO::ImageBuf buffer;
	std::shared_ptr<OIIO::ImageCache> cache;
};


// Represent individual channel 'group' within multichannel or multipart image 
struct ImagePlane
{
	std::string name;		// name of sub-image from metadata
	std::string groupName;	// group name if exists
	std::string channels;	// channels names after dot concatenated for corresponding group
	int nMIPs;

	float gainValues = 1.0;
	float offsetValues = 0.0;
	bool doOCIO = false;    // guess from data type and channel naming
	bool checkNaN = true;

	std::vector<ImagePlaneData> MIPs; // Only one entry for non mip-mapped images
};
