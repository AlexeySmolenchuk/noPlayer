if(MSVC)
    add_compile_options(
        $<$<CONFIG:>:/MT>
        $<$<CONFIG:Debug>:/MTd>
        $<$<CONFIG:Release>:/MT>
        )
endif()

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
    )

if(MSVC)
    # disable windows console window
    set_target_properties(noPlayer PROPERTIES
        LINK_FLAGS "/ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS"
        )
endif()
