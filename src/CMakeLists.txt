if(MSVC)
    add_compile_options(
        $<$<CONFIG:>:/MT>
        $<$<CONFIG:Debug>:/MTd>
        $<$<CONFIG:Release>:/MT>
        )
endif()

add_executable(noPlayer
    application.cpp
    imagePlane.cpp
    noPlayer.cpp
    scanImageFile.cpp
    configureOCIO.cpp
    oglRoutine.cpp
    
    ${PROJECT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
    ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_win32.cpp
    )

target_include_directories(noPlayer PRIVATE
    ${PROJECT_SOURCE_DIR}/imgui
    ${PROJECT_SOURCE_DIR}/imgui/backends)

target_link_libraries(noPlayer PRIVATE
    glfw
    GLEW::GLEW
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
