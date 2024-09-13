#pragma once
#include <string>
#include <vector>
#include <GL/glew.h>
#include <OpenImageIO/imageio.h>

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

struct ImagePlaneData
{
    void load();
    void generateGlTexture();

    std::string imageFileName;
    unsigned int subimage;	// index for async loading
    unsigned int mip;		// index for async loading

    std::string name;		// name of sub-image from metadata
    std::string groupName;	// group name if exists
    std::string channels;	// channels names after dot concatenated for corresponding group
    std::string format;		// string representation of data type

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

    int begin; // index in oiio spec
    int len;
    precision *pixels;	// fill deferred
    GLuint glTexture;	// fill deferred
    int ready = 0;

    ~ImagePlaneData() { delete[] pixels;}
};

struct ImagePlane
{
    std::string name;		// name of sub-image from metadata
    std::string groupName;	// group name if exists
    std::string channels;	// channels names after dot concatenated for corresponding group
    int nMIPs;

    float gainValues = 1.0;
    float offsetValues = 0.0;
    bool doOCIO = false;    // guess from data type and channel naming

    std::vector<ImagePlaneData> MIPs;
};
