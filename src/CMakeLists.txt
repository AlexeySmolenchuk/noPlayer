

add_executable(noPlayer
        noPlayer.cpp
        )

target_link_libraries(noPlayer PRIVATE
        glfw
        GLEW::GLEW
        imgui::imgui
        OpenImageIO::OpenImageIO
        OpenColorIO::OpenColorIO
        )


set_target_properties(noPlayer PROPERTIES
            CXX_STANDARD 17
            CXX_EXTENSIONS OFF
            LINK_FLAGS "/ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS" # disable windows console window
            )

