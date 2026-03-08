if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(noPlayer
    application.cpp
    imagePlane.cpp
    noPlayer.cpp
    waveformPanel.cpp
    scanImageFile.cpp
    configureOCIO.cpp
    oglRoutine.cpp
    
    ${PROJECT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
    ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
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

if(NOPLAYER_INSTALL_RUNTIME_DEPENDENCIES)
    install(TARGETS noPlayer
        RUNTIME_DEPENDENCY_SET noPlayer_RuntimeDependencies
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(WIN32)
        install(RUNTIME_DEPENDENCY_SET noPlayer_RuntimeDependencies
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            PRE_EXCLUDE_REGEXES
                "api-ms-win-.*"
                "ext-ms-win-.*"
            POST_EXCLUDE_REGEXES
                ".*[Ww]indows[\\\\/](System32|WinSxS)[\\\\/].*"
        )
    elseif(UNIX AND NOT APPLE)
        install(RUNTIME_DEPENDENCY_SET noPlayer_RuntimeDependencies
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            POST_EXCLUDE_REGEXES
                "^/lib.*"
                "^/usr/lib.*"
                ".*/ld-linux.*"
                ".*/lib(c|m|pthread|dl|rt)\\.so.*"
                ".*/libgcc_s\\.so.*"
                ".*/libstdc\\+\\+\\.so.*"
        )
    else()
        install(RUNTIME_DEPENDENCY_SET noPlayer_RuntimeDependencies
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
else()
    install(TARGETS noPlayer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
