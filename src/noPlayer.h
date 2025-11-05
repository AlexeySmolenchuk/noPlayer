#pragma once

#include "imagePlane.h"

#include <string>
#include <queue>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <OpenImageIO/imagecache.h>

class NoPlayer
{

public:
	NoPlayer();
	~NoPlayer();

	void init(const char* fileName, bool fresh = true);
	void run();
	void draw();
	void setChannelSoloing(int idx) {channelSoloing = idx;}
	void clear() { imagePlanes.clear();}
	std::string getFileName() {return imageFileName;}

private:
	bool scanImageFile();
	void configureOCIO();
	void createPlane();
	void addShader(GLuint program, const char* shaderCode, GLenum type);
	void createShaders();
	void loader();

private:
	GLFWwindow *mainWindow;

	std::string imageFileName;
	unsigned int subimages;
	unsigned int mips;

	std::vector<ImagePlane> imagePlanes;
	std::vector<ImagePlaneData*> loadingQueue;
	std::queue<ImagePlaneData*> textureQueue;
	std::mutex mtx;

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

	std::shared_ptr<OpenImageIO_v3_0::ImageCache> cache;
};
