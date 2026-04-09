cmake_minimum_required(VERSION 3.16)
project(M3U8Downloader VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Find FFmpeg executable (optional, can bundle or use system)
find_program(FFMPEG_EXECUTABLE ffmpeg REQUIRED)

set(SOURCES
    main.cpp
    mainwindow.cpp
    ffmpegworker.cpp
)

set(HEADERS
    mainwindow.h
    ffmpegworker.h
)

set(UI_FILES
    mainwindow.ui
)

set(RESOURCES
    resources/icons.qrc
)

# Create executable
add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${RESOURCES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Set target properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# Install rules
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME "M3U8 Downloader")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP;NSIS")  # Windows: ZIP and installer
include(CPack)

# WinDeployQt for Windows
if(WIN32)
    find_program(WINDEPLOYQT windeployqt6)
    if(WINDEPLOYQT)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${WINDEPLOYQT} $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Deploying Qt dependencies..."
        )
    endif()
endif()