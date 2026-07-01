# Phase 5.2 OpenCV 依赖接入报告

## 结论

Phase 5.2 可以收口。

当前项目已经具备 OpenCV 依赖接入基础，OpenCV 库随项目放在：

```text
Lib/opencv
```

当前确认信息：

- OpenCV 位于项目 `Lib` 目录下，符合当前 FFmpeg 随项目发布的依赖管理方式。
- OpenCV 使用 VC2019 编译器构建。
- 项目当前使用 VC2019 编译器。
- `CMakeLists.txt` 已经接入 OpenCV include / lib / bin。
- Debug / Release 已分别链接 `opencv_world4100d.lib` / `opencv_world4100.lib`。
- Windows 构建后会把 OpenCV DLL 拷贝到可执行文件目录。

因此，Phase 5 后续可以开始写图像处理抽象和最小 OpenCV 算法代码。

## 当前目录结构

实际检查到的 OpenCV 目录：

```text
Lib/opencv/
    include/
        opencv2/
    lib/
        opencv_world4100.lib
        opencv_world4100d.lib
    bin/
        opencv_world4100.dll
        opencv_world4100d.dll
        opencv_videoio_ffmpeg4100_64.dll
        opencv_videoio_msmf4100_64.dll
        opencv_videoio_msmf4100_64d.dll
```

这说明当前依赖是 OpenCV 4.10.0 的 world 单库形式。

## CMake 接入方式

当前 `CMakeLists.txt` 使用项目内相对路径管理 OpenCV：

```cmake
set(OPENCV_ROOT        "${CMAKE_CURRENT_SOURCE_DIR}/Lib/opencv")
set(OPENCV_INCLUDE_DIR "${OPENCV_ROOT}/include")
set(OPENCV_LIB_DIR     "${OPENCV_ROOT}/lib")
set(OPENCV_BIN_DIR     "${OPENCV_ROOT}/bin")
```

头文件接入：

```cmake
include_directories(${OPENCV_INCLUDE_DIR})
```

库目录接入：

```cmake
link_directories(${OPENCV_LIB_DIR})
```

Debug / Release 链接：

```cmake
set(OPENCV_LIBS
    optimized ${OPENCV_LIB_DIR}/opencv_world4100.lib
    debug ${OPENCV_LIB_DIR}/opencv_world4100d.lib
)
```

目标链接：

```cmake
target_link_libraries(modernFFplay PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ${FFMPEG_LIBS}
    ${OPENCV_LIBS}
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
)
```

构建后 DLL 复制：

```cmake
file(GLOB OPENCV_DLLS "${OPENCV_BIN_DIR}/*.dll")
add_custom_command(TARGET modernFFplay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${OPENCV_DLLS}
            $<TARGET_FILE_DIR:modernFFplay>
    COMMENT "Copying OpenCV DLLs to output directory"
)
```

该方式和当前 FFmpeg 随项目发布的管理方式一致，短期不需要改成全局 `find_package(OpenCV)`。

## 为什么当前不推荐改成 find_package(OpenCV)

当前项目目标是简历项目和本地可复现开发环境，OpenCV 已经放在项目 `Lib` 目录下。

继续使用项目内相对路径的优点：

- 不依赖系统全局 OpenCV 安装。
- 不依赖用户环境变量。
- 和当前 FFmpeg 管理方式一致。
- 便于在不同机器上复现。
- Debug / Release 库可以明确区分。

改成 `find_package(OpenCV)` 的问题：

- 可能找到系统中另一套 OpenCV。
- 可能找到 MinGW / MSVC ABI 不匹配的 OpenCV。
- 需要额外设置 `OpenCV_DIR`。
- 用户环境越复杂，越容易出现错误库被找到的问题。

因此当前推荐保持：

```text
项目内 Lib/opencv 固定依赖路径
```

后续如果项目要对外发布或支持多平台，再考虑更通用的 `find_package(OpenCV)` 方案。

## ABI 判断

当前关键判断：

```text
OpenCV: VC2019
项目:   VC2019
```

结论：

- 编译器 ABI 匹配。
- 当前不需要支持 MinGW OpenCV。
- 当前不建议同时维护 MSVC 和 MinGW 两套 OpenCV。

风险点：

- 如果后续切回 MinGW kit，当前 `opencv_world4100.lib` 不能直接使用。
- 如果 Debug 构建找不到 `opencv_world4100d.dll`，程序启动会失败。
- 如果 Release 构建找不到 `opencv_world4100.dll`，程序启动会失败。

当前 CMake 已经复制 `Lib/opencv/bin/*.dll`，可以降低运行时 DLL 缺失风险。

## Debug / Release 策略

当前链接策略正确：

- Release 链接 `opencv_world4100.lib`。
- Debug 链接 `opencv_world4100d.lib`。

这点很重要，不能让 Debug 链接 Release OpenCV，也不能让 Release 链接 Debug OpenCV。

原因：

- MSVC Debug / Release CRT 可能不一致。
- OpenCV Debug 库通常带 `d` 后缀。
- 混用可能引发链接错误、运行崩溃或堆内存问题。

## 对后续 Phase 5 的影响

OpenCV 依赖基础已经具备，后续可以进入：

```text
Phase 5.3 图像处理抽象空壳
```

下一步可以新增：

- `IImageProcessor`
- `ImageProcessorPipeline`

但仍然不建议立刻接入播放链路。

正确顺序仍应保持：

```text
依赖确认
    -> 抽象空壳
    -> AVFrame / cv::Mat 转换边界
    -> ImageProcessingFrameSink
    -> 基础处理器
    -> UI 接入
```

## 不做内容

本阶段不做：

- 不新增图像处理算法。
- 不修改 `MediaPipeline`。
- 不修改 `VideoFrameScheduler`。
- 不修改 `MultiViewRenderer`。
- 不新增 UI。
- 不引入 `find_package(OpenCV)`。
- 不支持 MinGW OpenCV。

## 残留风险

当前仍有以下风险需要后续人工编译验证：

- `opencv_world4100.lib` / `opencv_world4100d.lib` 是否和当前 MSVC 工具链完全匹配。
- Debug / Release 启动时 DLL 是否都能正确复制到输出目录。
- OpenCV world 库体积较大，会增加构建和发布体积。
- 后续真正使用 `cv::Mat` 后，才会暴露包含路径、命名空间和链接符号层面的实际问题。

这些风险不阻塞 Phase 5.3，但在首次加入 OpenCV 代码后需要用户侧编译验证。

## 验收结论

Phase 5.2 已完成。

当前可以明确：

- 使用项目内 `Lib/opencv`。
- 使用 VC2019 OpenCV。
- 与当前 VC2019 项目 ABI 匹配。
- CMake 已经接入 include / lib / dll copy。
- 暂不需要修改 CMake。
- 可以进入 Phase 5.3 图像处理抽象空壳。
