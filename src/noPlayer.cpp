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
	int internal_format[] = {0, GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F};
#else()
	#include <half.h>
	#define precision half
	#define PRECISION HALF
	#define PRECISION_GL GL_HALF_FLOAT
	int internal_format[] = {0, GL_R16F, GL_RG16F, GL_RGB16F, GL_RGBA16F};
#endif()

int data_format[] = {0, GL_RED, GL_RG, GL_RGB, GL_RGBA};

class NoPlayer
{

	struct ImagePlane
	{
		void load();
		void generateGlTexture();

		std::string image_file;
		unsigned int subimage;
		unsigned int mip;
		std::string name;		// name of sub-image from metadata
		std::string groupName;	// group name if exists
		std::string channels;	// channels names after dot concatenated for corresponding group
		std::string format;		// string representation of data type

		// Data Window
		unsigned int image_width;
		unsigned int image_height;
		int image_offset_x;
		int image_offset_y;

		// Display Window
		unsigned int window_width;
		unsigned int window_height;
		int window_offset_x;
		int window_offset_y;

		bool windowEqualData; // is Data Window match Display Window

		int begin; // index in oiio spec
		int len;
		precision *pixels;		// fill deferred
		GLuint glTexture;	// fill deferred
		int ready = 0;

	};

public:
	NoPlayer(const char* filename);
	~NoPlayer();
	void run();
	void draw();
	void setChannelSoloing(int idx) {channel_soloing = idx;}

private:
	void scanImageFile();
	void configureOCIO();
	void createPlane();
	void addShader(GLuint program, const char* shader_code, GLenum type);
	void createShaders();

private:
	GLFWwindow *mainWindow;

	std::string image_file;
	unsigned int subimages = 0;
	unsigned int mips = 0;

	std::vector<ImagePlane> imagePlanes;
	int activePlaneIdx = 0;
	// unsigned int image_nchannels;

	GLuint VAO;
	GLuint VBO;

	GLuint shader;
	GLuint frameShader;

	float scale = 1.f;
	// With this little offset we align image and screen pixels for even and odd combinations
	float offset_x = 0.25f; // Offset of viewed image
	float offset_y = 0.25f; // Offset of viewed image

	std::string vertex_shader_code = R"glsl(
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

	std::string fragment_shader_code = R"glsl(
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
	std::string frame_fragment_shader_code = R"glsl(
		#version 330 core
		out vec4 FragColor;
		void main() {
			FragColor = vec4(0.2);
		}
	)glsl";

	float gain = 1.0;
	int channel_soloing = 0;
	bool inspect = false;

	bool fullscreen = false;
};


void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width and
	// height will be significantly larger than specified on retina displays.
	// glViewport(0, 0, width, height);
	NoPlayer *view = static_cast<NoPlayer*>(glfwGetWindowUserPointer(window));
	view->draw();
}


GLFWmonitor* get_current_monitor(GLFWwindow *window)
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


void key_callback(GLFWwindow* mainWindow, int key, int scancode, int action, int mods)
{
	// FullScreen Mode
	if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
	{
		if ( glfwGetKey( mainWindow, GLFW_KEY_F11 ) == GLFW_PRESS )
		{
			static bool fullscreen = false;
			static int x,y,w,h;
			if (fullscreen)
			{
				glfwSetWindowPos(mainWindow, x, y);
				glfwSetWindowSize(mainWindow, w, h);
				fullscreen = false;
			}
			else
			{
				glfwGetWindowPos(mainWindow, &x, &y);
				glfwGetWindowSize(mainWindow, &w, &h);
				GLFWmonitor *monitor = get_current_monitor(mainWindow);
				int _x, _y ,_w ,_h;
				glfwGetMonitorWorkarea(monitor, &_x, &_y, &_w, &_h);
				glfwSetWindowPos(mainWindow, _x, _y);

				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				glfwSetWindowSize(mainWindow, mode->width, mode->height);
				fullscreen = true;
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

NoPlayer::NoPlayer(const char* filename)
{
	image_file = filename;
	scanImageFile();

	std::thread(&NoPlayer::ImagePlane::load, std::ref(imagePlanes[activePlaneIdx])).detach();
	imagePlanes[0].ready = 1;

	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);

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
	mainWindow = glfwCreateWindow(1280, 720, (std::string("noPlayer ") + std::string(filename)).c_str(), nullptr, nullptr);
	if (!mainWindow)
	{
		std::cout << "GLFW creation failed!\n";
		glfwTerminate();
		std::exit(1);
	}

	// Helper pointer to run callbacks
	glfwSetWindowUserPointer(mainWindow, this);
	glfwSetFramebufferSizeCallback(mainWindow, framebuffer_size_callback);
	glfwSetKeyCallback(mainWindow, key_callback);


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

	std::cout << glGetString(GL_VENDOR) << std::endl;
	std::cout << glGetString(GL_RENDERER) << std::endl;

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

		draw();
	}
}


void NoPlayer::draw()
{
	ImagePlane &plane = imagePlanes[activePlaneIdx];
	float compensate = powf(2.0, plane.mip);


	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();

	glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	int display_w, display_h;
	glfwGetFramebufferSize(mainWindow, &display_w, &display_h);

	ImGuiIO& io = ImGui::GetIO();

	ImGui::NewFrame();

	// Zoom by scrolling
	if (!io.WantCaptureMouse && io.MouseWheel!=0.0)
	{
		ImVec2 scalePivot = ImGui::GetMousePos() - ImVec2(display_w, display_h)/2 - ImVec2(offset_x, offset_y);
		float factor = powf(2, io.MouseWheel/3.0f);
		ImVec2 temp = scalePivot*(factor-1);
		offset_x -= temp.x;
		offset_y -= temp.y;

		scale *= powf(2, io.MouseWheel/3.f);
	}

	//Zoom in
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadAdd)))
	{
		offset_x *= 2;
		offset_y *= 2;
		scale *= 2;
	}

	// Zoom out
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadSubtract)))
	{
		offset_x *= 0.5;
		offset_y *= 0.5;
		scale *= 0.5;
	}

	// Scale with RMB
	// While adjusting zoom values are updated in "shift" and "factor"
	// When finished (RMB released) these values are baked into scale and offset_x offset_y
	static float factor = 1.0;
	static ImVec2 shift(0, 0);
	if (io.MouseDown[1])
	{
		ImVec2 delta = ImGui::GetMousePos() - io.MouseClickedPos[1];

		float drag = (delta.x - delta.y) * 0.01;
		factor = powf( 2, drag / 3.0f);

		ImVec2 scalePivot = io.MouseClickedPos[1] - ImVec2(display_w, display_h)/2 - ImVec2(offset_x, offset_y);
		shift = scalePivot * (factor - 1);
	}
	else if (io.MouseReleased[1])
	{
		scale *= factor;
		offset_x -= shift.x;
		offset_y -= shift.y;

		shift = ImVec2(0, 0);
		factor = 1.0;
	}

	// Pan
	if (io.MouseDown[2])
	{
		offset_x += ImGui::GetIO().MouseDelta.x;
		offset_y += ImGui::GetIO().MouseDelta.y;
	}

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F)))
	{
		// with this little offset we align image and screen pixels for even and odd resolutions
		offset_x = 0.25;
		offset_y = 0.25;
		if (scale == 1.0/compensate)
		{
			scale = std::min(float(display_h)/float(plane.window_height), float(display_w)/float(plane.window_width))/compensate;
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
		gain = 1.0;

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Equal)))
		gain *= 2.0;

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Minus)))
		gain *= 0.5;

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_I)))
		inspect = !inspect;

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightBracket)))
		activePlaneIdx = (activePlaneIdx+1)%imagePlanes.size();

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftBracket)))
		activePlaneIdx = (imagePlanes.size()+activePlaneIdx-1)%imagePlanes.size();


	if (ui)
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration
									| ImGuiWindowFlags_AlwaysAutoResize
									| ImGuiWindowFlags_NoBackground;
									;
		ImGui::Begin( "Info", nullptr, window_flags);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1));
		// ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		ImGui::Text(image_file.c_str());
		if (subimages>1)
		{
			ImGui::SameLine();
			ImGui::Text("[%d Subimages]", subimages);
		}

		if (plane.windowEqualData)
		{
			ImGui::Text("%dx%d", plane.image_width, plane.image_height);
		}
		else
		{
			ImGui::Text("Display: %dx%d", plane.window_width, plane.window_height);
			if(plane.window_offset_x!=0 || plane.window_offset_y!=0)
			{
				ImGui::SameLine();
				ImGui::Text("(%d,%d)", plane.window_offset_x, plane.window_offset_y);
			}
			ImGui::Text("Data:	%dx%d", plane.image_width, plane.image_height);
			if(plane.image_offset_x!=0 || plane.image_offset_y!=0)
			{
				ImGui::SameLine();
				ImGui::Text("(%d,%d)", plane.image_offset_x, plane.image_offset_y);
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
		ImGui::SetNextWindowSizeConstraints(ImVec2(-1, 0), ImVec2(-1, display_h - 120));

		window_flags = ImGuiWindowFlags_None
					| ImGuiWindowFlags_NoDecoration
					| ImGuiWindowFlags_NoNav
					| ImGuiWindowFlags_AlwaysAutoResize
					| ImGuiWindowFlags_NoBackground;

		ImGui::Begin( "AOVs", nullptr, window_flags);
		for (int n = 0; n < imagePlanes.size(); n++)
		{
			if(activePlaneIdx == n)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0, 1.0, 1.0, 1));
			else
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1));

			ImGui::PushID(n);
			if (ImGui::Selectable(imagePlanes[n].name.c_str(), activePlaneIdx == n))
				activePlaneIdx = n;
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
				if (channel_soloing==0)
				{
					ImGui::Text(plane.channels.c_str());
				}
				else
				{
					for (int i = 0; i < plane.len; i++)
					{
						if (i) ImGui::SameLine(0, 0);
						ImGui::TextColored( ((i+1)==channel_soloing) ? ImVec4(1,1,1,1) : ImVec4(0.5,0.5,0.5,1), plane.channels.substr(i, 1).c_str());
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
	}

	{
		ImGui::SetNextWindowPos(ImVec2(500, 500));
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration
									| ImGuiWindowFlags_NoBackground
									// | ImGuiWindowFlags_AlwaysAutoResize
									;

		ImGui::Begin( "CC", nullptr, window_flags);
		gain = std::clamp( gain, -1000000.f, 1000000.f);
		ImGui::DragFloat("Gain", &gain, std::max(0.00001, std::abs(gain)*0.02));
		// ImGui::SameLine();
		static float offset=0;
		offset = std::clamp( offset, -1000000.f, 1000000.f);
		ImGui::DragFloat("Offset", &offset, std::max(0.00001, std::abs(offset)*0.02));
		ImGui::End();
	}

	if(plane.ready != 3)
	{
		ImGui::SetNextWindowPos( ImVec2(display_w, display_h)/2);
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration
									| ImGuiWindowFlags_NoBackground;
		ImGui::Begin( "Loading", nullptr, window_flags);
		ImGui::Text("Loading...");
		ImGui::End();
	}

	if(inspect)
	{
		float center_x = plane.image_offset_x + plane.image_width * 0.5
						- plane.window_offset_x - plane.window_width * 0.5;
		float center_y = plane.image_offset_y + plane.image_height * 0.5
						- plane.window_offset_y - plane.window_height * 0.5;

		ImVec2 mousePos = ImGui::GetMousePos();
		ImVec2 coords = mousePos - ImVec2(display_w, display_h)/2 - ImVec2(offset_x, offset_y) + shift;
		coords /= scale * compensate * factor;
		coords += ImVec2(plane.image_width, plane.image_height)*0.5 - ImVec2(center_x, center_y);

		if(coords.x >= 0 && coords.x < plane.image_width && coords.y >= 0 && coords.y < plane.image_height)
		{
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.4f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

			ImGui::SetNextWindowPos(mousePos + ImVec2((mousePos.x + 128) > display_w ? -100.f : 16.f,
													  (mousePos.y + 128) > display_h ? -96.f : 24.f));
			ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_AlwaysAutoResize;

			ImGui::Begin( "Inspect", nullptr, window_flags);

			int idx = (int(coords.x) + int(coords.y)*plane.image_width)*plane.len;

			if(!plane.windowEqualData)
				ImGui::Text("(%d, %d)", (int)(coords.x + plane.image_offset_x - plane.window_offset_x),
										(int)(coords.y + plane.image_offset_y - plane.window_offset_y));
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
	glViewport(0, 0, display_w, display_h);

	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glUseProgram(shader);

	float center_x =  plane.image_offset_x + plane.image_width * 0.5f
					- plane.window_offset_x - plane.window_width * 0.5f;
	float center_y =  plane.image_offset_y + plane.image_height * 0.5f
					- plane.window_offset_y - plane.window_height * 0.5f;

	glUniform2f(glGetUniformLocation(shader, "offset"),  (offset_x - shift.x + center_x * scale * factor * compensate)/(float)display_w,
														-(offset_y - shift.y + center_y * scale * factor * compensate)/(float)display_h);
	glUniform2f(glGetUniformLocation(shader, "scale"),  scale * factor * compensate * plane.image_width/(float)display_w,
														scale * factor * compensate * plane.image_height/(float)display_h);

	glUniform1f(glGetUniformLocation(shader, "gain"), gain);
	glUniform1i(glGetUniformLocation(shader, "soloing"), channel_soloing);

	glDisable(GL_DEPTH_TEST); // prevents framebuffer rectangle from being discarded
	glBindTexture(GL_TEXTURE_2D, plane.glTexture);
	glBindVertexArray(VAO);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	if (!plane.windowEqualData)
	{
		glUseProgram(frameShader);
		glUniform2f(glGetUniformLocation(frameShader, "offset"), (offset_x - shift.x)/(float)display_w,
																-(offset_y - shift.y)/(float)display_h);
		glUniform2f(glGetUniformLocation(frameShader, "scale"), scale * factor * compensate * plane.window_width/(float)display_w,
																scale * factor * compensate * plane.window_height/(float)display_h);
		glDrawArrays(GL_LINE_LOOP, 0, 4);
		glBindVertexArray(0);
		glUseProgram(0);
	}

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glfwSwapBuffers(mainWindow);
}


void NoPlayer::scanImageFile()
{
	using namespace OIIO;

	auto inp = ImageInput::open (image_file);
	if (! inp)
	{
		std::cerr << "Could not open " << image_file
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
								plane.image_file = image_file;
								plane.subimage = subimages;
								plane.mip = mip;
								plane.name = spec["Name"].get<std::string>();
								plane.groupName = "";

								imagePlanes.back().format = spec.channelformat(i).c_str();
								plane.begin = i;
								plane.len = 0;

								plane.image_width = spec.width;
								plane.image_height = spec.height;
								plane.image_offset_x = spec.x;
								plane.image_offset_y = spec.y;
								plane.window_width = spec.full_width;
								plane.window_height = spec.full_height;
								plane.window_offset_x = spec.full_x;
								plane.window_offset_y = spec.full_y;

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
						plane.image_file = image_file;
						plane.subimage = subimages;
						plane.mip = mip;
						plane.name = spec["Name"].get<std::string>();
						plane.groupName = "";

						plane.channels += name;
						plane.format = spec.channelformat(i).c_str();
						plane.begin = i;
						plane.len = 1;

						plane.image_width = spec.width;
						plane.image_height = spec.height;
						plane.image_offset_x = spec.x;
						plane.image_offset_y = spec.y;
						plane.window_width = spec.full_width;
						plane.window_height = spec.full_height;
						plane.window_offset_x = spec.full_x;
						plane.window_offset_y = spec.full_y;
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
						plane.image_file = image_file;
						plane.subimage = subimages;
						plane.mip = mip;
						plane.name = spec["Name"].get<std::string>();
						plane.groupName = channel_group;
						plane.format = spec.channelformat(i).c_str();
						plane.begin = i;
						plane.len = 0;

						plane.image_width = spec.width;
						plane.image_height = spec.height;
						plane.image_offset_x = spec.x;
						plane.image_offset_y = spec.y;
						plane.window_width = spec.full_width;
						plane.window_height = spec.full_height;
						plane.window_offset_x = spec.full_x;
						plane.window_offset_y = spec.full_y;
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

	auto inp = ImageInput::open (image_file);
	if (! inp)
	{
		std::cerr << "Could not open " << image_file
				<< ", error = " << OIIO::geterror() << "\n";
		return;
	}

	pixels = new precision[ image_width
							* image_height
							* len];

	if (! inp->read_image( subimage,
							mip,
							begin,
							begin + len,
							TypeDesc::PRECISION,
							&pixels[0]))
	{
		std::cerr << "Could not read pixels from " << image_file
				<< ", error = " << inp->geterror() << "\n";
		return;
	}
	if (! inp->close ())
	{
		std::cerr << "Error closing " << image_file
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

	glTexImage2D(GL_TEXTURE_2D, 0, internal_format[len],
					image_width, image_height, 0,
					data_format[len], PRECISION_GL,
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

	fragment_shader_code = R"glsl(#version 330 core
		out vec4 FragColor;
		in vec2 texCoords;
		uniform sampler2D textureSampler;
		uniform float gain;
		uniform int soloing;
	)glsl" +
	std::string(shaderDesc->getShaderText()) +
	R"glsl(
		void main() {
			FragColor = vec4(0.0);
			vec4 fragment = texture(textureSampler, texCoords.xy);
			for (int i=0; i<4; i++){
				if (isnan(fragment[i]))
					fragment[i]=0.0;
			}
			fragment *= gain;
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

	addShader(shader, vertex_shader_code.c_str(), GL_VERTEX_SHADER);
	addShader(shader, fragment_shader_code.c_str(), GL_FRAGMENT_SHADER);

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

	addShader(frameShader, vertex_shader_code.c_str(), GL_VERTEX_SHADER);
	addShader(frameShader, frame_fragment_shader_code.c_str(), GL_FRAGMENT_SHADER);

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

	if (argc > 1)
	{
		NoPlayer viewer(argv[1]);
		viewer.run();
	}

	return 0;
}
