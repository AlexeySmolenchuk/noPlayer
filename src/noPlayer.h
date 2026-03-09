#pragma once

#include "imagePlane.h"
#include "shaderUniforms.h"
#include "waveformPanel.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <OpenImageIO/imagecache.h>

#include <condition_variable>
#include <cstdint>
#include <queue>
#include <string>
#include <thread>

/**
 * @file noPlayer.h
 * @brief Runtime facade for the interactive viewer application.
 */

/**
 * @brief Interactive image viewer application entry object.
 *
 * @details
 * Why this class exists:
 * - Centralizes ownership of window, UI, rendering, OCIO, and async loading state.
 * - Keeps application entry points and callbacks focused on intent-level operations.
 *
 * Where it is used:
 * - `main` in `application.cpp` creates one instance and drives `init()` and `run()`.
 * - GLFW/event callbacks in `noPlayer.cpp` forward user actions to this facade.
 */
class NoPlayer
{
public:
	/**
	 * @brief Create window, OpenGL context, ImGui, OCIO shader state, and loader worker.
	 *
	 * Called once by `main`.
	 */
	NoPlayer();

	/**
	 * @brief Stop background work and release all GL/UI resources.
	 *
	 * Called when `main` exits.
	 */
	~NoPlayer();

	/**
	 * @brief Scan and queue one image file for loading.
	 *
	 * Called by startup path and drop/reload callbacks.
	 *
	 * @param fileName Image path.
	 * @param fresh When `true`, reset view/selection state for a new file.
	 */
	void init(const char* fileName, bool fresh = true);

	/**
	 * @brief Run the event and render loop until window close.
	 *
	 * Called by `main`.
	 */
	void run();

	/**
	 * @brief Draw one frame of UI and image output.
	 *
	 * Called from `run` and framebuffer callback.
	 */
	void draw();

	/**
	 * @brief Refresh ImGui font scale from the current monitor content scale.
	 *
	 * Called during startup and window-move/resize callbacks.
	 */
	void updateFontScale();

	/**
	 * @brief Set active channel-solo mode.
	 *
	 * Called by keyboard shortcuts.
	 *
	 * @param idx `0` for combined view, `1..8` for channel/derived solo modes.
	 */
	void setChannelSoloing(int idx) { channelSoloing = idx; }

	/**
	 * @brief Clear current file state and wait for in-flight loader work.
	 *
	 * Called before opening another image.
	 */
	void clear();

	/**
	 * @brief Current image path for reload and UI display.
	 *
	 * Called by keyboard callback.
	 *
	 * @return Current image filename or empty string.
	 */
	std::string getFileName() { return imageFileName; }

private:
	/**
	 * @brief Build logical plane/mip model from file metadata.
	 *
	 * Called by `init`.
	 *
	 * @return `true` when file scan succeeds.
	 */
	bool scanImageFile();

	/**
	 * @brief Prepare OCIO processor shader text and LUT textures.
	 *
	 * Called during startup and when OCIO display/view selection changes.
	 */
	void configureOCIO();

	/**
	 * @brief Bind OCIO LUT textures to their assigned texture units.
	 *
	 * Called in draw before the image quad render call.
	 */
	void bindOCIOTextures();

	/**
	 * @brief Delete OCIO LUT textures.
	 *
	 * Called before rebuilding OCIO state and during shutdown.
	 */
	void releaseOCIOTextures();

	/**
	 * @brief Create fullscreen quad geometry used by image/frame programs.
	 *
	 * Called during startup.
	 */
	void createPlane();

	/**
	 * @brief Compile and attach one shader stage.
	 *
	 * Called by `createShaders`.
	 *
	 * @param program Program handle receiving the compiled stage.
	 * @param shaderCode Shader source text.
	 * @param type GL shader stage enum.
	 */
	void addShader(GLuint program, const char* shaderCode, GLenum type);

	/**
	 * @brief Link render programs and assign sampler uniforms.
	 *
	 * Called during startup after OCIO shader text is prepared.
	 */
	void createShaders();

	/**
	 * @brief Background loader worker entry point.
	 *
	 * Started by constructor, stopped by destructor.
	 */
	void loader();

	/**
	 * @brief Queue one plane for async loading.
	 *
	 * Called by run loop while `mtx` is held.
	 *
	 * @param plane Plane payload to queue.
	 */
	void enqueueLoadLocked(ImagePlaneData* plane);

	/**
	 * @brief Queue record for async load and texture generation.
	 *
	 * @details
	 * Why this struct exists:
	 * - Couples a plane pointer with queue generation so stale tasks can be ignored.
	 * - Provides one shared payload type across worker and render handoff queues.
	 *
	 * Where it is used:
	 * - Produced/consumed by `enqueueLoadLocked()`, `loader()`, and `draw()`.
	 */
	struct LoadTask
	{
		ImagePlaneData* plane = nullptr;
		std::uint64_t generation = 0;
	};

private:
	/** GLFW window used by event/render loop. */
	GLFWwindow* mainWindow = nullptr;

	/** Current opened image path. */
	std::string imageFileName;
	/** Number of scanned subimages in current file. */
	unsigned int subimages = 0;
	/** Total mip count across scanned subimages. */
	unsigned int mips = 0;

	/** Logical planes for the current file. */
	std::vector<ImagePlane> imagePlanes;
	/** Pending CPU load tasks. */
	std::vector<LoadTask> loadingQueue;
	/** Completed CPU load tasks waiting for GL upload. */
	std::queue<LoadTask> textureQueue;
	/** Mutex guarding loader queues and task states. */
	std::mutex mtx;
	/** Loader wakeup/idle condition variable. */
	std::condition_variable queueCondition;
	/** Background worker thread for file reads. */
	std::thread loaderThread;
	/** Loader shutdown flag set by destructor. */
	bool loaderStop = false;
	/** Number of active load tasks in worker. */
	size_t activeLoads = 0;
	/** Generation counter used to invalidate stale queued work. */
	std::uint64_t queueGeneration = 1;

	/** Selected logical plane index. */
	int activePlaneIdx = 0;
	/** Selected mip index for active plane. */
	int activeMIP = 0;

	/** Quad VAO for render calls. */
	GLuint VAO = 0;
	/** Quad VBO for render calls. */
	GLuint VBO = 0;

	/** Main image shader program. */
	GLuint shader = 0;
	/** Frame-outline shader program. */
	GLuint frameShader = 0;
	/** Cached uniform locations for the image program. */
	ImageShaderUniforms shaderUniforms;
	/** Cached uniform locations for the frame program. */
	FrameShaderUniforms frameShaderUniforms;

	/** Base zoom scale value. */
	float scale = 1.0f;
	/** View offset X in screen space. */
	float offsetX = 0.25f;
	/** View offset Y in screen space. */
	float offsetY = 0.25f;

	/** Vertex shader source for both programs. */
	std::string vertexShaderCode = R"glsl(
		#version 330 core
		layout (location = 0) in vec2 position;
		uniform vec2 scale;
		uniform vec2 offset;
		out vec2 texCoords;
		void main() {
			gl_Position = vec4((position*2.0 - 1.0)*scale + offset*2, 0.0, 1.0);
			texCoords = vec2(position.x, 1.0 - position.y);
		}
	)glsl";

	/** Default fragment shader source before OCIO replaces it. */
	std::string fragmentShaderCode = R"glsl(
		#version 330 core
		out vec4 FragColor;
		in vec2 texCoords;
		uniform sampler2D textureSampler;
		void main() {
			vec4 fragment = texture(textureSampler, texCoords.xy);
			FragColor.rgb = pow(fragment.rgb, vec3(1.0f / 1.8f)) - 0.05;
		}
	)glsl";

	/** Frame-outline shader source used for display-window guides. */
	std::string frameFragmentShaderCode = R"glsl(
		#version 330 core
		out vec4 FragColor;
		void main() {
			float dashed = (int(gl_FragCoord.x/3) + int(gl_FragCoord.y/3)) % 2;
			FragColor = vec4(vec3(0.25), 0.75 * dashed);
		}
	)glsl";

	/** Active channel solo mode. */
	int channelSoloing = 0;
	/** Inspector toggle state. */
	bool inspect = false;
	/** Active region-selection state for inspector averaging. */
	bool inspectRegionActive = false;
	/** Drag in progress flag for region selection. */
	bool inspectRegionDragging = false;
	/** Drag movement threshold result for region selection. */
	bool inspectRegionMoved = false;
	/** Region selection start in image-space coordinates. */
	ImVec2 inspectRegionStart = ImVec2(0.0f, 0.0f);
	/** Region selection end in image-space coordinates. */
	ImVec2 inspectRegionEnd = ImVec2(0.0f, 0.0f);
	/** Split-screen waveform analysis visibility toggle (shortcut `W`). */
	bool waveformSplitView = false;
	/** Width fraction used by the split waveform pane when enabled. */
	float waveformSplitRatio = 0.5f;
	/** Helper owning waveform cache and drawing for split analysis mode. */
	WaveformPanel waveformPanel;

	/** Fullscreen UI mode toggle. */
	bool fullScreen = false;
	/** OCIO display/view picker visibility toggle (shortcut `O`). */
	bool ocioPickerVisible = false;
	/** Startup/scan message text shown when no image is loaded. */
	std::string message;

	/**
	 * @brief One uploaded OCIO LUT texture binding.
	 *
	 * @details
	 * Why this struct exists:
	 * - Stores GL id/target/unit and sampler symbol as one binding record.
	 * - Keeps generated OCIO LUT bindings explicit instead of spreading parallel arrays.
	 *
	 * Where it is used:
	 * - Built by `configureOCIO()`.
	 * - Consumed by `createShaders()` and `bindOCIOTextures()`.
	 */
	struct OcioLutTexture
	{
		GLuint id = 0;
		GLenum target = GL_TEXTURE_2D;
		GLint unit = 0;
		std::string samplerName;
	};

	/** Uploaded OCIO LUT textures required by current OCIO shader program. */
	std::vector<OcioLutTexture> ocioLutTextures;
	/** Label for currently loaded OCIO config source (`env` or `builtin`). */
	std::string ocioConfigSource;
	/** Active OCIO display used for shader generation. */
	std::string ocioSelectedDisplay;
	/** Active OCIO view used for shader generation. */
	std::string ocioSelectedView;
	/** Available OCIO displays from current config. */
	std::vector<std::string> ocioDisplays;
	/** Available OCIO views for `ocioSelectedDisplay`. */
	std::vector<std::string> ocioViews;

	/** Shared OIIO cache used by `ImagePlaneData` load operations. */
	std::shared_ptr<OIIO::ImageCache> cache;
};
