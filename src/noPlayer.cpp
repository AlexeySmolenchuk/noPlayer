#include "noPlayer.h"

void dropCallback(GLFWwindow* window, int count, const char** paths)
{
    // for (int i = 0;  i < count;  i++)
    //     std::cout << paths[i] << std::endl;

	// TODO amend async tasks first
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

	std::thread(&NoPlayer::loader, this).detach();
	std::thread(&NoPlayer::loader, this).detach();
};


void
NoPlayer::init(const char* fileName)
{
	imageFileName = fileName;
	if (!scanImageFile())
		return;

	scale = 1.f;
	// With this little offset we can align image and screen pixels for even and odd resolutions
	offsetX = 0.25f; // Offset of viewed image
	offsetY = 0.25f; // Offset of viewed image
	channelSoloing = 0;
	activePlaneIdx = 0;
	activeMIP = 0;

	// Preload
	std::unique_lock<std::mutex> lock(mtx);
	loadingQueue.clear();
	for(auto ip = imagePlanes.rbegin(); ip != imagePlanes.rend(); ++ip)
	{
		for(auto mip = ip->MIPs.rbegin(); mip != ip->MIPs.rend(); ++mip)
		{
			loadingQueue.push_back(&(*mip));
			mip->ready = ImagePlaneData::ISSUED;
		}
	}
}


NoPlayer::~NoPlayer()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(mainWindow);
	glfwTerminate();
}


void NoPlayer::loader()
{
	while (true)
	{
		mtx.lock();
		if (!loadingQueue.empty())
		{
			size_t idx = loadingQueue.size() - 1;
			ImagePlaneData* plane = loadingQueue[idx];

			loadingQueue.erase(loadingQueue.begin() + idx);

			mtx.unlock();

			if (plane->pixels == nullptr)
				plane->load();
		}
		else
		{
			mtx.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

	}
}


void NoPlayer::run()
{
	while (!glfwWindowShouldClose(mainWindow))
	{
		glfwPollEvents();
		draw();

		if (imagePlanes.size())
		{
			ImagePlaneData &plane = imagePlanes[activePlaneIdx].MIPs[activeMIP];

			if ( plane.ready < ImagePlaneData::LOADING_STARTED)
			{
				std::unique_lock<std::mutex> lock(mtx);
				if (loadingQueue.back() != &plane)
				{
					loadingQueue.push_back(&plane);
					plane.ready = ImagePlaneData::ISSUED;
				}
			}
			else
			{
				// we can preload next AOV
				int next = (activePlaneIdx + 1)%imagePlanes.size();
				if ( imagePlanes[next].MIPs[activeMIP].ready == ImagePlaneData::NOT_ISSUED)
				{
					std::unique_lock<std::mutex> lock(mtx);
					loadingQueue.push_back(&imagePlanes[next].MIPs[activeMIP]);
					imagePlanes[next].MIPs[activeMIP].ready = ImagePlaneData::ISSUED;
				}

				// preload next MIP
				int nextMIP = (activeMIP + 1) % imagePlanes[activePlaneIdx].MIPs.size();
				if ( imagePlanes[activePlaneIdx].MIPs[nextMIP].ready == ImagePlaneData::NOT_ISSUED)
				{
					std::unique_lock<std::mutex> lock(mtx);
					loadingQueue.push_back(&imagePlanes[activePlaneIdx].MIPs[nextMIP]);
					imagePlanes[activePlaneIdx].MIPs[nextMIP].ready = ImagePlaneData::ISSUED;
				}
			}

			if ( plane.ready == ImagePlaneData::LOADED)
			{
				plane.generateGlTexture();
				plane.ready = ImagePlaneData::TEXTURE_GENERATED;
			}
		}
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
			ImGui::Text((const char *)glGetString(GL_VERSION));
			ImGui::Text(message.c_str());
			ImGui::PopStyleColor();
			ImGui::End();
		}

		{
			const char* dropImageMsg = "Drop image";
			ImGui::SetNextWindowPos( (ImVec2(displayW, displayH) - ImGui::CalcTextSize(dropImageMsg))/2.f);
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_NoBackground;
			ImGui::Begin( "Hello", nullptr, windowFlags);
			ImGui::Text(dropImageMsg);
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

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Keypad9)))
		activeMIP = std::max(0, activeMIP-1);

	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Keypad3)))
		activeMIP = std::min( plane.nMIPs-1 , activeMIP+1);


	ImagePlaneData &planeData = plane.MIPs[activeMIP];
	float compensateMIP = powf(2.0f, planeData.mip);

	static int lag = 0;
	static float targetScale = scale;
	static float targetOffsetX = offsetX;
	static float targetOffsetY = offsetY;

	// Zoom by scrolling
	if (!io.WantCaptureMouse && io.MouseWheel!=0.0)
	{
		ImVec2 scalePivot = ImGui::GetMousePos() - ImVec2(displayW, displayH)/2.f - ImVec2(offsetX, offsetY);
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

	// Scale with RMB
	// While adjusting zoom values are updated in "shift" and "factor"
	// When finished (RMB released) these values are baked into scale and offsetX offsetY
	static float factor = 1.0;
	static ImVec2 shift(0, 0);
	if (io.MouseDown[1])
	{
		ImVec2 delta = ImGui::GetMousePos() - io.MouseClickedPos[1];

		float drag = (delta.x - delta.y) * 0.01f;
		factor = powf( 2.f, drag / 3.0f);

		ImVec2 scalePivot = io.MouseClickedPos[1] - ImVec2(displayW, displayH)/2.f - ImVec2(offsetX, offsetY);
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
		if (scale == 1.0/compensateMIP)
			scale = std::min(float(displayH)/float(planeData.windowHeight), float(displayW)/float(planeData.windowWidth))/compensateMIP;
		else
			scale = 1.0f/compensateMIP;
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

		ImGui::SameLine();
		if (mips<=subimages)
			ImGui::Text("[No MIPs]");
		else
			ImGui::Text("[%d MIPs]", mips);



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

			const ImVec4 clrs[] = {
				ImVec4(0.35, 0.35, 0.35, 1),
				ImVec4(0.2, 0.2, 0.2, 1),
				ImVec4(0.65, 0.65, 0.65, 1),
				ImVec4(0.5, 0.5, 0.5, 1),
				ImVec4(0.5, 0.5, 0.5, 1)
				};
			ImGui::TextColored( clrs[imagePlanes[n].MIPs[activeMIP].ready], "%5s", imagePlanes[n].MIPs[activeMIP].format.c_str());
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
				if (channelSoloing==0)
				{
					ImGui::Text(plane.channels.c_str());
				}
				else
				{
					for (int i = 0; i < planeData.len; i++)
					{
						if (i) ImGui::SameLine(0, 0);
						ImGui::TextColored( ((i+1)==channelSoloing) ? ImVec4(1,1,1,1) : ImVec4(0.5,0.5,0.5,1), plane.channels.substr(i, 1).c_str());
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

			ImGui::SetNextWindowPos(ImVec2(displayW/2 - 220, displayH - 35));
			ImGui::SetNextWindowSize(ImVec2(150, 0));

			ImGui::Begin( "Gain", nullptr, windowFlags);
			plane.gainValues = std::clamp( plane.gainValues, -100000.f, 100000.f);
			ImGui::DragFloat("Gain", &(plane.gainValues), std::fmax(0.00001f, std::fabs(plane.gainValues)*0.01f), 0, 0, "%g");
			ImGui::End();


			ImGui::SetNextWindowPos(ImVec2(displayW/2 - 70, displayH - 35));
			ImGui::SetNextWindowSize(ImVec2(150, 0));

			ImGui::Begin( "Offset", nullptr, windowFlags);
			plane.offsetValues = std::clamp( plane.offsetValues, -100000.f, 100000.f);
			ImGui::DragFloat("Offset", &(plane.offsetValues), std::fmax(0.0001f, std::fabs(plane.offsetValues)*0.01f), 0, 0, "%g");
			ImGui::End();

			ImGui::SetNextWindowPos(ImVec2(displayW/2 + 90, displayH - 35));
			ImGui::SetNextWindowSize(ImVec2(150, 0));

			ImGui::Begin( "OCIO", nullptr, windowFlags);
			ImGui::Checkbox("OCIO", &(plane.doOCIO));
			ImGui::SameLine();
			ImGui::Checkbox("NaN", &(plane.checkNaN));
			ImGui::End();

			ImGui::PopStyleColor(6);
		}
	}

	if(planeData.ready != ImagePlaneData::TEXTURE_GENERATED)
	{
		const char* message = "Loading...";
		ImGui::SetNextWindowPos( (ImVec2(displayW, displayH) - ImGui::CalcTextSize(message))/2.f);
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
									| ImGuiWindowFlags_NoBackground;
		ImGui::Begin( "Loading", nullptr, windowFlags);
		ImGui::Text(message);
		ImGui::End();
	}

	if(inspect)
	{
		float centerX = planeData.imageOffsetX + planeData.imageWidth * 0.5f
						- planeData.windowOffsetX - planeData.windowWidth * 0.5f;
		float centerY = planeData.imageOffsetY + planeData.imageHeight * 0.5f
						- planeData.windowOffsetY - planeData.windowHeight * 0.5f;

		ImVec2 mousePos = ImGui::GetMousePos();
		ImVec2 coords = mousePos - ImVec2(displayW, displayH)*0.5f - ImVec2(offsetX, offsetY) + shift;
		coords /= ImVec2(planeData.pixelAspect, 1.0) * scale * compensateMIP * factor;
		coords += ImVec2(planeData.imageWidth, planeData.imageHeight)*0.5f - ImVec2(centerX, centerY);

		if(coords.x >= 0 && coords.x < planeData.imageWidth && coords.y >= 0 && coords.y < planeData.imageHeight)
		{
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.4f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

			ImGui::SetNextWindowPos(mousePos + ImVec2((mousePos.x + 128) > displayW ? -100.f : 16.f,
													  (mousePos.y + 128) > displayH ? -96.f : 24.f));
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
										| ImGuiWindowFlags_AlwaysAutoResize;

			ImGui::Begin( "Inspect", nullptr, windowFlags);

			int idx = (int(coords.x) + int(coords.y)*planeData.imageWidth)*planeData.len;

			if(!planeData.windowMatchData)
				ImGui::Text("(%d, %d)", (int)(coords.x + planeData.imageOffsetX - planeData.windowOffsetX),
										(int)(coords.y + planeData.imageOffsetY - planeData.windowOffsetY));
			ImGui::Text("(%d, %d)", (int)(coords.x), (int)(coords.y));

			if (planeData.ready >= ImagePlaneData::LOADED)
				for (int i=0; i<planeData.len; i++)
					ImGui::Text("%s %g",  planeData.channels.substr(i, 1).c_str(), float(planeData.pixels[idx+i]));
			ImGui::End();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
		}
	}

	ImGui::Render();

	// Draw on Main Window Background
	glViewport(0, 0, displayW, displayH);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	if(channelSoloing <= planeData.len)
	{
		glUseProgram(shader);

		float centerX =  planeData.imageOffsetX + planeData.imageWidth * 0.5f
						- planeData.windowOffsetX - planeData.windowWidth * 0.5f;
		float centerY =  planeData.imageOffsetY + planeData.imageHeight * 0.5f
						- planeData.windowOffsetY - planeData.windowHeight * 0.5f;

		glUniform2f(glGetUniformLocation(shader, "offset"),  (offsetX - shift.x + centerX * scale * factor * compensateMIP)/(float)displayW,
															-(offsetY - shift.y + centerY * scale * factor * compensateMIP)/(float)displayH);
		glUniform2f(glGetUniformLocation(shader, "scale"),  scale * factor * compensateMIP * planeData.imageWidth/(float)displayW * planeData.pixelAspect,
															scale * factor * compensateMIP * planeData.imageHeight/(float)displayH);

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

		glDisable(GL_DEPTH_TEST); // prevents framebuffer rectangle from being discarded
		glBindTexture(GL_TEXTURE_2D, planeData.glTexture);
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}

	if (!planeData.windowMatchData || channelSoloing > planeData.len)
	{
		glUseProgram(frameShader);
		glUniform2f(glGetUniformLocation(frameShader, "offset"), (offsetX - shift.x)/(float)displayW,
																-(offsetY - shift.y)/(float)displayH);
		glUniform2f(glGetUniformLocation(frameShader, "scale"), scale * factor * compensateMIP * planeData.windowWidth/(float)displayW * planeData.pixelAspect,
																scale * factor * compensateMIP * planeData.windowHeight/(float)displayH);
		glBindVertexArray(VAO);
		glDrawArrays(GL_LINE_LOOP, 0, 4);
		glBindVertexArray(0);
		glUseProgram(0);
	}

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	glfwSwapBuffers(mainWindow);
}
