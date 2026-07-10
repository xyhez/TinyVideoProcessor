# TinyVideoProcessor 单元测试

## 测试框架

项目使用 Qt Test 编写单元测试，并通过 CTest 统一发现和执行。测试默认不参与应用构建，需要显式启用：

```text
-DTINY_VIDEO_BUILD_TESTS=ON
```

## 当前覆盖范围

- `test_sync_queue`：队列顺序、满队列阻塞、等待中断、停止与恢复。
- `test_playlist_manager`：列表增删去重、索引修正、播放模式和信号。
- `test_vision_config_loader`：配置缺失、非法 JSON、默认参数合并、重复 ID 和路径解析。
- `test_vision_pipeline_manager`：管线注册、切换、启动失败和未知 ID。

当前测试只覆盖确定性的纯逻辑模块，不依赖真实摄像头、RTSP 服务、OpenGL 上下文或 YOLO 模型。

## 配置与构建

在已经配置好编译器和 Qt 环境的终端中执行：

```powershell
cmake -S . -B build/unit-tests `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=<Qt安装目录> `
  -DTINY_VIDEO_BUILD_TESTS=ON

cmake --build build/unit-tests --target `
  test_sync_queue `
  test_playlist_manager `
  test_vision_config_loader `
  test_vision_pipeline_manager
```

Windows 使用 MSVC 时，应先进入 Developer PowerShell/Command Prompt，或先执行 Visual Studio 的 `vcvars64.bat`。

## 运行

确保 Qt DLL 目录已经加入 `PATH`，然后执行：

```powershell
ctest --test-dir build/unit-tests --output-on-failure
```

连续执行 10 轮以检查偶发并发问题：

```powershell
ctest --test-dir build/unit-tests --repeat until-fail:10 --output-on-failure
```

CTest 为每个测试目标设置了 15 秒超时，避免队列并发测试失败时无限等待。
