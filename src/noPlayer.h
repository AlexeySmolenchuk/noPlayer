#pragma once

#include "imagePlane.h"

#include <string>
#include <queue>
#include <condition_variable>
#include <thread>
#include <cstdint>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <OpenImageIO/imagecache.h>

/**
 * @brief Interactive image viewer application.
 *
 * Manages the GLFW window, ImGui UI, image discovery/loading, and OpenGL
 * rendering. Image loading is performed asynchronously by a worker thread.
 */
class NoPlayer
{

public:
	/**
	 * @brief Create the application window and initialize rendering/UI systems.
	 */
	NoPlayer();

	/**
	 * @brief Stop background work and release UI/window resources.
	 */
	~NoPlayer();

	/**
	 * @brief Load metadata for an image file and queue plane loading.
	 *
	 * @param fileName Path to the image file to open.
	 * @param fresh When true, reset view state (zoom, offsets, active plane/MIP).
	 */
	void init(const char* fileName, bool fresh = true);

	/**
	 * @brief Run the main event/render loop until the window is closed.
	 */
	void run();

	/**
	 * @brief Draw one frame of UI and image content.
	 */
	void draw();

	/**
	 * @brief Select channel display mode.
	 * @param idx Channel selector: 0 for combined view, 1..N for individual channels.
	 */
	void setChannelSoloing(int idx) {channelSoloing = idx;}

	/**
	 * @brief Clear current image state and wait for in-flight loads to finish.
	 */
	void clear();

	/**
	 * @brief Get the currently opened image file path.
	 * @return Current image file path, or an empty string if none is set.
	 */
	std::string getFileName() {return imageFileName;}

private:
	/**
	 * @brief Inspect the file and build the in-memory image plane model.
	 * @return True on success, false when the file cannot be scanned.
	 */
	bool scanImageFile();

	/**
	 * @brief Configure OpenColorIO state used during rendering.
	 */
	void configureOCIO();

	/**
	 * @brief Create the fullscreen quad geometry used for image drawing.
	 */
	void createPlane();

	/**
	 * @brief Compile and attach a shader stage to a program.
	 * @param program OpenGL program handle.
	 * @param shaderCode GLSL source code.
	 * @param type Shader stage type (for example, @c GL_VERTEX_SHADER).
	 */
	void addShader(GLuint program, const char* shaderCode, GLenum type);

	/**
	 * @brief Build and link shader programs used by the renderer.
	 */
	void createShaders();

	/**
	 * @brief Background worker entry point that loads plane pixels from disk.
	 */
	void loader();

	/**
	 * @brief Queue an image plane for background loading.
	 *
	 * The caller must hold @c mtx before calling this function.
	 *
	 * @param plane Plane data to enqueue.
	 */
	void enqueueLoadLocked(ImagePlaneData* plane);

	/**
	 * @brief Work item used by asynchronous loading and texture upload queues.
	 */
	struct LoadTask
	{
		ImagePlaneData* plane = nullptr;
		std::uint64_t generation = 0;
	};

private:
	GLFWwindow *mainWindow;

	std::string imageFileName;
	unsigned int subimages;
	unsigned int mips;

	std::vector<ImagePlane> imagePlanes;
	std::vector<LoadTask> loadingQueue;
	std::queue<LoadTask> textureQueue;
	std::mutex mtx;
	std::condition_variable queueCondition;
	std::thread loaderThread;
	bool loaderStop = false;
	size_t activeLoads = 0;
	std::uint64_t queueGeneration = 1;

	int activePlaneIdx;
	int activeMIP;

	GLuint VAO;
	GLuint VBO;

	GLuint shader;
	GLuint frameShader;

	float scale;
	float offsetX;
	float offsetY;

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

	// outline frame for Display Window
	std::string frameFragmentShaderCode = R"glsl(
		#version 330 core
		out vec4 FragColor;
		void main() {
			float dashed = (int(gl_FragCoord.x/3) + int(gl_FragCoord.y/3)) % 2;
			FragColor = vec4(vec3(0.25), 0.75 * dashed);
		}
	)glsl";

	int channelSoloing = 0;
	bool inspect = false;

	bool fullScreen = false;

	std::string message = "";

	std::shared_ptr<OIIO::ImageCache> cache;
};
