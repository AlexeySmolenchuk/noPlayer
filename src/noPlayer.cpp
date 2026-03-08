#include "noPlayer.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <OpenImageIO/imageio.h>
#include <OpenEXR/OpenEXRConfig.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr int SOLO_NONE = 0;
constexpr int SOLO_R = 1;
constexpr int SOLO_G = 2;
constexpr int SOLO_B = 3;
constexpr int SOLO_H = 4;
constexpr int SOLO_S = 5;
constexpr int SOLO_L = 6;
constexpr int SOLO_A = 7;

int clampMipIndexForPlane(const ImagePlane& plane, int mipIndex)
{
	// Keep active mip index in the valid range for the selected plane.
	if (plane.MIPs.empty())
		return 0;
	return std::clamp(mipIndex, 0, static_cast<int>(plane.MIPs.size()) - 1);
}

void releasePlaneTextures(std::vector<ImagePlane>& planes)
{
	// Delete all GPU textures currently owned by plane payloads.
	for (ImagePlane& plane : planes)
	{
		for (ImagePlaneData& mip : plane.MIPs)
		{
			if (mip.glTexture != 0)
			{
				glDeleteTextures(1, &mip.glTexture);
				mip.glTexture = 0;
			}
		}
	}
}

bool isRgbChannels(const std::string& channels)
{
	// Treat both lower and upper case channel labels as RGB.
	if (channels.size() < 3)
		return false;

	auto isChannel = [](char value, char expected)
	{
		return value == expected || value == static_cast<char>(expected - 'a' + 'A');
	};

	return isChannel(channels[0], 'r')
		&& isChannel(channels[1], 'g')
		&& isChannel(channels[2], 'b');
}

bool isHslSolo(int mode)
{
	return mode >= SOLO_H && mode <= SOLO_L;
}

bool canRenderSoloMode(int mode, const ImagePlaneData& plane)
{
	// Guard solo modes against incompatible channel layouts.
	if (mode == SOLO_NONE)
		return true;

	if (mode >= SOLO_R && mode <= SOLO_B)
		return mode <= plane.len;

	if (isHslSolo(mode) && plane.len >= 3 && isRgbChannels(plane.channels))
		return true;

	if (mode == SOLO_A && plane.len >= 4)
		return true;

	return false;
}

void rgbToHsl(float r, float g, float b, float& h, float& s, float& l)
{
	// Convert raw RGB values to normalized hue plus unconstrained S/L.
	auto sanitize = [](float value)
	{
		if (!std::isfinite(value))
			return 0.0f;
		return value;
	};

	r = sanitize(r);
	g = sanitize(g);
	b = sanitize(b);

	const float cmax = std::max({r, g, b});
	const float cmin = std::min({r, g, b});
	const float delta = cmax - cmin;

	l = (cmax + cmin) * 0.5f;

	if (delta <= std::numeric_limits<float>::epsilon())
	{
		h = 0.0f;
		s = 0.0f;
		return;
	}

	const float denominator = 1.0f - std::fabs(2.0f * l - 1.0f);
	if (std::fabs(denominator) <= std::numeric_limits<float>::epsilon())
		s = 0.0f;
	else
		s = delta / denominator;

	if (cmax == r)
		h = std::fmod((g - b) / delta, 6.0f);
	else if (cmax == g)
		h = ((b - r) / delta) + 2.0f;
	else
		h = ((r - g) / delta) + 4.0f;

	h /= 6.0f;
	while (h < 0.0f)
		h += 1.0f;
	while (h >= 1.0f)
		h -= 1.0f;
}

bool isPointInsideImage(const ImVec2& coords, const ImagePlaneData& planeData)
{
	// Check if image-space coordinates land inside the data window.
	return coords.x >= 0.0f
		&& coords.x < static_cast<float>(planeData.imageWidth)
		&& coords.y >= 0.0f
		&& coords.y < static_cast<float>(planeData.imageHeight);
}

ImVec2 clampImageCoords(const ImVec2& coords, const ImagePlaneData& planeData)
{
	// Clamp image-space coordinates to the valid sampling domain.
	const float maxX = std::max(0.0f, static_cast<float>(planeData.imageWidth) - 0.0001f);
	const float maxY = std::max(0.0f, static_cast<float>(planeData.imageHeight) - 0.0001f);
	return ImVec2(std::clamp(coords.x, 0.0f, maxX), std::clamp(coords.y, 0.0f, maxY));
}

void getImageSelectionBounds(const ImVec2& start, const ImVec2& end, const ImagePlaneData& planeData,
							int& minX, int& minY, int& maxX, int& maxY)
{
	// Convert drag endpoints into integer pixel bounds.
	if (planeData.imageWidth == 0 || planeData.imageHeight == 0)
	{
		minX = maxX = 0;
		minY = maxY = 0;
		return;
	}

	const float imageMaxX = static_cast<float>(planeData.imageWidth) - 1.0f;
	const float imageMaxY = static_cast<float>(planeData.imageHeight) - 1.0f;

	const float clampedStartX = std::clamp(start.x, 0.0f, imageMaxX);
	const float clampedStartY = std::clamp(start.y, 0.0f, imageMaxY);
	const float clampedEndX = std::clamp(end.x, 0.0f, imageMaxX);
	const float clampedEndY = std::clamp(end.y, 0.0f, imageMaxY);

	minX = std::max(0, static_cast<int>(std::floor(std::min(clampedStartX, clampedEndX))));
	minY = std::max(0, static_cast<int>(std::floor(std::min(clampedStartY, clampedEndY))));
	maxX = std::min(static_cast<int>(planeData.imageWidth) - 1, static_cast<int>(std::floor(std::max(clampedStartX, clampedEndX))));
	maxY = std::min(static_cast<int>(planeData.imageHeight) - 1, static_cast<int>(std::floor(std::max(clampedStartY, clampedEndY))));

	maxX = std::max(maxX, minX);
	maxY = std::max(maxY, minY);
}
}

void dropCallback(GLFWwindow* window, int count, const char** paths)
{
	// Replace current image with the first dropped path.
	NoPlayer *view = static_cast<NoPlayer*>(glfwGetWindowUserPointer(window));
	view->clear();
	view->init(paths[0]);
}


void framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	// Trigger redraw when framebuffer size changes.
	NoPlayer *view = static_cast<NoPlayer*>(glfwGetWindowUserPointer(window));
	view->draw();
}


GLFWmonitor* getCurrentMonitor(GLFWwindow *window)
{
	// Select monitor with the largest overlap to current window bounds.
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
	// Toggle fullscreen on F11.
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
					if (!monitor)
						return;

					int _x, _y ,_w ,_h;
					glfwGetMonitorWorkarea(monitor, &_x, &_y, &_w, &_h);
					glfwSetWindowPos(mainWindow, _x, _y);

					const GLFWvidmode* mode = glfwGetVideoMode(monitor);
					if (!mode)
						return;
					glfwSetWindowSize(mainWindow, mode->width, mode->height);
					fullScreen = true;
				}
			glfwSwapBuffers(mainWindow);
		}
	}

	if ( glfwGetKey( mainWindow, GLFW_KEY_F5 ) == GLFW_PRESS )
	{
		// Reload current file while preserving per-plane state where possible.
		NoPlayer *view = static_cast<NoPlayer*>(glfwGetWindowUserPointer(mainWindow));
		std::string currentFileName = view->getFileName();
		if (!currentFileName.empty())
		{
			view->clear();
			view->init(currentFileName.c_str(), false);
		}
	}

	if ( glfwGetKey( mainWindow, GLFW_KEY_ESCAPE ) == GLFW_PRESS )
		glfwSetWindowShouldClose(mainWindow, GL_TRUE);
}


NoPlayer::NoPlayer()
{
	// Initialize windowing and OpenGL context.
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

	mainWindow = glfwCreateWindow(1280, 720, "noPlayer ", nullptr, nullptr);
	if (!mainWindow)
	{
		std::cout << "GLFW creation failed!\n";
		glfwTerminate();
		std::exit(1);
	}

	// Wire GLFW callbacks to the NoPlayer instance.
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

	// Initialize ImGui backends for GLFW/OpenGL.
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = NULL;
	io.LogFilename = NULL;

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	// Build rendering resources and OCIO shader source.
	configureOCIO();

	createPlane();
	createShaders();

	// Configure shared OIIO cache used by async loaders.
	using namespace OIIO;
	cache = ImageCache::create ();
	cache->attribute ("max_memory_MB", 8000.0f);
	cache->attribute ("autotile", 64);

	// Start background loading worker.
	loaderThread = std::thread(&NoPlayer::loader, this);
};


void
NoPlayer::init(const char* fileName, bool fresh)
{
	// Scan file metadata and rebuild plane model.
	imageFileName = fileName;
	if (!scanImageFile())
		return;

	if (fresh)
	{
		// Reset navigation and selection state for a fresh file load.
		scale = 1.f;
		offsetX = 0.25f;
		offsetY = 0.25f;
		channelSoloing = 0;
		activePlaneIdx = 0;
		activeMIP = 0;
		inspectRegionActive = false;
		inspectRegionDragging = false;
		inspectRegionMoved = false;
	}
	waveformPanel.invalidate();

	std::unique_lock<std::mutex> lock(mtx);
	loadingQueue.clear();
	// Queue all mips so first draw can progressively resolve textures.
	for(auto ip = imagePlanes.rbegin(); ip != imagePlanes.rend(); ++ip)
	{
		for(auto mip = ip->MIPs.rbegin(); mip != ip->MIPs.rend(); ++mip)
		{
			enqueueLoadLocked(&(*mip));
		}
	}
}


void NoPlayer::clear()
{
	std::unique_lock<std::mutex> lock(mtx);

	// Invalidate queued tasks and wait for active loads to finish.
	queueGeneration++;
	loadingQueue.clear();
	while (!textureQueue.empty())
		textureQueue.pop();
	queueCondition.wait(lock, [this]() { return activeLoads == 0; });

	releasePlaneTextures(imagePlanes);
	imagePlanes.clear();
	activePlaneIdx = 0;
	activeMIP = 0;
	inspectRegionActive = false;
	inspectRegionDragging = false;
	inspectRegionMoved = false;
	waveformPanel.invalidate();
}

void NoPlayer::bindOCIOTextures()
{
	// Bind each uploaded OCIO LUT texture to its assigned unit.
	for (const OcioLutTexture& texture : ocioLutTextures)
	{
		glActiveTexture(GL_TEXTURE0 + texture.unit);
		glBindTexture(texture.target, texture.id);
	}
	glActiveTexture(GL_TEXTURE0);
}


void NoPlayer::releaseOCIOTextures()
{
	// Delete uploaded OCIO LUT textures and reset container state.
	for (OcioLutTexture& texture : ocioLutTextures)
	{
		if (texture.id != 0)
		{
			glDeleteTextures(1, &texture.id);
			texture.id = 0;
		}
	}
	ocioLutTextures.clear();
}


NoPlayer::~NoPlayer()
{
	waveformPanel.shutdownWorker();

	// Signal worker shutdown and wait for thread exit.
	{
		std::lock_guard<std::mutex> lock(mtx);
		loaderStop = true;
		loadingQueue.clear();
		queueCondition.notify_all();
	}
	if (loaderThread.joinable())
		loaderThread.join();

	glfwMakeContextCurrent(mainWindow);
	{
		// Free OCIO LUTs and image textures while context is active.
		std::lock_guard<std::mutex> lock(mtx);
		releaseOCIOTextures();
		waveformPanel.releaseGlResources();
		releasePlaneTextures(imagePlanes);
		imagePlanes.clear();
	}

	if (shader != 0)
	{
		glDeleteProgram(shader);
		shader = 0;
	}
	if (frameShader != 0)
	{
		glDeleteProgram(frameShader);
		frameShader = 0;
	}
	if (VBO != 0)
	{
		glDeleteBuffers(1, &VBO);
		VBO = 0;
	}
	if (VAO != 0)
	{
		glDeleteVertexArrays(1, &VAO);
		VAO = 0;
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(mainWindow);
	glfwTerminate();
}


void NoPlayer::enqueueLoadLocked(ImagePlaneData* plane)
{
	// Skip invalid, duplicate, or already-issued tasks.
	if (plane == nullptr)
		return;

	if (plane->ready != ImagePlaneData::NOT_ISSUED)
		return;

	for (const LoadTask &task : loadingQueue)
	{
		if (task.generation == queueGeneration && task.plane == plane)
			return;
	}

	loadingQueue.push_back({plane, queueGeneration});
	// Mark as issued and wake worker.
	plane->ready = ImagePlaneData::ISSUED;
	queueCondition.notify_one();
}


void NoPlayer::loader()
{
	// Pop queued work, load pixels, and forward completed planes for GL upload.
	while (true)
	{
		LoadTask task;
		{
			std::unique_lock<std::mutex> lock(mtx);
			queueCondition.wait(lock, [this]()
			{
				return loaderStop || !loadingQueue.empty();
			});

			if (loaderStop && loadingQueue.empty())
				return;

			task = loadingQueue.back();
			loadingQueue.pop_back();

			if (task.plane == nullptr ||
				task.generation != queueGeneration ||
				task.plane->ready != ImagePlaneData::ISSUED)
			{
				continue;
			}

			task.plane->ready = ImagePlaneData::LOADING_STARTED;
			++activeLoads;
		}

		bool loadOk = task.plane->load();

		{
			std::lock_guard<std::mutex> lock(mtx);
			if (!loaderStop && task.generation == queueGeneration && task.plane)
			{
				task.plane->ready = loadOk ? ImagePlaneData::LOADED : ImagePlaneData::NOT_ISSUED;
				if (loadOk)
					textureQueue.push(task);
			}
			--activeLoads;
			queueCondition.notify_all();
		}
	}
}


void NoPlayer::run()
{
	// Poll events, draw frames, and keep preload queues moving.
	while (!glfwWindowShouldClose(mainWindow))
	{
		if (glfwGetWindowAttrib(mainWindow, GLFW_VISIBLE))
		{
			GLFWmonitor* monitor = getCurrentMonitor(mainWindow);
			if (monitor)
				ImGui::GetIO().FontGlobalScale = ImGui_ImplGlfw_GetContentScaleForMonitor(monitor);
			glfwPollEvents();
			draw();
		}

		if (imagePlanes.size())
		{
			activeMIP = clampMipIndexForPlane(imagePlanes[activePlaneIdx], activeMIP);
			ImagePlaneData &plane = imagePlanes[activePlaneIdx].MIPs[activeMIP];

			ImagePlaneData::state planeReady;
			{
				std::lock_guard<std::mutex> lock(mtx);
				planeReady = plane.ready;
			}

			if (planeReady < ImagePlaneData::LOADING_STARTED)
			{
				std::unique_lock<std::mutex> lock(mtx);
				enqueueLoadLocked(&plane);
			}
			else
			{
				std::unique_lock<std::mutex> lock(mtx);

				int next = (activePlaneIdx + 1)%imagePlanes.size();
				int nextMipForPlane = clampMipIndexForPlane(imagePlanes[next], activeMIP);
				if (imagePlanes[next].MIPs[nextMipForPlane].ready == ImagePlaneData::NOT_ISSUED)
				{
					enqueueLoadLocked(&imagePlanes[next].MIPs[nextMipForPlane]);
				}

				int nextMIP = (activeMIP + 1) % imagePlanes[activePlaneIdx].MIPs.size();
				if (imagePlanes[activePlaneIdx].MIPs[nextMIP].ready == ImagePlaneData::NOT_ISSUED)
				{
					enqueueLoadLocked(&imagePlanes[activePlaneIdx].MIPs[nextMIP]);
				}
			}


			LoadTask finishedTask;
			{
				std::lock_guard<std::mutex> lock(mtx);
				if (!textureQueue.empty())
				{
					finishedTask = textureQueue.front();
					textureQueue.pop();
				}
			}
			if (finishedTask.plane)
			{
				bool generated = finishedTask.plane->generateGlTexture();
				std::lock_guard<std::mutex> lock(mtx);
				if (finishedTask.generation == queueGeneration)
				{
					finishedTask.plane->ready = generated ? ImagePlaneData::TEXTURE_GENERATED : ImagePlaneData::NOT_ISSUED;
				}
			}
		}
	}
}


void NoPlayer::draw()
{
	// Start a new ImGui/OpenGL frame.
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();

	glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	int displayW, displayH;
	glfwGetFramebufferSize(mainWindow, &displayW, &displayH);

	ImGuiIO& io = ImGui::GetIO();
	ImGui::NewFrame();
	float unit = ImGui::GetFontSize() * 0.5;

	static bool help = 0;
	help = ImGui::IsKeyDown(ImGuiKey_F1);
	
	if (imagePlanes.size() == 0)
	{
		// Show startup info and drop hint when no image is loaded.
		{
			ImGui::SetNextWindowPos(ImVec2(unit, unit));
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_AlwaysAutoResize
										| ImGuiWindowFlags_NoBackground;
										;
			ImGui::Begin( "Info", nullptr, windowFlags);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1));

			ImGui::Text((const char *)glGetString(GL_VENDOR));
			ImGui::Text((const char *)glGetString(GL_RENDERER));
			ImGui::Text((const char *)glGetString(GL_VERSION));
			ImGui::Text("");

			ImGui::Text("OpenImageIO " OIIO_VERSION_STRING);
			ImGui::Text(OPENEXR_PACKAGE_STRING);

			ImGui::Text(message.c_str());
			ImGui::PopStyleColor();
			ImGui::End();
		}

		{
			const char* dropImageMsg = "Drop image";
			ImGui::SetNextWindowPos( (ImVec2(displayW, displayH) - ImGui::CalcTextSize(dropImageMsg))/2.f);
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_NoBackground
										| ImGuiWindowFlags_AlwaysAutoResize;
			ImGui::Begin( "Drop image", nullptr, windowFlags);
			ImGui::Text(dropImageMsg);
			ImGui::End();
		}

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(mainWindow);
		return;
	}

	ImagePlane &plane = imagePlanes[activePlaneIdx];
	activeMIP = clampMipIndexForPlane(plane, activeMIP);

	ImagePlaneData &planeData = plane.MIPs[activeMIP];
	ImagePlaneData::state planeReady;
	{
		std::lock_guard<std::mutex> lock(mtx);
		planeReady = planeData.ready;
	}
	float compensateMIP = powf(2.0f, planeData.mip);

	static bool ui = true;
	static int lag = 0;
	static float targetScale = scale;
	static float targetOffsetX = offsetX;
	static float targetOffsetY = offsetY;
	static float factor = 1.0;
	static ImVec2 shift(0, 0);

		const int splitterWidthPx = std::max(1, static_cast<int>(std::round(std::max(6.0f, unit))));
		const int splitterLeftPadding = splitterWidthPx / 2;
		const int splitterRightPadding = splitterWidthPx - splitterLeftPadding;
		const int availableSplitWidth = std::max(1, displayW - splitterWidthPx);
		const int minSplitPaneWidth = std::min(std::max(160, static_cast<int>(24.0f * unit)), std::max(1, availableSplitWidth / 2));
		const int minDividerCenterX = minSplitPaneWidth + splitterLeftPadding;
		const int maxDividerCenterX = std::max(minDividerCenterX, displayW - minSplitPaneWidth - splitterRightPadding);
		const int dividerCenterX = waveformSplitView
			? std::clamp(static_cast<int>(std::round(static_cast<float>(displayW) * waveformSplitRatio)),
						minDividerCenterX,
						maxDividerCenterX)
			: 0;
		const int analysisPanelWidth = waveformSplitView ? std::max(1, dividerCenterX - splitterLeftPadding) : 0;
		const int imageViewportX = waveformSplitView ? std::min(displayW - 1, dividerCenterX + splitterRightPadding) : 0;
		const int imageViewportY = 0;
		const int imageViewportW = std::max(1, displayW - imageViewportX);
		const int imageViewportH = std::max(1, displayH);
		const ImVec2 imageViewportCenter(imageViewportX + imageViewportW * 0.5f, imageViewportY + imageViewportH * 0.5f);
		const float imageViewportCenterX = static_cast<float>(imageViewportX) + static_cast<float>(imageViewportW) * 0.5f;
		const float imageViewportRight = static_cast<float>(imageViewportX + imageViewportW);
		const float imageViewportBottom = static_cast<float>(imageViewportY + imageViewportH);

	const float centerX = planeData.imageOffsetX + planeData.imageWidth * 0.5f
						- planeData.windowOffsetX - planeData.windowWidth * 0.5f;
	const float centerY = planeData.imageOffsetY + planeData.imageHeight * 0.5f
						- planeData.windowOffsetY - planeData.windowHeight * 0.5f;

	auto isPointInsideImageViewport = [&](const ImVec2& point)
	{
		return point.x >= imageViewportX
			&& point.x < static_cast<float>(imageViewportX + imageViewportW)
			&& point.y >= imageViewportY
			&& point.y < static_cast<float>(imageViewportY + imageViewportH);
	};

	auto screenToImageCoords = [&](const ImVec2& screenCoords)
	{
		// Map cursor position from screen-space into image-space.
		ImVec2 imageCoords = screenCoords - imageViewportCenter - ImVec2(offsetX, offsetY) + shift;
		imageCoords /= ImVec2(planeData.pixelAspect, 1.0f) * scale * compensateMIP * factor;
		imageCoords += ImVec2(planeData.imageWidth, planeData.imageHeight) * 0.5f - ImVec2(centerX, centerY);
		return imageCoords;
	};

	auto imageToScreenCoords = [&](const ImVec2& imageCoords)
	{
		// Map image-space coordinates back to screen-space for overlays.
		ImVec2 screenCoords = imageCoords - ImVec2(planeData.imageWidth, planeData.imageHeight) * 0.5f + ImVec2(centerX, centerY);
		screenCoords *= ImVec2(planeData.pixelAspect, 1.0f) * scale * compensateMIP * factor;
		screenCoords += imageViewportCenter + ImVec2(offsetX, offsetY) - shift;
		return screenCoords;
	};

	if (!io.WantCaptureMouse)
	{
		const ImVec2 mousePos = ImGui::GetMousePos();
		const bool mouseInsideImageViewport = isPointInsideImageViewport(mousePos);
		const bool ctrlLeftPan = io.KeyCtrl
			&& io.MouseDown[ImGuiMouseButton_Left]
			&& isPointInsideImageViewport(io.MouseClickedPos[ImGuiMouseButton_Left]);
		// Zoom around mouse cursor using wheel input.
		if (io.MouseWheel!=0.0 && mouseInsideImageViewport)
		{
			ImVec2 scalePivot = mousePos - imageViewportCenter - ImVec2(offsetX, offsetY);
			float factor = powf(2, io.MouseWheel/3.0f);
			ImVec2 temp = scalePivot*(factor-1);
			offsetX = offsetX - temp.x;
			offsetY = offsetY - temp.y;

			scale *= powf(2, io.MouseWheel/3.f);
		}

		// Scale around RMB click pivot while dragging.
		if (io.MouseDown[1] && isPointInsideImageViewport(io.MouseClickedPos[1]))
		{
			ImVec2 delta = ImGui::GetMousePos() - io.MouseClickedPos[1];

			float drag = (delta.x - delta.y) * 0.01f;
			factor = powf( 2.f, drag / 3.0f);

			ImVec2 scalePivot = io.MouseClickedPos[1] - imageViewportCenter - ImVec2(offsetX, offsetY);
			shift = scalePivot * (factor - 1);
		}
		// Commit temporary scale transform when RMB is released.
		else if (io.MouseReleased[1])
		{
			scale *= factor;
			offsetX -= shift.x;
			offsetY -= shift.y;

			shift = ImVec2(0, 0);
			factor = 1.0;
		}

		// Pan view with MMB drag.
		if (io.MouseDown[2] && isPointInsideImageViewport(io.MousePos))
		{
			offsetX += ImGui::GetIO().MouseDelta.x;
			offsetY += ImGui::GetIO().MouseDelta.y;
		}

		// Pan view with Ctrl + LMB drag.
		if (ctrlLeftPan)
		{
			offsetX += ImGui::GetIO().MouseDelta.x;
			offsetY += ImGui::GetIO().MouseDelta.y;
		}

		// Start inspector region selection on LMB click inside image.
		if (inspect && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
			&& !io.KeyCtrl
			&& isPointInsideImageViewport(io.MouseClickedPos[ImGuiMouseButton_Left]))
		{
			const ImVec2 clickedCoords = screenToImageCoords(io.MouseClickedPos[ImGuiMouseButton_Left]);
			if (isPointInsideImage(clickedCoords, planeData))
			{
				inspectRegionDragging = true;
				inspectRegionMoved = false;
				inspectRegionStart = clampImageCoords(clickedCoords, planeData);
				inspectRegionEnd = inspectRegionStart;
			}
		}
	}

	if (inspectRegionDragging)
	{
		// Update active region while dragging.
		if (io.MouseDown[ImGuiMouseButton_Left])
		{
			const ImVec2 currentCoords = clampImageCoords(screenToImageCoords(ImGui::GetMousePos()), planeData);
			inspectRegionEnd = currentCoords;
			const float deltaX = std::fabs(inspectRegionEnd.x - inspectRegionStart.x);
			const float deltaY = std::fabs(inspectRegionEnd.y - inspectRegionStart.y);
			inspectRegionMoved = deltaX >= 1.0f || deltaY >= 1.0f;
		}

		// Finalize region selection on LMB release.
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (inspectRegionMoved)
			{
				inspectRegionActive = true;
			}
			else if (inspectRegionActive)
			{
				inspectRegionActive = false;
			}
			inspectRegionDragging = false;
			inspectRegionMoved = false;
		}
	}

	if (!inspect)
	{
		inspectRegionDragging = false;
		inspectRegionMoved = false;
	}

	if (!io.WantCaptureKeyboard)
	{
		// Apply viewer keyboard shortcuts only when UI is not typing.
			if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
			{
				activePlaneIdx = (activePlaneIdx+1)%imagePlanes.size();
				activeMIP = clampMipIndexForPlane(imagePlanes[activePlaneIdx], activeMIP);
				channelSoloing = 0;
			}

			if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
			{
				activePlaneIdx = (imagePlanes.size()+activePlaneIdx-1)%imagePlanes.size();
				activeMIP = clampMipIndexForPlane(imagePlanes[activePlaneIdx], activeMIP);
				channelSoloing = 0;
			}
		if (ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_Keypad9))
			activeMIP = std::max(0, activeMIP-1);

		if (ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_Keypad3))
			activeMIP = std::min( plane.nMIPs-1 , activeMIP+1);

		if (ImGui::IsKeyPressed(ImGuiKey_F))
		{
			offsetX = 0.25;
			offsetY = 0.25;
			if (scale == 1.0/compensateMIP)
				scale = std::min(float(imageViewportH)/float(planeData.windowHeight), float(imageViewportW)/float(planeData.windowWidth))/compensateMIP;
			else
				scale = 1.0f/compensateMIP;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_H))
			ui = !ui;
		if (ImGui::IsKeyPressed(ImGuiKey_O))
			ocioPickerVisible = !ocioPickerVisible;
		if (ImGui::IsKeyPressed(ImGuiKey_W))
		{
			waveformSplitView = !waveformSplitView;
			waveformPanel.invalidate();
		}

		if (ImGui::IsKeyPressed(ImGuiKey_0))
		{
			plane.gainValues = 1.0;
			plane.offsetValues = 0.0;
		}
			
		if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
		{
			targetOffsetX = offsetX * 2;
			targetOffsetY = offsetY * 2;
			targetScale = scale * 2;
			lag = 4;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
		{
			targetOffsetX = offsetX * 0.5f;
			targetOffsetY = offsetY * 0.5f;
			targetScale = scale * 0.5f;
			lag = 4;
		}

		if (lag)
		{
			float f = 1.0f/lag;
			scale = scale * (1.0f-f) + targetScale * f;
			offsetX = offsetX * (1.0f-f) + targetOffsetX * f;
			offsetY = offsetY * (1.0f-f) + targetOffsetY * f;
			lag--;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_Equal))
			plane.gainValues *= 2.0;

		if (ImGui::IsKeyPressed(ImGuiKey_Minus))
			plane.gainValues *= 0.5;

		if (ImGui::IsKeyPressed(ImGuiKey_R))
		{
			if (planeData.len > 0)
			{
				float pixel_min[4], pixel_max[4];
				float min_value = std::numeric_limits<float>::max();
				float max_value = std::numeric_limits<float>::lowest();
				float t;
				planeData.getRange(pixel_min, pixel_max);

				if (channelSoloing >= SOLO_R && channelSoloing <= SOLO_B && channelSoloing <= planeData.len)
				{
					min_value = pixel_min[channelSoloing-1];
					max_value = pixel_max[channelSoloing-1];
				}
				else if (channelSoloing == SOLO_A && planeData.len >= 4)
				{
					min_value = pixel_min[3];
					max_value = pixel_max[3];
				}
				else
				{
					const int rgbChannels = std::max(1, std::min(3, planeData.len));
					for (int i = 0; i < rgbChannels; i++)
					{
						min_value = std::min(min_value, pixel_min[i]);
						max_value = std::max(max_value, pixel_max[i]);
					}
				}

				float d = (max_value - min_value);
				if (d>0.00001)
				{
					t = 1.f/(max_value - min_value);
					plane.gainValues = t;
					plane.offsetValues = -min_value;
				}
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_I))
		{
			inspect = !inspect;
			if (!inspect)
			{
				inspectRegionDragging = false;
				inspectRegionMoved = false;
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent))
			setChannelSoloing(SOLO_NONE);

		if (ImGui::IsKeyPressed(ImGuiKey_1))
			setChannelSoloing(SOLO_R);

		if (ImGui::IsKeyPressed(ImGuiKey_2))
			setChannelSoloing(SOLO_G);

		if (ImGui::IsKeyPressed(ImGuiKey_3))
			setChannelSoloing(SOLO_B);

		if (ImGui::IsKeyPressed(ImGuiKey_4))
			setChannelSoloing(SOLO_H);

		if (ImGui::IsKeyPressed(ImGuiKey_5))
			setChannelSoloing(SOLO_S);

		if (ImGui::IsKeyPressed(ImGuiKey_6))
			setChannelSoloing(SOLO_L);

		if (ImGui::IsKeyPressed(ImGuiKey_7))
			setChannelSoloing(SOLO_A);
	}

	if (help)
	{
		// Show centered shortcut help overlay.

				static const char* helpMsg = 
					"              Shortcuts:\n\n"
					"` 1 2 3 4 5 6 7 (top row) RGB / R / G / B / H / S / L / A\n\n"
				"0 - =         (top row) Exposure Reset / EV- / EV+\n\n"
				"R             Adjust Gain and Offset to fit Range\n\n"
				"[             Previous AOV\n\n"
				"]             Next AOV\n\n"
				"PgUp          Previous MIP\n\n"
				"PgDn          Next MIP\n\n"
				"F             Fit / 100%%\n\n"
				"I             Inspect Tool\n\n"
				"Ctrl+LMB      Pan View\n\n"
				"O             OCIO Display/View picker\n\n"
				"W             Split Waveform View\n\n"
				"F5            Reload image\n\n"
				"F11           Fullscreen\n\n"
				"H             Hide UI\n\n"
				"+ -           (numpad) ZoomIn / ZoomOut\n\n"
				"Esc           Exit\n\n";

			ImGui::SetNextWindowPos( (ImVec2(displayW, displayH) - ImGui::CalcTextSize(helpMsg))/2.f);

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75, 0.75, 0.75, 1));
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 0.75f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_AlwaysAutoResize;

			ImGui::Begin( "Help", nullptr, windowFlags);
			ImGui::Text(helpMsg);
			ImGui::End();

			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar();
	}
	else if (ui)
	{
		// Draw metadata, AOV list, and grading controls.
		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(imageViewportX) + unit, unit));
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
									| ImGuiWindowFlags_AlwaysAutoResize
									| ImGuiWindowFlags_NoBackground;
									;
		ImGui::Begin( "Info", nullptr, windowFlags);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1));
		ImGui::Text(imageFileName.c_str());
		if (subimages>1)
		{
			ImGui::SameLine();
			ImGui::Text("[%d Subimages]", subimages);
		}

		ImGui::SameLine();
		if (mips<=subimages)
			ImGui::Text("[No MIPs]");
		else
			ImGui::Text("[%d MIPs]", mips);

		ImGui::SameLine();
		ImGui::Text(planeData.compression.c_str());

		if ( planeData.quality != -1)
		{
			ImGui::SameLine();
			ImGui::Text("%d", planeData.quality);
		}

		ImGui::SameLine();
		if ( (planeData.tile_width != 0) && (planeData.tile_height != 0))
			ImGui::Text("tiled: %dx%d", planeData.tile_height, planeData.tile_height);
		else
			ImGui::Text("scanline");

		if (planeData.windowMatchData)
		{
			ImGui::Text("%dx%d", planeData.imageWidth, planeData.imageHeight);
		}
		else
		{
			ImGui::Text("Display: %dx%d", planeData.windowWidth, planeData.windowHeight);
			if(planeData.windowOffsetX!=0 || planeData.windowOffsetY!=0)
			{
				ImGui::SameLine();
				ImGui::Text("(%d,%d)", planeData.windowOffsetX, planeData.windowOffsetY);
			}
			ImGui::Text("Data:	%dx%d", planeData.imageWidth, planeData.imageHeight);
			if(planeData.imageOffsetX!=0 || planeData.imageOffsetY!=0)
			{
				ImGui::SameLine();
				ImGui::Text("(%d,%d)", planeData.imageOffsetX, planeData.imageOffsetY);
			}
		}

		if (mips>subimages)
		{
			ImGui::SameLine();
			ImGui::Text("[MIP %d]", planeData.mip);
		}

		if (planeData.pixelAspect != 1.0)
			ImGui::Text("Pixel Aspect: %g", planeData.pixelAspect);

		ImGui::Text("Zoom %.01f%%", scale*factor*compensateMIP*100);
		ImGui::PopStyleColor();
		ImGui::End();


		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0, 1.0, 1.0, 0.2));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0, 1.0, 1.0, 0.1));
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0, 1.0, 1.0, 0.0));

		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(imageViewportX) + unit, unit*14));
		ImGui::SetNextWindowSizeConstraints(ImVec2(-1, 0), ImVec2(-1, imageViewportH - 19*unit));

		windowFlags = ImGuiWindowFlags_None
					| ImGuiWindowFlags_NoDecoration
					| ImGuiWindowFlags_NoNav
					| ImGuiWindowFlags_AlwaysAutoResize
					| ImGuiWindowFlags_NoBackground;

		ImGui::Begin( "AOVs", nullptr, windowFlags);
			for (int n = 0; n < imagePlanes.size(); n++)
			{
				int planeMipIdx = clampMipIndexForPlane(imagePlanes[n], activeMIP);

				const ImVec4 clrs[] = {
					ImVec4(0.35, 0.35, 0.35, 1),
					ImVec4(0.2, 0.2, 0.2, 1),
					ImVec4(0.65, 0.65, 0.65, 1),
					ImVec4(0.5, 0.5, 0.5, 1),
					ImVec4(0.5, 0.5, 0.5, 1)
					};
				ImagePlaneData::state listPlaneReady;
				{
					std::lock_guard<std::mutex> lock(mtx);
					listPlaneReady = imagePlanes[n].MIPs[planeMipIdx].ready;
				}
				ImGui::TextColored( clrs[static_cast<int>(listPlaneReady)], "%5s", imagePlanes[n].MIPs[planeMipIdx].format.c_str());
				ImGui::SameLine();

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
				if (channelSoloing == SOLO_NONE)
				{
					ImGui::Text(plane.channels.c_str());
				}
				else if (isHslSolo(channelSoloing) && planeData.len >= 3 && isRgbChannels(plane.channels))
				{
					const char* hsl = "HSL";
					for (int i = 0; i < 3; i++)
					{
						if (i) ImGui::SameLine(0, 0);
						ImGui::TextColored(((SOLO_H + i) == channelSoloing) ? ImVec4(1,1,1,1) : ImVec4(0.5,0.5,0.5,1),
										"%c", hsl[i]);
					}
				}
				else
				{
						for (int i = 0; i < planeData.len; i++)
						{
							if (i) ImGui::SameLine(0, 0);
							const bool isSelectedChannel = (((i+1) == channelSoloing) && channelSoloing <= SOLO_B)
								|| (i == 3 && channelSoloing == SOLO_A);
							ImGui::TextColored(isSelectedChannel ? ImVec4(1,1,1,1) : ImVec4(0.5,0.5,0.5,1),
										plane.channels.substr(i, 1).c_str());
						}
				}
			}
			else
			{
				ImGui::TextColored( ImVec4(0.5,0.5,0.5, 1), imagePlanes[n].channels.c_str());
			}
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleColor(3);
		ImGui::End();

		if (ocioPickerVisible)
		{
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0, 1.0, 1.0, 0.2));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0, 1.0, 1.0, 0.1));
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0, 1.0, 1.0, 0.0));

			// Anchor picker on right-middle side of the screen.
			const float pickerWidth = 40.0f * unit;
			const float pickerRightMargin = 4.0f * unit;
				ImGui::SetNextWindowPos(ImVec2(imageViewportRight - pickerRightMargin,
											static_cast<float>(imageViewportY) + static_cast<float>(imageViewportH) * 0.5f),
										ImGuiCond_Always,
										ImVec2(1.0f, 0.5f));
				ImGui::SetNextWindowSizeConstraints(ImVec2(pickerWidth, 0), ImVec2(pickerWidth, imageViewportH - 8 * unit));

			windowFlags = ImGuiWindowFlags_None
						| ImGuiWindowFlags_NoDecoration
						| ImGuiWindowFlags_NoNav
						| ImGuiWindowFlags_AlwaysAutoResize
						| ImGuiWindowFlags_NoBackground;

			bool rebuildOcio = false;
			bool selectedDisplayChanged = false;
			std::string nextDisplay = ocioSelectedDisplay;
			std::string nextView = ocioSelectedView;

			ImGui::Begin("OCIO Picker", nullptr, windowFlags);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1.0f));
				ImGui::Text("OCIO");
				if (!ocioConfigSource.empty())
					ImGui::Text("%s", ocioConfigSource.c_str());
				ImGui::Text("Displays");
				for (int index = 0; index < static_cast<int>(ocioDisplays.size()); index++)
				{
					const std::string& displayName = ocioDisplays[index];
					const bool isSelectedDisplay = (ocioSelectedDisplay == displayName);
					if (isSelectedDisplay)
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0, 1.0, 1.0, 1.0f));
					ImGui::PushID(index);
					if (ImGui::Selectable(displayName.c_str(), isSelectedDisplay))
					{
						nextDisplay = displayName;
						selectedDisplayChanged = true;
						rebuildOcio = true;
					}
					ImGui::PopID();
					if (isSelectedDisplay)
						ImGui::PopStyleColor();
				}

				ImGui::Text("");
				ImGui::Text("Views");
				for (int index = 0; index < static_cast<int>(ocioViews.size()); index++)
				{
					const std::string& viewName = ocioViews[index];
					const bool isSelectedView = (ocioSelectedView == viewName);
					if (isSelectedView)
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0, 1.0, 1.0, 1.0f));
					ImGui::PushID(static_cast<int>(ocioDisplays.size()) + index);
					if (ImGui::Selectable(viewName.c_str(), isSelectedView))
					{
						nextView = viewName;
						rebuildOcio = true;
					}
					ImGui::PopID();
					if (isSelectedView)
						ImGui::PopStyleColor();
				}
				ImGui::PopStyleColor();
			ImGui::End();

			ImGui::PopStyleColor(3);

			// Rebuild OCIO shader + LUT resources after user selection changes.
			if (rebuildOcio)
			{
				if (selectedDisplayChanged)
				{
					ocioSelectedDisplay = nextDisplay;
					// Trigger default/first-view selection for the new display.
					ocioSelectedView.clear();
				}
				else
				{
					ocioSelectedDisplay = nextDisplay;
					ocioSelectedView = nextView;
				}
				configureOCIO();
				createShaders();
			}
		}

		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.05f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.1f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.5f, 0.5f, 0.5f, 0.15f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.5f, 0.5f, 0.5f, 0.1f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None
										| ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_NoBackground
										;

				ImGui::SetNextWindowPos(ImVec2(imageViewportCenterX - 34*unit, imageViewportBottom - 5*unit));
			ImGui::SetNextWindowSize(ImVec2(23*unit, 0));

			ImGui::Begin( "Gain", nullptr, windowFlags);
			plane.gainValues = std::clamp( plane.gainValues, -100000.f, 100000.f);
			ImGui::DragFloat("Gain", &(plane.gainValues), std::fmax(0.00001f, std::fabs(plane.gainValues)*0.01f), 0, 0, "%g");
			ImGui::End();


				ImGui::SetNextWindowPos(ImVec2(imageViewportCenterX - 10*unit, imageViewportBottom - 5*unit));
			ImGui::SetNextWindowSize(ImVec2(23*unit, 0));

			ImGui::Begin( "Offset", nullptr, windowFlags);
			plane.offsetValues = std::clamp( plane.offsetValues, -100000.f, 100000.f);
			ImGui::DragFloat("Offset", &(plane.offsetValues), std::fmax(0.0001f, std::fabs(plane.offsetValues)*0.01f), 0, 0, "%g");
			ImGui::End();

				ImGui::SetNextWindowPos(ImVec2(imageViewportCenterX + 14*unit, imageViewportBottom - 5*unit));
			ImGui::SetNextWindowSize(ImVec2(23*unit, 0));

			ImGui::Begin( "OCIO", nullptr, windowFlags);
			ImGui::Checkbox("OCIO", &(plane.doOCIO));
			ImGui::SameLine();
			ImGui::Checkbox("NaN", &(plane.checkNaN));
			ImGui::End();

			ImGui::PopStyleColor(6);
		}
	}

		int waveformSelectionMinX = 0;
		int waveformSelectionMinY = 0;
		int waveformSelectionMaxX = 0;
		int waveformSelectionMaxY = 0;
		const bool waveformSelectionActive = inspect
			&& (inspectRegionActive || (inspectRegionDragging && inspectRegionMoved));
		if (waveformSelectionActive)
		{
			getImageSelectionBounds(inspectRegionStart, inspectRegionEnd, planeData,
									waveformSelectionMinX, waveformSelectionMinY,
									waveformSelectionMaxX, waveformSelectionMaxY);
		}

		const bool waveformSourceReady = planeReady >= ImagePlaneData::LOADED;
		if (waveformSplitView)
		{
			waveformPanel.draw(analysisPanelWidth,
								displayH,
								unit,
								waveformSourceReady,
								waveformSourceReady ? &planeData : nullptr,
								activePlaneIdx,
								activeMIP,
								queueGeneration,
								waveformSelectionActive,
								waveformSelectionMinX,
								waveformSelectionMinY,
								waveformSelectionMaxX,
								waveformSelectionMaxY);

			const float splitterWidth = static_cast<float>(splitterWidthPx);
			const float splitterX = static_cast<float>(analysisPanelWidth);
			ImGui::SetNextWindowPos(ImVec2(splitterX, 0.0f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(splitterWidth, static_cast<float>(displayH)), ImGuiCond_Always);
			ImGuiWindowFlags splitterFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_NoBackground
										| ImGuiWindowFlags_NoMove
										| ImGuiWindowFlags_NoResize
										| ImGuiWindowFlags_NoSavedSettings
										| ImGuiWindowFlags_NoNav;
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin("Waveform Splitter", nullptr, splitterFlags);
			ImGui::InvisibleButton("##waveform_splitter", ImVec2(splitterWidth, static_cast<float>(displayH)));
			const bool splitterHovered = ImGui::IsItemHovered();
			const bool splitterActive = ImGui::IsItemActive();
			if (splitterHovered || splitterActive)
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			if (splitterActive)
			{
				const float dividerMouseX = std::clamp(ImGui::GetIO().MousePos.x,
													static_cast<float>(minDividerCenterX),
													static_cast<float>(maxDividerCenterX));
				waveformSplitRatio = dividerMouseX / static_cast<float>(displayW);
			}
			ImDrawList* splitterDrawList = ImGui::GetWindowDrawList();
			const ImU32 splitterColor = splitterActive
				? IM_COL32(180, 180, 180, 180)
				: (splitterHovered ? IM_COL32(150, 150, 150, 140) : IM_COL32(100, 100, 100, 110));
			splitterDrawList->AddLine(ImVec2(splitterWidth * 0.5f, 0.0f),
										ImVec2(splitterWidth * 0.5f, static_cast<float>(displayH)),
										splitterColor,
										1.5f);
			ImGui::End();
			ImGui::PopStyleVar();
		}

	if (planeReady != ImagePlaneData::TEXTURE_GENERATED)
	{
		// Show loading indicator until texture upload is complete.
		const char* message = "Loading...";
		const ImVec2 imageCenter = ImVec2(static_cast<float>(imageViewportX + imageViewportW * 0.5f),
										static_cast<float>(imageViewportY + imageViewportH * 0.5f));
		ImGui::SetNextWindowPos(imageCenter - ImGui::CalcTextSize(message) * 0.5f);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
									| ImGuiWindowFlags_NoBackground
									| ImGuiWindowFlags_AlwaysAutoResize;
		ImGui::Begin( "Loading", nullptr, windowFlags);
		ImGui::Text(message);
		ImGui::End();
	}

	if (inspect)
	{
		// Draw inspector popup with cursor or region average values.
		const ImVec2 mousePos = ImGui::GetMousePos();
		const ImVec2 coords = screenToImageCoords(mousePos);
		const bool cursorInsideImage = isPointInsideImageViewport(mousePos) && isPointInsideImage(coords, planeData);

		int selectionMinX = 0;
		int selectionMinY = 0;
		int selectionMaxX = 0;
		int selectionMaxY = 0;
		if (inspectRegionActive)
		{
			getImageSelectionBounds(inspectRegionStart, inspectRegionEnd, planeData,
									selectionMinX, selectionMinY, selectionMaxX, selectionMaxY);
		}

		if (cursorInsideImage || inspectRegionActive)
		{
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.4f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

				ImGui::SetNextWindowPos(mousePos + ImVec2((mousePos.x + 20 * unit) > imageViewportRight ? -15 * unit : 3 * unit,
														  (mousePos.y + 20 * unit) > imageViewportBottom ? -15 * unit : 4 * unit));
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_AlwaysAutoResize;

			ImGui::Begin("Inspect", nullptr, windowFlags);

			if (inspectRegionActive)
			{
				const int selectedWidth = selectionMaxX - selectionMinX + 1;
				const int selectedHeight = selectionMaxY - selectionMinY + 1;
				const int displayMinX = selectionMinX + planeData.imageOffsetX;
				const int displayMinY = selectionMinY + planeData.imageOffsetY;
				const int displayMaxX = selectionMaxX + planeData.imageOffsetX;
				const int displayMaxY = selectionMaxY + planeData.imageOffsetY;

				ImGui::Text("Selection: %dx%d", selectedWidth, selectedHeight);
				if (planeData.windowMatchData)
				{
					ImGui::Text("(%d, %d) - (%d, %d)", displayMinX, displayMinY, displayMaxX, displayMaxY);
				}
				else
				{
					ImGui::Text("Display: (%d, %d) - (%d, %d)", displayMinX, displayMinY, displayMaxX, displayMaxY);
					ImGui::Text("Data:    (%d, %d) - (%d, %d)",
								selectionMinX - planeData.windowOffsetX,
								selectionMinY - planeData.windowOffsetY,
								selectionMaxX - planeData.windowOffsetX,
								selectionMaxY - planeData.windowOffsetY);
				}
			}
			else
			{
				const int x = static_cast<int>(coords.x + planeData.imageOffsetX);
				const int y = static_cast<int>(coords.y + planeData.imageOffsetY);

				if (planeData.windowMatchData)
				{
					ImGui::Text("(%d, %d)", x, y);
				}
				else
				{
					ImGui::Text("Display: (%d, %d)", x, y);
					ImGui::Text("Data:    (%d, %d)",
								static_cast<int>(coords.x - planeData.windowOffsetX),
								static_cast<int>(coords.y - planeData.windowOffsetY));
				}
			}

			if (planeReady >= ImagePlaneData::LOADED)
			{
				const int inspectedChannels = std::min(planeData.len, 4);
				float channelValues[4] = {0.0f, 0.0f, 0.0f, 0.0f};
				int sampledPixels = 0;

				if (inspectRegionActive)
				{
					const int selectedWidth = selectionMaxX - selectionMinX + 1;
					const int selectedHeight = selectionMaxY - selectionMinY + 1;
					sampledPixels = selectedWidth * selectedHeight;
					double channelSums[4] = {0.0, 0.0, 0.0, 0.0};

					for (int sampleY = selectionMinY; sampleY <= selectionMaxY; sampleY++)
					{
						const int bufferY = sampleY + planeData.imageOffsetY;
						for (int sampleX = selectionMinX; sampleX <= selectionMaxX; sampleX++)
						{
							const int bufferX = sampleX + planeData.imageOffsetX;
							for (int i = 0; i < inspectedChannels; i++)
								channelSums[i] += planeData.buffer.getchannel(bufferX, bufferY, 0, planeData.begin + i);
						}
					}

					if (sampledPixels > 0)
					{
						for (int i = 0; i < inspectedChannels; i++)
							channelValues[i] = static_cast<float>(channelSums[i] / sampledPixels);
					}

					ImGui::Text("Average over %d px", sampledPixels);
				}
				else if (cursorInsideImage)
				{
					const int x = static_cast<int>(coords.x + planeData.imageOffsetX);
					const int y = static_cast<int>(coords.y + planeData.imageOffsetY);
					sampledPixels = 1;
					for (int i = 0; i < inspectedChannels; i++)
						channelValues[i] = planeData.buffer.getchannel(x, y, 0, planeData.begin + i);
				}

				if (sampledPixels > 0)
				{
					if (inspectedChannels >= 3 && isRgbChannels(planeData.channels))
					{
						float hue, saturation, lightness;
						rgbToHsl(channelValues[0], channelValues[1], channelValues[2], hue, saturation, lightness);
						ImGui::Text("R: %.4f  H:%.4f", channelValues[0], hue);
						ImGui::Text("G: %.4f  S:%.4f", channelValues[1], saturation);
						ImGui::Text("B: %.4f  L:%.4f", channelValues[2], lightness);

						for (int i = 3; i < inspectedChannels; i++)
							ImGui::Text("%s: %.4f", planeData.channels.substr(i, 1).c_str(), channelValues[i]);
					}
					else
					{
						for (int i = 0; i < inspectedChannels; i++)
							ImGui::Text("%s: %.4f", planeData.channels.substr(i, 1).c_str(), channelValues[i]);
					}
				}
			}

			ImGui::End();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
		}

		const bool showSelection = inspectRegionActive || (inspectRegionDragging && inspectRegionMoved);
		if (showSelection)
		{
			// Draw selection rectangle overlay in screen-space.
			int drawMinX = 0;
			int drawMinY = 0;
			int drawMaxX = 0;
			int drawMaxY = 0;
			getImageSelectionBounds(inspectRegionStart, inspectRegionEnd, planeData,
									drawMinX, drawMinY, drawMaxX, drawMaxY);

			const ImVec2 p0 = imageToScreenCoords(ImVec2(static_cast<float>(drawMinX), static_cast<float>(drawMinY)));
			const ImVec2 p1 = imageToScreenCoords(ImVec2(static_cast<float>(drawMaxX + 1), static_cast<float>(drawMaxY + 1)));
			const ImVec2 screenMin(std::min(p0.x, p1.x), std::min(p0.y, p1.y));
			const ImVec2 screenMax(std::max(p0.x, p1.x), std::max(p0.y, p1.y));

			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			drawList->PushClipRect(ImVec2(static_cast<float>(imageViewportX), static_cast<float>(imageViewportY)),
								ImVec2(static_cast<float>(imageViewportX + imageViewportW),
										static_cast<float>(imageViewportY + imageViewportH)),
								true);
			drawList->AddRect(screenMin, screenMax, IM_COL32(255, 210, 80, 220), 0.0f, 0, 1.5f);
			drawList->PopClipRect();
		}
	}

	ImGui::Render();

	// Render the image quad and optional display-window frame.
	glViewport(imageViewportX, imageViewportY, imageViewportW, imageViewportH);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	const bool renderSoloMode = canRenderSoloMode(channelSoloing, planeData);
	if(renderSoloMode)
	{
		// Push per-frame uniforms and draw image texture.
		glUseProgram(shader);

		glUniform2f(glGetUniformLocation(shader, "offset"),  (offsetX - shift.x + centerX * scale * factor * compensateMIP)/(float)imageViewportW,
															-(offsetY - shift.y + centerY * scale * factor * compensateMIP)/(float)imageViewportH);
		glUniform2f(glGetUniformLocation(shader, "scale"),  scale * factor * compensateMIP * planeData.imageWidth/(float)imageViewportW * planeData.pixelAspect,
															scale * factor * compensateMIP * planeData.imageHeight/(float)imageViewportH);

		glUniform1f(glGetUniformLocation(shader, "gainValues"), plane.gainValues);
		glUniform1f(glGetUniformLocation(shader, "offsetValues"), plane.offsetValues);
		glUniform1i(glGetUniformLocation(shader, "soloing"), channelSoloing);
		glUniform1i(glGetUniformLocation(shader, "nchannels"), planeData.len);
		glUniform1i(glGetUniformLocation(shader, "doOCIO"), plane.doOCIO);
		glUniform1i(glGetUniformLocation(shader, "checkNaN"), plane.checkNaN);

		static float flash = 0;
		flash += 1.0f/100;
		if (flash>1.0)
			flash = 0;

		glUniform1f(glGetUniformLocation(shader, "flash"), flash);

		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, planeData.glTexture);
		// Bind OCIO LUT textures referenced by the active shader.
		bindOCIOTextures();
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}

	if (!planeData.windowMatchData || !renderSoloMode)
	{
		// Draw display-window outline when needed.
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		glUseProgram(frameShader);
		glUniform2f(glGetUniformLocation(frameShader, "offset"), (offsetX - shift.x)/(float)imageViewportW,
																-(offsetY - shift.y)/(float)imageViewportH);
		glUniform2f(glGetUniformLocation(frameShader, "scale"), scale * factor * compensateMIP * planeData.windowWidth/(float)imageViewportW * planeData.pixelAspect,
																scale * factor * compensateMIP * planeData.windowHeight/(float)imageViewportH);
		glBindVertexArray(VAO);
		glDrawArrays(GL_LINE_LOOP, 0, 4);
		glBindVertexArray(0);
		glUseProgram(0);
	}

	glViewport(0, 0, displayW, displayH);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glfwSwapBuffers(mainWindow);
}
