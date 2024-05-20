#include <iostream>
#include <string>
#include <thread>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <OpenImageIO/imageio.h>
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;

// #define PRECISION_FLOAT

#ifdef PRECISION_FLOAT
	#define precision float
	#define PRECISION FLOAT
	#define PRECISION_GL GL_FLOAT
	int internalFormats[] = {0, GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F};
#else()
	#include <half.h>
	#define precision half
	#define PRECISION HALF
	#define PRECISION_GL GL_HALF_FLOAT
	int internalFormats[] = {0, GL_R16F, GL_RG16F, GL_RGB16F, GL_RGBA16F};
#endif()


class NoPlayer
{

	struct ImagePlane
	{
		void load();
		void generateGlTexture();

		std::string imageFileName;
		unsigned int subimage;
		unsigned int mip;
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

		bool windowEqualData; // is Data Window match Display Window

		int begin; // index in oiio spec
		int len;
		precision *pixels;		// fill deferred
		GLuint glTexture;	// fill deferred
		int ready = 0;

		float gainValues = 1.0;
		float offsetValues = 0.0;

		~ImagePlane() { delete[] pixels;}
	};

public:
	NoPlayer();
	~NoPlayer();

	void init(const char* fileName);
	void run();
	void draw();
	void setChannelSoloing(int idx) {channelSoloing = idx;}
	void clear() { imagePlanes.clear();}

private:
	void scanImageFile();
	void configureOCIO();
	void createPlane();
	void addShader(GLuint program, const char* shaderCode, GLenum type);
	void createShaders();

private:
	GLFWwindow *mainWindow;

	std::string imageFileName;
	unsigned int subimages;
	unsigned int mips;

	std::vector<ImagePlane> imagePlanes;
	int activePlaneIdx;

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
			FragColor = vec4(0.2);
		}
	)glsl";

	int channelSoloing = 0;
	bool inspect = false;

	bool fullScreen = false;
};


void dropCallback(GLFWwindow* window, int count, const char** paths)
{
    // for (int i = 0;  i < count;  i++)
    //     std::cout << paths[i] << std::endl;
	NoPlayer *view = static_cast<NoPlayer*>(glfwGetWindowUserPointer(window));
	view->clear();
	view->init(paths[0]);
}


void framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width and
	// height will be significantly larger than specified on retina displays.
	// glViewport(0, 0, width, height);
	NoPlayer *view = static_cast<NoPlayer*>(glfwGetWindowUserPointer(window));
	view->draw();
}


GLFWmonitor* getCurrentMonitor(GLFWwindow *window)
{
	int nmonitors, i;
	int wx, wy, ww, wh;
	int mx, my, mw, mh;
	int overlap, bestoverlap;
	GLFWmonitor *bestmonitor;
	GLFWmonitor **monitors;
	const GLFWvidmode *mode;

	bestoverlap = 0;
	bestmonitor = NULL;

	glfwGetWindowPos(window, &wx, &wy);
	glfwGetWindowSize(window, &ww, &wh);
	monitors = glfwGetMonitors(&nmonitors);

	for (i = 0; i < nmonitors; i++)
	{
		mode = glfwGetVideoMode(monitors[i]);
		glfwGetMonitorPos(monitors[i], &mx, &my);
		mw = mode->width;
		mh = mode->height;

		overlap =
			std::max(0, std::min(wx + ww, mx + mw) - std::max(wx, mx)) *
			std::max(0, std::min(wy + wh, my + mh) - std::max(wy, my));

		if (bestoverlap < overlap)
		{
			bestoverlap = overlap;
			bestmonitor = monitors[i];
		}
	}

	return bestmonitor;
}


void keyCallback(GLFWwindow* mainWindow, int key, int scancode, int action, int mods)
{
	// FullScreen Mode
	if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
	{
		if ( glfwGetKey( mainWindow, GLFW_KEY_F11 ) == GLFW_PRESS )
		{
			static bool fullScreen = false;
			static int x,y,w,h;
			if (fullScreen)
			{
				glfwSetWindowPos(mainWindow, x, y);
				glfwSetWindowSize(mainWindow, w, h);
				fullScreen = false;
			}
			else
			{
				glfwGetWindowPos(mainWindow, &x, &y);
				glfwGetWindowSize(mainWindow, &w, &h);
				GLFWmonitor *monitor = getCurrentMonitor(mainWindow);
				int _x, _y ,_w ,_h;
				glfwGetMonitorWorkarea(monitor, &_x, &_y, &_w, &_h);
				glfwSetWindowPos(mainWindow, _x, _y);

				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				glfwSetWindowSize(mainWindow, mode->width, mode->height);
				fullScreen = true;
			}
			glfwSwapBuffers(mainWindow);
		}
	}

	NoPlayer *view = static_cast<NoPlayer*>(glfwGetWindowUserPointer(mainWindow));

	if ( glfwGetKey( mainWindow, GLFW_KEY_GRAVE_ACCENT ) == GLFW_PRESS )
		view->setChannelSoloing(0);
	if ( glfwGetKey( mainWindow, GLFW_KEY_1 ) == GLFW_PRESS )
		view->setChannelSoloing(1);
	if ( glfwGetKey( mainWindow, GLFW_KEY_2 ) == GLFW_PRESS )
		view->setChannelSoloing(2);
	if ( glfwGetKey( mainWindow, GLFW_KEY_3 ) == GLFW_PRESS )
		view->setChannelSoloing(3);
	if ( glfwGetKey( mainWindow, GLFW_KEY_4 ) == GLFW_PRESS )
		view->setChannelSoloing(4);

	if ( glfwGetKey( mainWindow, GLFW_KEY_ESCAPE ) == GLFW_PRESS )
		glfwSetWindowShouldClose(mainWindow, GL_TRUE);
}


NoPlayer::NoPlayer()
{

	// glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);

	if (!glfwInit())
	{
		std::cout << "GLFW initialisation failed!\n";
		glfwTerminate();
		std::exit(1);
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	// Create the window
	mainWindow = glfwCreateWindow(1280, 720, "noPlayer ", nullptr, nullptr);
	if (!mainWindow)
	{
		std::cout << "GLFW creation failed!\n";
		glfwTerminate();
		std::exit(1);
	}

	// Helper pointer to run callbacks
	glfwSetWindowUserPointer(mainWindow, this);
	glfwSetFramebufferSizeCallback(mainWindow, framebufferSizeCallback);
	glfwSetKeyCallback(mainWindow, keyCallback);
	glfwSetDropCallback(mainWindow, dropCallback);

	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(mainWindow, &bufferWidth, &bufferHeight);
	glfwMakeContextCurrent(mainWindow);
	glfwSwapInterval(1);

	if (glewInit() != GLEW_OK)
	{
		std::cout << "glew initialisation failed!\n";
		glfwDestroyWindow(mainWindow);
		glfwTerminate();
		std::exit(1);
	}

	glViewport(0, 0, bufferWidth, bufferHeight);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = NULL;
	io.LogFilename = NULL;

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	configureOCIO();

	createPlane();
	createShaders();

};


void
NoPlayer::init(const char* fileName)
{
	imageFileName = fileName;
	scanImageFile();

	scale = 1.f;
	// With this little offset we can align image and screen pixels for even and odd resolutions
	offsetX = 0.25f; // Offset of viewed image
	offsetY = 0.25f; // Offset of viewed image
	channelSoloing = 0;
	activePlaneIdx = 0;

	// Preload
	std::thread(&NoPlayer::ImagePlane::load, std::ref(imagePlanes[activePlaneIdx])).detach();
	imagePlanes[0].ready = 1;
}


NoPlayer::~NoPlayer()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(mainWindow);
	glfwTerminate();
}


void NoPlayer::run()
{
	while (!glfwWindowShouldClose(mainWindow))
	{
		glfwPollEvents();

		if (imagePlanes.size())
		{

			if ( imagePlanes[activePlaneIdx].ready == 0) // not issued yet
			{
				std::thread(&NoPlayer::ImagePlane::load, std::ref(imagePlanes[activePlaneIdx])).detach();
				imagePlanes[activePlaneIdx].ready = 1; // just issued
			}

			// we can preload next AOV
			int next = (activePlaneIdx + 1)%imagePlanes.size();
			if ( imagePlanes[next].ready == 0) // not issued yet
			{
				std::thread(&NoPlayer::ImagePlane::load, std::ref(imagePlanes[next])).detach();
				imagePlanes[next].ready = 1; // just issued
			}

			if ( imagePlanes[activePlaneIdx].ready == 2) // pixels loaded in RAM
			{
				imagePlanes[activePlaneIdx].generateGlTexture();
				imagePlanes[activePlaneIdx].ready = 3; // corresponding gl texture created
			}
		}

		draw();
	}
}


void NoPlayer::draw()
{

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();

	glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	int displayW, displayH;
	glfwGetFramebufferSize(mainWindow, &displayW, &displayH);

	ImGuiIO& io = ImGui::GetIO();
	ImGui::NewFrame();

	if (imagePlanes.size() == 0)
	{
		{
			ImGui::SetNextWindowPos(ImVec2(10, 10));
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_AlwaysAutoResize
										| ImGuiWindowFlags_NoBackground;
										;
			ImGui::Begin( "Info", nullptr, windowFlags);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1));

			ImGui::Text((const char *)glGetString(GL_VENDOR));
			ImGui::Text((const char *)glGetString(GL_RENDERER));
			ImGui::PopStyleColor();
			ImGui::End();
		}

		{
			const char* message = "Drop image";
			ImGui::SetNextWindowPos( (ImVec2(displayW, displayH) - ImGui::CalcTextSize(message))/2);
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_NoBackground;
			ImGui::Begin( "Hello", nullptr, windowFlags);
			ImGui::Text(message);
			ImGui::End();
		}

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(mainWindow);
		return;
	}


	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightBracket)))
	{
		activePlaneIdx = (activePlaneIdx+1)%imagePlanes.size();
		channelSoloing = 0;
	}

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftBracket)))
	{
		activePlaneIdx = (imagePlanes.size()+activePlaneIdx-1)%imagePlanes.size();
		channelSoloing = 0;
	}


	ImagePlane &plane = imagePlanes[activePlaneIdx];
	float compensate = powf(2.0, plane.mip);

	static int lag = 0;
	static float targetScale = scale;
	static float targetOffsetX = offsetX;
	static float targetOffsetY = offsetY;

	// Zoom by scrolling
	if (!io.WantCaptureMouse && io.MouseWheel!=0.0)
	{
		ImVec2 scalePivot = ImGui::GetMousePos() - ImVec2(displayW, displayH)/2 - ImVec2(offsetX, offsetY);
		float factor = powf(2, io.MouseWheel/3.0f);
		ImVec2 temp = scalePivot*(factor-1);
		offsetX = offsetX - temp.x;
		offsetY = offsetY - temp.y;

		scale *= powf(2, io.MouseWheel/3.f);
	}

	//Zoom in
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadAdd)))
	{
		targetOffsetX = offsetX * 2;
		targetOffsetY = offsetY * 2;
		targetScale = scale * 2;
		lag = 4;
	}

	// Zoom out
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadSubtract)))
	{
		targetOffsetX = offsetX * 0.5;
		targetOffsetY = offsetY * 0.5;
		targetScale = scale * 0.5;
		lag = 4;
	}

	if (lag)
	{
		float f = 1.0/lag;
		scale = scale * (1.0-f) + targetScale * f;
		offsetX = offsetX * (1.0-f) + targetOffsetX * f;
		offsetY = offsetY * (1.0-f) + targetOffsetY * f;
		lag--;
	}

	// Scale with RMB
	// While adjusting zoom values are updated in "shift" and "factor"
	// When finished (RMB released) these values are baked into scale and offsetX offsetY
	static float factor = 1.0;
	static ImVec2 shift(0, 0);
	if (io.MouseDown[1])
	{
		ImVec2 delta = ImGui::GetMousePos() - io.MouseClickedPos[1];

		float drag = (delta.x - delta.y) * 0.01;
		factor = powf( 2, drag / 3.0f);

		ImVec2 scalePivot = io.MouseClickedPos[1] - ImVec2(displayW, displayH)/2 - ImVec2(offsetX, offsetY);
		shift = scalePivot * (factor - 1);
	}
	else if (io.MouseReleased[1])
	{
		scale *= factor;
		offsetX -= shift.x;
		offsetY -= shift.y;

		shift = ImVec2(0, 0);
		factor = 1.0;
	}

	// Pan
	if (io.MouseDown[2])
	{
		offsetX += ImGui::GetIO().MouseDelta.x;
		offsetY += ImGui::GetIO().MouseDelta.y;
	}

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F)))
	{
		// with this little offset we align image and screen pixels for even and odd resolutions
		offsetX = 0.25;
		offsetY = 0.25;
		if (scale == 1.0/compensate)
		{
			scale = std::min(float(displayH)/float(plane.windowHeight), float(displayW)/float(plane.windowWidth))/compensate;
		}
		else
		{
			scale = 1.0/compensate;
		}
	}

	static bool ui = true;
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_H)))
		ui = !ui;

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_0)))
	{
		plane.gainValues = 1.0;
		plane.offsetValues = 0.0;
	}

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Equal)))
		plane.gainValues *= 2.0;

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Minus)))
		plane.gainValues *= 0.5;

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_I)))
		inspect = !inspect;


	if (ui)
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
									| ImGuiWindowFlags_AlwaysAutoResize
									| ImGuiWindowFlags_NoBackground;
									;
		ImGui::Begin( "Info", nullptr, windowFlags);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1));
		// ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		ImGui::Text(imageFileName.c_str());
		if (subimages>1)
		{
			ImGui::SameLine();
			ImGui::Text("[%d Subimages]", subimages);
		}

		if (plane.windowEqualData)
		{
			ImGui::Text("%dx%d", plane.imageWidth, plane.imageHeight);
		}
		else
		{
			ImGui::Text("Display: %dx%d", plane.windowWidth, plane.windowHeight);
			if(plane.windowOffsetX!=0 || plane.windowOffsetY!=0)
			{
				ImGui::SameLine();
				ImGui::Text("(%d,%d)", plane.windowOffsetX, plane.windowOffsetY);
			}
			ImGui::Text("Data:	%dx%d", plane.imageWidth, plane.imageHeight);
			if(plane.imageOffsetX!=0 || plane.imageOffsetY!=0)
			{
				ImGui::SameLine();
				ImGui::Text("(%d,%d)", plane.imageOffsetX, plane.imageOffsetY);
			}
		}

		if (mips>subimages)
		{
			ImGui::SameLine();
			ImGui::Text("[%d/%d MIP]", plane.mip, mips);
		}

		ImGui::Text("Zoom %.01f%%", scale*factor*compensate*100);
		ImGui::PopStyleColor();
		ImGui::End();


		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0, 1.0, 1.0, 0.2));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0, 1.0, 1.0, 0.1));
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0, 1.0, 1.0, 0.0));

		ImGui::SetNextWindowPos(ImVec2(10, 100));
		ImGui::SetNextWindowSizeConstraints(ImVec2(-1, 0), ImVec2(-1, displayH - 120));

		windowFlags = ImGuiWindowFlags_None
					| ImGuiWindowFlags_NoDecoration
					| ImGuiWindowFlags_NoNav
					| ImGuiWindowFlags_AlwaysAutoResize
					| ImGuiWindowFlags_NoBackground;

		ImGui::Begin( "AOVs", nullptr, windowFlags);
		for (int n = 0; n < imagePlanes.size(); n++)
		{
			if(activePlaneIdx == n)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0, 1.0, 1.0, 1));
			else
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1));

			ImGui::PushID(n);
			if (ImGui::Selectable(imagePlanes[n].name.c_str(), activePlaneIdx == n))
			{
				activePlaneIdx = n;
				channelSoloing = 0;
			}
			ImGui::PopID();

			if (imagePlanes[n].name.empty())
				ImGui::SameLine(0, 0);
			else
				ImGui::SameLine();


			if (!imagePlanes[n].groupName.empty())
			{
				ImGui::Text("%s.", imagePlanes[n].groupName.c_str());
				ImGui::SameLine(0, 0);
			}

			if(activePlaneIdx == n)
			{
				if (channelSoloing==0)
				{
					ImGui::Text(plane.channels.c_str());
				}
				else
				{
					for (int i = 0; i < plane.len; i++)
					{
						if (i) ImGui::SameLine(0, 0);
						ImGui::TextColored( ((i+1)==channelSoloing) ? ImVec4(1,1,1,1) : ImVec4(0.5,0.5,0.5,1), plane.channels.substr(i, 1).c_str());
					}
				}
				ImGui::SameLine();
				ImGui::TextColored( ImVec4(0.5,0.5,0.5,1), imagePlanes[n].format.c_str());
			}
			else
			{
				ImGui::TextColored( ImVec4(0.5,0.5,0.5, 1), imagePlanes[n].channels.c_str());
			}
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleColor(3);
		ImGui::End();

		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5, 0.5, 0.5, 0.05));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.5, 0.5, 0.5, 0.1));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.5, 0.5, 0.5, 0.15));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1.0));
			ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.5, 0.5, 0.5, 0.1));

			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None
										| ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_NoBackground
										;

			ImGui::SetNextWindowPos(ImVec2(displayW/2-150, displayH - 35));
			ImGui::SetNextWindowSize(ImVec2(150, 0));

			ImGui::Begin( "Gain", nullptr, windowFlags);
			plane.gainValues = std::clamp( plane.gainValues, -1000000.f, 1000000.f);
			ImGui::DragFloat("Gain", &(plane.gainValues), std::max(0.00001, std::abs(plane.gainValues)*0.01));
			ImGui::End();


			ImGui::SetNextWindowPos(ImVec2(displayW/2, displayH - 35));
			ImGui::SetNextWindowSize(ImVec2(150, 0));

			ImGui::Begin( "Offset", nullptr, windowFlags);
			plane.offsetValues = std::clamp( plane.offsetValues, -1000000.f, 1000000.f);
			ImGui::DragFloat("Offset", &(plane.offsetValues), std::max(0.00001, std::abs(plane.offsetValues)*0.01));
			ImGui::End();

			ImGui::PopStyleColor(5);
		}
	}

	if(plane.ready != 3)
	{
		const char* message = "Loading...";
		ImGui::SetNextWindowPos( (ImVec2(displayW, displayH) - ImGui::CalcTextSize(message))/2);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
									| ImGuiWindowFlags_NoBackground;
		ImGui::Begin( "Loading", nullptr, windowFlags);
		ImGui::Text(message);
		ImGui::End();
	}

	if(inspect)
	{
		float centerX = plane.imageOffsetX + plane.imageWidth * 0.5
						- plane.windowOffsetX - plane.windowWidth * 0.5;
		float centerY = plane.imageOffsetY + plane.imageHeight * 0.5
						- plane.windowOffsetY - plane.windowHeight * 0.5;

		ImVec2 mousePos = ImGui::GetMousePos();
		ImVec2 coords = mousePos - ImVec2(displayW, displayH)/2 - ImVec2(offsetX, offsetY) + shift;
		coords /= scale * compensate * factor;
		coords += ImVec2(plane.imageWidth, plane.imageHeight)*0.5 - ImVec2(centerX, centerY);

		if(coords.x >= 0 && coords.x < plane.imageWidth && coords.y >= 0 && coords.y < plane.imageHeight)
		{
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.4f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

			ImGui::SetNextWindowPos(mousePos + ImVec2((mousePos.x + 128) > displayW ? -100.f : 16.f,
													  (mousePos.y + 128) > displayH ? -96.f : 24.f));
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_AlwaysAutoResize;

			ImGui::Begin( "Inspect", nullptr, windowFlags);

			int idx = (int(coords.x) + int(coords.y)*plane.imageWidth)*plane.len;

			if(!plane.windowEqualData)
				ImGui::Text("(%d, %d)", (int)(coords.x + plane.imageOffsetX - plane.windowOffsetX),
										(int)(coords.y + plane.imageOffsetY - plane.windowOffsetY));
			ImGui::Text("(%d, %d)", (int)(coords.x), (int)(coords.y));

			if (plane.ready>1)
				for (int i=0; i<plane.len; i++)
					ImGui::Text("%s %g",  plane.channels.substr(i, 1), float(plane.pixels[idx+i]));
			ImGui::End();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
		}
	}

	ImGui::Render();

	// Draw on Main Window Background
	glViewport(0, 0, displayW, displayH);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	if(channelSoloing <= plane.len)
	{
		glUseProgram(shader);

		float centerX =  plane.imageOffsetX + plane.imageWidth * 0.5f
						- plane.windowOffsetX - plane.windowWidth * 0.5f;
		float centerY =  plane.imageOffsetY + plane.imageHeight * 0.5f
						- plane.windowOffsetY - plane.windowHeight * 0.5f;

		glUniform2f(glGetUniformLocation(shader, "offset"),  (offsetX - shift.x + centerX * scale * factor * compensate)/(float)displayW,
															-(offsetY - shift.y + centerY * scale * factor * compensate)/(float)displayH);
		glUniform2f(glGetUniformLocation(shader, "scale"),  scale * factor * compensate * plane.imageWidth/(float)displayW,
															scale * factor * compensate * plane.imageHeight/(float)displayH);

		glUniform1f(glGetUniformLocation(shader, "gainValues"), plane.gainValues);
		glUniform1f(glGetUniformLocation(shader, "offsetValues"), plane.offsetValues);
		glUniform1i(glGetUniformLocation(shader, "soloing"), channelSoloing);
		glUniform1i(glGetUniformLocation(shader, "nchannels"), plane.len);

		static float flash = 0;
		flash += 1.0f/100;
		if (flash>1.0)
			flash = 0;

		glUniform1f(glGetUniformLocation(shader, "flash"), flash);

		glDisable(GL_DEPTH_TEST); // prevents framebuffer rectangle from being discarded
		glBindTexture(GL_TEXTURE_2D, plane.glTexture);
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}

	if (!plane.windowEqualData || channelSoloing > plane.len)
	{
		glUseProgram(frameShader);
		glUniform2f(glGetUniformLocation(frameShader, "offset"), (offsetX - shift.x)/(float)displayW,
																-(offsetY - shift.y)/(float)displayH);
		glUniform2f(glGetUniformLocation(frameShader, "scale"), scale * factor * compensate * plane.windowWidth/(float)displayW,
																scale * factor * compensate * plane.windowHeight/(float)displayH);
		glBindVertexArray(VAO);
		glDrawArrays(GL_LINE_LOOP, 0, 4);
		glBindVertexArray(0);
		glUseProgram(0);
	}

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glfwSwapBuffers(mainWindow);
}


void NoPlayer::scanImageFile()
{
	subimages = 0;
	mips = 0;

	using namespace OIIO;

	auto inp = ImageInput::open (imageFileName);
	if (! inp)
	{
		std::cerr << "Could not open " << imageFileName
				<< ", error = " << OIIO::geterror() << "\n";
		return;
	}

	std::vector<std::string> predefined = {"RGBA", "XYZ", "UV", "rgba", "xyz", "uv"};

	int mip = 0;
	while (inp->seek_subimage(subimages, mip))
	{
		while (inp->seek_subimage(subimages, mip))
		{
			int count = 0;
			const ImageSpec &spec = inp->spec();

			// std::cout
			// << "Compression:" << spec.decode_compression_metadata().first
			// << " " << spec.decode_compression_metadata().second << std::endl
			// << "tile pixels:" << spec.tile_pixels() << std::endl
			// << "tile width:" << spec.tile_width << std::endl
			// << "tile height:" << spec.tile_height << std::endl
			// << std::endl;

			bool windowEqualData = (spec.width == spec.full_width &&
									spec.height == spec.full_height &&
									spec.x == spec.full_x &&
									spec.y == spec.full_y);

			std::vector<bool> created(predefined.size());

			for(int i = 0; i < spec.nchannels; i++)
			{
				auto name = spec.channel_name(i);
				size_t pos = name.find_last_of('.');

				// Grouping for non "dot" separated names
				if (pos == std::string::npos)
				{
					bool groupFound = false;
					for (int n = 0; n < predefined.size(); n++)
					{
						if (predefined[n].find(name) != std::string::npos)
						{
							if (!created[n])
							{
								imagePlanes.emplace_back();
								auto &plane = imagePlanes.back();
								plane.imageFileName = imageFileName;
								plane.subimage = subimages;
								plane.mip = mip;
								plane.name = spec["Name"].get<std::string>();
								plane.groupName = "";

								imagePlanes.back().format = spec.channelformat(i).c_str();
								plane.begin = i;
								plane.len = 0;

								plane.imageWidth = spec.width;
								plane.imageHeight = spec.height;
								plane.imageOffsetX = spec.x;
								plane.imageOffsetY = spec.y;
								plane.windowWidth = spec.full_width;
								plane.windowHeight = spec.full_height;
								plane.windowOffsetX = spec.full_x;
								plane.windowOffsetY = spec.full_y;

								created[n] = true;
							}

							// Fill additional information for already created plane
							imagePlanes.back().channels += name;
							imagePlanes.back().len++;
							groupFound = true;
							break;
						}
					}

					// can't group automatically
					if (!groupFound)
					{
						imagePlanes.emplace_back();
						auto &plane = imagePlanes.back();
						plane.imageFileName = imageFileName;
						plane.subimage = subimages;
						plane.mip = mip;
						plane.name = spec["Name"].get<std::string>();
						plane.groupName = "";

						plane.channels += name;
						plane.format = spec.channelformat(i).c_str();
						plane.begin = i;
						plane.len = 1;

						plane.imageWidth = spec.width;
						plane.imageHeight = spec.height;
						plane.imageOffsetX = spec.x;
						plane.imageOffsetY = spec.y;
						plane.windowWidth = spec.full_width;
						plane.windowHeight = spec.full_height;
						plane.windowOffsetX = spec.full_x;
						plane.windowOffsetY = spec.full_y;
					}
				}
				else // Grouping for "dot" separated names
				{
					auto channel_group = name.substr(0, pos);
					if ((count == 0) || (imagePlanes.back().groupName != channel_group))
					{
						count++;
						imagePlanes.emplace_back();
						auto &plane = imagePlanes.back();
						plane.imageFileName = imageFileName;
						plane.subimage = subimages;
						plane.mip = mip;
						plane.name = spec["Name"].get<std::string>();
						plane.groupName = channel_group;
						plane.format = spec.channelformat(i).c_str();
						plane.begin = i;
						plane.len = 0;

						plane.imageWidth = spec.width;
						plane.imageHeight = spec.height;
						plane.imageOffsetX = spec.x;
						plane.imageOffsetY = spec.y;
						plane.windowWidth = spec.full_width;
						plane.windowHeight = spec.full_height;
						plane.windowOffsetX = spec.full_x;
						plane.windowOffsetY = spec.full_y;
					}
					imagePlanes.back().len++;
					imagePlanes.back().channels += name.substr(pos+1);
				}

				imagePlanes.back().windowEqualData = windowEqualData;
			}
			mip++;
			mips++;
		}
		subimages++;
		mip = 0;
	}

	// for (auto plane: imagePlanes)
	// {
	// 	std::cout <<
	// 	plane.name << " " <<
	// 	plane.groupName << " " <<
	// 	plane.channels << " " <<
	// 	plane.format << " " <<
	// 	plane.begin << " " <<
	// 	plane.len << std::endl;
	// }

	// std::cout << "Subimages: " << subimage << std::endl;
	// std::cout << "MIPs: " << mips << std::endl;

	// std::cout << spec.serialize(ImageSpec::SerialText) << std::endl;
}


void NoPlayer::ImagePlane::load()
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


void NoPlayer::ImagePlane::generateGlTexture()
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


void NoPlayer::configureOCIO()
{
	// std::cout << std::getenv( "OCIO" ) << std::endl;

	// for (int i = 0; i < OCIO::BuiltinConfigRegistry::Get().getNumBuiltinConfigs(); i++)
	// {
	// 	std::cout <<
	// 	OCIO::BuiltinConfigRegistry::Get().getBuiltinConfigName(i)
	// 	<< std::endl;
	// }

	OCIO::ConstConfigRcPtr config;
	// if(OCIO::IsEnvVariablePresent("OCIO"))
	// 	config = OCIO::GetCurrentConfig();
	// else
		config = OCIO::Config::CreateFromBuiltinConfig("studio-config-v1.0.0_aces-v1.3_ocio-v2.1");


	float g_exposure_fstop{ 0.0f };
	float g_display_gamma{ 1.0f };
	int g_channelHot[4]{ 1, 1, 1, 1 };  // show rgb

	std::string g_inputColorSpace;
	std::string g_display;
	std::string g_transformName;
	std::string g_look;
	OCIO::OptimizationFlags g_optimization{ OCIO::OPTIMIZATION_DEFAULT }; //OPTIMIZATION_DRAFT

	g_display = config->getDefaultDisplay();
	g_transformName = config->getDefaultView(g_display.c_str());
	g_look = config->getDisplayViewLooks(g_display.c_str(), g_transformName.c_str());
	g_inputColorSpace = OCIO::ROLE_SCENE_LINEAR;

	// std::cout << g_display << std::endl;
	// std::cout << g_transformName << std::endl;
	// std::cout << g_look << std::endl;

	OCIO::DisplayViewTransformRcPtr transform = OCIO::DisplayViewTransform::Create();
	transform->setSrc( g_inputColorSpace.c_str() );
	transform->setDisplay( g_display.c_str() );
	transform->setView( g_transformName.c_str() );

	OCIO::LegacyViewingPipelineRcPtr vp = OCIO::LegacyViewingPipeline::Create();
	vp->setDisplayViewTransform(transform);
	vp->setLooksOverrideEnabled(true);
	vp->setLooksOverride(g_look.c_str());

	// auto ect = OCIO::ExposureContrastTransform::Create();
	// ect->setStyle(OCIO::EXPOSURE_CONTRAST_LOGARITHMIC);
	// ect->makeExposureDynamic();
	// vp->setLinearCC(ect);

	OCIO::ConstProcessorRcPtr processor;
	processor = vp->getProcessor(config, config->getCurrentContext());

	// Set the shader context.
	OCIO::GpuShaderDescRcPtr shaderDesc = OCIO::GpuShaderDesc::CreateShaderDesc();
	shaderDesc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_3);
	shaderDesc->setFunctionName("OCIODisplay");
	shaderDesc->setResourcePrefix("ocio_");

	// Extract the shader information.
	OCIO::ConstGPUProcessorRcPtr gpu = processor->getOptimizedGPUProcessor(g_optimization);
	gpu->extractGpuShaderInfo(shaderDesc);

	fragmentShaderCode = R"glsl(#version 330 core
		out vec4 FragColor;
		in vec2 texCoords;
		uniform sampler2D textureSampler;
		uniform float gainValues;
		uniform float offsetValues;
		uniform int soloing;
		uniform int nchannels;
		uniform float flash;
	)glsl" +
	std::string(shaderDesc->getShaderText()) +
	R"glsl(
		void main() {
			FragColor = vec4(0.0);
			vec4 fragment = texture(textureSampler, texCoords.xy);
			for (int i=0; i<nchannels; i++){
				float f = abs(flash-0.5)*2.0;
				if (isnan(fragment[i]))
				{
					FragColor = vec4(1, 0.25, 0, 0) * f;
					return;
				}
				else if (isinf(fragment[i]))
				{
					FragColor = vec4(1, 0.25, 0, 0) * (1.0-f);
					return;
				}
			}

			fragment *= gainValues;
			fragment += vec4(offsetValues);

			FragColor = fragment;

			if (nchannels==1)
			{
				FragColor = FragColor.rrrr;
				return;
			}

			if (nchannels >=3)
				FragColor = OCIODisplay(fragment);

			if (soloing!=0){
				switch (soloing) {
				case 1:
					FragColor = FragColor.rrrr;
					break;
				case 2:
					FragColor = FragColor.gggg;
					break;
				case 3:
					FragColor = FragColor.bbbb;
					break;
				case 4:
					FragColor = FragColor.aaaa;
					break;
				}
			}
		}
	)glsl";

	// std::cout << shaderDesc->getShaderText() << std::endl;

	// std::cout << shaderDesc->getNum3DTextures() << std::endl;
	// std::cout << shaderDesc->getNumTextures() << std::endl;
}


void NoPlayer::createPlane()
{
	GLfloat vertices[] = {
		0.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 1.0f,
		1.0f, 0.0f,
	};

	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

}


void NoPlayer::addShader(GLuint program, const char* shader_code, GLenum type)
{
	GLuint current_shader = glCreateShader(type);

	const GLchar* code[1];
	code[0] = shader_code;

	GLint code_length[1];
	code_length[0] = strlen(shader_code);

	glShaderSource(current_shader, 1, code, code_length);
	glCompileShader(current_shader);

	GLint result = 0;
	GLchar log[1024] = {0};

	glGetShaderiv(current_shader, GL_COMPILE_STATUS, &result);
	if (!result)
	{
		glGetShaderInfoLog(current_shader, sizeof(log), NULL, log);
		std::cout << "Error compiling " << type << " shader: " << log << "\n";
		return;
	}

	glAttachShader(program, current_shader);
}


void NoPlayer::createShaders()
{
	shader = glCreateProgram();
	if(!shader)
	{
		std::cout << "Error creating shader program!\n";
		exit(1);
	}

	addShader(shader, vertexShaderCode.c_str(), GL_VERTEX_SHADER);
	addShader(shader, fragmentShaderCode.c_str(), GL_FRAGMENT_SHADER);

	GLint result = 0;
	GLchar log[1024] = {0};

	glLinkProgram(shader);
	glGetProgramiv(shader, GL_LINK_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(shader, sizeof(log), NULL, log);
		std::cout << "Error linking program:\n" << log << '\n';
		return;
	}

	glValidateProgram(shader);
	glGetProgramiv(shader, GL_VALIDATE_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(shader, sizeof(log), NULL, log);
		std::cout << "Error validating program:\n" << log << '\n';
		return;
	}

	frameShader = glCreateProgram();
	if(!frameShader)
	{
		std::cout << "Error creating shader program!\n";
		exit(1);
	}

	addShader(frameShader, vertexShaderCode.c_str(), GL_VERTEX_SHADER);
	addShader(frameShader, frameFragmentShaderCode.c_str(), GL_FRAGMENT_SHADER);

	glLinkProgram(frameShader);
	glGetProgramiv(frameShader, GL_LINK_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(frameShader, sizeof(log), NULL, log);
		std::cout << "Error linking program:\n" << log << '\n';
		return;
	}

	glValidateProgram(frameShader);
	glGetProgramiv(frameShader, GL_VALIDATE_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(frameShader, sizeof(log), NULL, log);
		std::cout << "Error validating program:\n" << log << '\n';
		return;
	}
}


int main(int argc, char**argv)
{

	NoPlayer noPlayerApp;

	if (argc > 1)
		noPlayerApp.init(argv[1]);

	noPlayerApp.run();

	return 0;
}
