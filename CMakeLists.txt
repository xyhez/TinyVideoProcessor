cmake_minimum_required(VERSION 3.16)

project(modernFFplay VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Multimedia OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Multimedia OpenGLWidgets)


# FFmpeg 随项目发布，统一放在 ${PROJECT_SOURCE_DIR}/Lib/ffmpeg
set(FFMPEG_ROOT        "${CMAKE_CURRENT_SOURCE_DIR}/Lib/ffmpeg")
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR     "${FFMPEG_ROOT}/lib")
set(FFMPEG_BIN_DIR     "${FFMPEG_ROOT}/bin")

# 让编译器找得到头文件（-I）
include_directories(${FFMPEG_INCLUDE_DIR})
# 让链接器找得到 .lib（-L）
link_directories(${FFMPEG_LIB_DIR})


# 显式链接哪些库
set(FFMPEG_LIBS
    ${FFMPEG_LIB_DIR}/avcodec.lib
    ${FFMPEG_LIB_DIR}/avformat.lib
    ${FFMPEG_LIB_DIR}/avutil.lib
    ${FFMPEG_LIB_DIR}/swscale.lib
    ${FFMPEG_LIB_DIR}/swresample.lib
)



set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        decoder.h
        decoder.cpp
        demuxer.h
        demuxer.cpp
        syncmanager.h
        syncmanager.cpp
        videorender.h
        videorender.cpp
        AudioPlayer.h
        AudioPlayer.cpp
        global.h
        global.cpp
        开发文档.txt
        架构指导.md
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(modernFFplay
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET modernFFplay APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(modernFFplay SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(modernFFplay
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(modernFFplay PRIVATE Qt${QT_VERSION_MAJOR}::Widgets ${FFMPEG_LIBS} Qt${QT_VERSION_MAJOR}::Multimedia Qt${QT_VERSION_MAJOR}::OpenGLWidgets)

# 构建后将 FFmpeg DLL 拷贝到可执行文件目录，免去手动复制
if(WIN32)
    file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
    add_custom_command(TARGET modernFFplay POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${FFMPEG_DLLS}
                $<TARGET_FILE_DIR:modernFFplay>
        COMMENT "Copying FFmpeg DLLs to output directory"
    )
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.modernFFplay)
endif()
set_target_properties(modernFFplay PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS modernFFplay
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(modernFFplay)
endif()
