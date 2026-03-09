#pragma once

#include <GL/glew.h>
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

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
 * @file imagePlane.h
 * @brief Data models for scanned image planes and per-mip payloads.
 */

/**
 * @brief Plane payload for one subimage/mip/channel-group slice.
 *
 * @details
 * Why this struct exists:
 * - Combines file metadata, CPU buffers, async state, and GL handle for one mip payload.
 * - Avoids scattering per-mip state across separate containers during load/render handoff.
 *
 * Where it is used:
 * - Created and initialized by `NoPlayer::scanImageFile()`.
 * - Loaded by `NoPlayer::loader()` via `load()`.
 * - Uploaded and sampled in `NoPlayer::draw()` via `generateGlTexture()` and inspection.
 */
struct ImagePlaneData
{
	/**
	 * @brief Load pixels into the packed CPU-side upload buffer.
	 *
	 * Called by the background loader worker to prepare data for inspection and upload.
	 *
	 * @return `true` when loading succeeds.
	 */
	bool load();

	/**
	 * @brief Upload loaded pixels to an OpenGL texture object.
	 *
	 * Called by the render thread when a loaded plane reaches the texture queue.
	 *
	 * @return `true` when texture creation and upload succeed.
	 */
	bool generateGlTexture();

	/**
	 * @brief Compute per-channel min/max across the plane.
	 *
	 * Used by the range-fit shortcut in `NoPlayer::draw`.
	 *
	 * @param minimum Output array with one minimum value per channel.
	 * @param maximum Output array with one maximum value per channel.
	 */
	void getRange(float* minimum, float* maximum);

	/**
	 * @brief Read one loaded pixel from the packed channel buffer.
	 *
	 * @param x Data-window X coordinate.
	 * @param y Data-window Y coordinate.
	 * @param values Output channel values.
	 * @param maxValues Maximum number of channels to copy.
	 * @return `true` when the sample could be read.
	 */
	bool samplePixel(int x, int y, float* values, int maxValues) const;

	/**
	 * @brief Compute or reuse the cached average for a rectangular selection.
	 *
	 * @param minX Data-window min X.
	 * @param minY Data-window min Y.
	 * @param maxX Data-window max X.
	 * @param maxY Data-window max Y.
	 * @param values Output average values.
	 * @param maxValues Maximum number of channels to copy.
	 * @param sampledPixels Output number of averaged pixels.
	 * @return `true` when the average could be computed.
	 */
	bool getSelectionAverage(int minX, int minY, int maxX, int maxY, float* values, int maxValues, int& sampledPixels);

	/** Path to the source image on disk. Used by `load`. */
	std::string imageFileName;
	/** Subimage index used by OIIO reads. Set by file scan and used by `load`. */
	unsigned int subimage = 0;
	/** Mip index used by OIIO reads. Set by file scan and used by `load`. */
	unsigned int mip = 0;

	/** Logical plane name from metadata. Used for AOV/UI labels. */
	std::string name;
	/** Optional group/family token from channel naming. Used for AOV/UI labels. */
	std::string groupName;
	/** Packed channel label string (e.g. `RGBA`). Used by draw and inspector UI. */
	std::string channels;
	/** Channel storage format label from file metadata. Used in UI. */
	std::string format;

	/** Compression method label from metadata. Used in info UI. */
	std::string compression;
	/** Compression quality metadata value when present. Used in info UI. */
	int quality = -1;
	/** Tile width when image is tiled. Used in info UI. */
	int tile_width = 0;
	/** Tile height when image is tiled. Used in info UI. */
	int tile_height = 0;

	/** Data-window width in pixels. */
	unsigned int imageWidth = 0;
	/** Data-window height in pixels. */
	unsigned int imageHeight = 0;
	/** Data-window X offset. */
	int imageOffsetX = 0;
	/** Data-window Y offset. */
	int imageOffsetY = 0;

	/** Display-window width in pixels. */
	unsigned int windowWidth = 0;
	/** Display-window height in pixels. */
	unsigned int windowHeight = 0;
	/** Display-window X offset. */
	int windowOffsetX = 0;
	/** Display-window Y offset. */
	int windowOffsetY = 0;

	/** Whether data and display windows are identical. Used in UI and coordinate mapping. */
	bool windowMatchData = true;
	/** Pixel aspect ratio from file metadata. Used in viewport transform math. */
	float pixelAspect = 1.0f;

	/** First channel index in the original OIIO spec. Used for channel fetches during load. */
	int begin = 0;
	/** Number of channels in this plane. */
	int len = 0;

	/** Packed pixel storage used for GL upload. Produced by `load`, consumed by `generateGlTexture`. */
	std::shared_ptr<precision[]> pixels;
	/** GL texture handle for this plane/mip once uploaded. Used by render path. */
	GLuint glTexture = 0;

	/**
	 * @brief Async state used by `NoPlayer` loader and render queues.
	 */
	enum state
	{
		NOT_ISSUED,
		ISSUED,
		LOADING_STARTED,
		LOADED,
		TEXTURE_GENERATED,
	};

	/** Current async state for this plane payload. */
	state ready = NOT_ISSUED;
	/** Shared image cache injected by `NoPlayer` for efficient I/O reuse. */
	std::shared_ptr<OIIO::ImageCache> cache;

	/** Cached per-channel min/max values for the loaded packed pixel buffer. */
	bool rangeCacheValid = false;
	std::array<float, 4> cachedMinimum = {0.0f, 0.0f, 0.0f, 0.0f};
	std::array<float, 4> cachedMaximum = {0.0f, 0.0f, 0.0f, 0.0f};

	/** Cached region-average result for the inspector selection overlay. */
	bool selectionAverageValid = false;
	int selectionAverageMinX = 0;
	int selectionAverageMinY = 0;
	int selectionAverageMaxX = 0;
	int selectionAverageMaxY = 0;
	int selectionAverageSampledPixels = 0;
	std::array<float, 4> selectionAverageValues = {0.0f, 0.0f, 0.0f, 0.0f};
};

/**
 * @brief Logical AOV plane with all mips for the same channel grouping.
 *
 * @details
 * Why this struct exists:
 * - Groups all mip payloads that represent one logical plane in the viewer UI.
 * - Stores per-plane view controls (gain/offset/ocio/debug) once, independent of mip level.
 *
 * Where it is used:
 * - Built by `NoPlayer::scanImageFile()` when channel groups are discovered.
 * - Consumed by `NoPlayer::draw()` for AOV selection, controls, and rendering.
 */
struct ImagePlane
{
	/** Plane label shown in the AOV panel. */
	std::string name;
	/** Optional group token shown in the AOV panel. */
	std::string groupName;
	/** Channel label string for display and solo controls. */
	std::string channels;
	/** Number of mips available for this logical plane. */
	int nMIPs = 0;

	/** Gain applied in fragment shader for visual exposure control. */
	float gainValues = 1.0f;
	/** Offset applied in fragment shader for visual black-point control. */
	float offsetValues = 0.0f;
	/** Enables OCIO display transform for this plane in shader. */
	bool doOCIO = false;
	/** Enables NaN/Inf highlight mode in shader. */
	bool checkNaN = true;

	/** Per-mip payload entries for this logical plane. */
	std::vector<ImagePlaneData> MIPs;
};
