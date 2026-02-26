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


/**
 * @brief Image data and metadata for a single subimage/MIP/channel group.
 *
 * Instances are created during file scanning and transitioned through a loading
 * state machine as pixels are read and uploaded to OpenGL.
 */
struct ImagePlaneData
{
	/**
	 * @brief Load pixel data from disk into CPU memory.
	 *
	 * This updates @c ready from @c ISSUED to @c LOADING_STARTED and then to
	 * @c LOADED when successful.
	 */
	void load();

	/**
	 * @brief Upload loaded pixels to a GPU texture.
	 *
	 * On success, updates @c ready to @c TEXTURE_GENERATED.
	 */
	void generateGlTexture();

	/**
	 * @brief Compute channel-wise pixel range.
	 * @param minimum Output array for minimum values, one entry per channel.
	 * @param maximum Output array for maximum values, one entry per channel.
	 */
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

	/**
	 * @brief Loading and upload state for the plane.
	 */
	enum state
	{
		NOT_ISSUED,       ///< Plane has not been scheduled for load.
		ISSUED,           ///< Plane has been queued for background loading.
		LOADING_STARTED,  ///< Worker thread started reading pixel data.
		LOADED,           ///< Pixel data is available in CPU memory.
		TEXTURE_GENERATED, ///< OpenGL texture has been created and filled.
	};

	state ready = NOT_ISSUED;

	OIIO::ImageBuf buffer;
	std::shared_ptr<OIIO::ImageCache> cache;
};


/**
 * @brief Logical image plane shown in the viewer (AOV/group with MIPs).
 */
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
