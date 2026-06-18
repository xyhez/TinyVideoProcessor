# TinyVideoProcessor

<p align="center">
  <strong>A lightweight yet powerful video processing framework</strong>
</p>

<p align="center">
  基于 Qt6 和 FFmpeg 的轻量级视频处理框架<br>
  从简单播放器演进为专业的多路视频处理平台
</p>

<p align="center">
  <a href="#-特性">特性</a> •
  <a href="#-架构设计">架构</a> •
  <a href="#-快速开始">快速开始</a> •
  <a href="#-演进路线">演进路线</a> •
  <a href="#-技术栈">技术栈</a>
</p>

---

## 📖 项目简介

TinyVideoProcessor 是一个渐进式演进的视频处理框架，遵循清晰的架构设计原则，从基础的视频播放器逐步扩展为支持多路视频源、实时图像处理和智能分析的专业平台。

### 设计理念

- **渐进式演进**：每个阶段都是完整可用的版本，不是功能堆砌
- **架构优先**：清晰的分层设计，易于理解和扩展
- **开放封闭**：通过接口抽象支持新功能，无需修改现有代码
- **轻量高效**：精简的代码，优异的性能

---

## ✨ 特性

### 当前版本 (Phase 0 - 基础播放器)

- ✅ 视频播放（本地文件）
- ✅ 音视频同步
- ✅ 硬件加速解码（NVDEC/DXVA2）
- ✅ OpenGL 渲染
- ✅ 播放/暂停控制
- ✅ 音量调节
- ✅ 多线程架构

### 规划中的功能

#### Phase 1: 完善播放器
- [x] 进度条拖动（Seek）
- [x] 播放列表管理
- [x] 播放速度控制
- [x] 快进/快退
- [x] 全屏模式
- [ ] 配置持久化

#### Phase 2: 摄像头支持
- [ ] USB 摄像头采集
- [ ] 设备枚举与选择
- [ ] 分辨率/帧率设置
- [ ] 实时预览

#### Phase 3: 多路视频
- [ ] 多路同时播放
- [ ] 灵活布局（2x2, 3x3, 4x4, 1+5）
- [ ] 独立控制
- [ ] 单路全屏

#### Phase 4: 网络流
- [ ] RTSP 流播放
- [ ] RTMP 流播放
- [ ] HTTP-FLV 支持
- [ ] 断线重连

#### Phase 5-6: 图像处理与分析
- [ ] 实时图像处理（灰度化、二值化、边缘检测）
- [ ] 插件式处理器架构
- [ ] 目标检测与识别
- [ ] 尺寸测量
- [ ] 运动检测与跟踪

---

## 🏗️ 架构设计

### 分层架构

```
┌─────────────────────────────────────────────────┐
│         Presentation Layer (UI 层)              │
│              MainWindow                         │
├─────────────────────────────────────────────────┤
│       Application Layer (应用层)                │
│         MediaPlayerController                   │
├─────────────────────────────────────────────────┤
│          Domain Layer (领域层)                  │
│  ┌──────────────┐  ┌──────────────┐           │
│  │ IMediaSource │  │MediaPipeline │           │
│  │  (抽象接口)   │  │  (播放管线)   │           │
│  └──────────────┘  └──────────────┘           │
├─────────────────────────────────────────────────┤
│     Infrastructure Layer (基础设施层)           │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐          │
│  │ Demuxer │ │ Decoder │ │ Renderer│          │
│  └─────────┘ └─────────┘ └─────────┘          │
└─────────────────────────────────────────────────┘
```

### 核心设计模式

**1. 策略模式 - 媒体源抽象**
```cpp
class IMediaSource {
    virtual bool open() = 0;
    virtual AVFormatContext* getFormatContext() = 0;
};

// 不同的媒体源实现
class FileMediaSource : public IMediaSource { };
class CameraMediaSource : public IMediaSource { };
class NetworkMediaSource : public IMediaSource { };
```

**2. 管道模式 - 处理流程**
```cpp
class MediaPipeline {
    IMediaSource → Demuxer → Decoder → Renderer
};
```

**3. 观察者模式 - 事件通知**
```cpp
// 使用 Qt 信号槽机制
signals:
    void frameReady(AVFrame *frame);
    void error(QString msg);
```

详细架构设计请参考：[架构指导.md](doc/架构指导.md)

---

## 🚀 快速开始

### 环境要求

- **操作系统**：Windows 10/11, Linux, macOS
- **编译器**：
  - Windows: MSVC 2019+ / MinGW
  - Linux: GCC 9+ / Clang 10+
  - macOS: Xcode 12+
- **Qt**: 6.5 或更高版本
- **FFmpeg**: 6.0 或更高版本
- **CMake**: 3.20 或更高版本

### 依赖安装

#### Windows (vcpkg)
```bash
vcpkg install ffmpeg:x64-windows
vcpkg install qt6:x64-windows
```

#### Linux (Ubuntu/Debian)
```bash
sudo apt install qt6-base-dev
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
sudo apt install libavdevice-dev  # 摄像头支持
```

#### macOS (Homebrew)
```bash
brew install qt6
brew install ffmpeg
```

### 编译

```bash
# 克隆仓库
git clone https://github.com/yourname/TinyVideoProcessor.git
cd TinyVideoProcessor

# 创建构建目录
mkdir build && cd build

# 配置
cmake ..

# 编译
cmake --build . --config Release

# 运行
./TinyVideoProcessor
```

### 使用 Qt Creator

1. 打开 Qt Creator
2. 文件 → 打开文件或项目 → 选择 `CMakeLists.txt`
3. 配置构建套件
4. 点击运行

---

## 📁 项目结构

```
TinyVideoProcessor/
├── README.md                     # 项目说明
├── CMakeLists.txt                # CMake 构建配置
├── Lib/
│   └── ffmpeg/                   # 随项目发布的 FFmpeg 依赖
├── doc/
│   ├── 架构指导.md               # 总体架构路线
│   ├── TODO.md                   # 当前阶段任务清单
│   ├── Phase1_*.md               # 播放器完善阶段文档
│   ├── Phase3_0_*.md             # 无音频流支持文档
│   ├── Phase3_1_*.md             # 单路 MediaPipeline 抽取文档
│   └── Phase3_2_*.md             # 多路管理设计文档
└── src/
    ├── ui/
    │   ├── main.cpp              # 程序入口
    │   ├── mainwindow.h/cpp      # 主窗口
    │   └── mainwindow.ui         # UI 设计文件
    ├── application/
    │   └── MediaPlayController.h/cpp
    ├── domain/
    │   ├── IMediaSource.h        # 媒体源抽象接口
    │   ├── FileMediaSource.h/cpp # 文件媒体源
    │   ├── MediaPipeline.h/cpp   # 单路播放管线
    │   └── PlaylistManager.h/cpp # 播放列表管理
    ├── infrastructure/
    │   ├── demuxer.h/cpp         # 解复用器
    │   ├── decoder.h/cpp         # 解码器
    │   ├── audioplayer.h/cpp     # 音频播放
    │   ├── videorender.h/cpp     # 视频渲染
    │   └── syncmanager.h/cpp     # 音视频同步
    └── core/
        └── queue.h               # 线程安全队列
```

---

## 🗺️ 演进路线

| Phase | 状态 | 功能 | 预计时间 |
|-------|------|------|---------|
| Phase 0 | 🚧 进行中 | 架构重构 + 基础播放器 | 1 周 |
| Phase 1 | 📋 计划中 | 完善播放器功能 | 2 周 |
| Phase 2 | 📋 计划中 | 摄像头支持 | 1.5 周 |
| Phase 3 | 📋 计划中 | 多路视频 | 2 周 |
| Phase 4 | 📋 计划中 | 网络流支持 | 1 周 |
| Phase 5 | 📋 计划中 | 图像处理基础 | 2 周 |
| Phase 6 | 📋 计划中 | 高级图像分析 | 2 周 |

详细规划请参考：[架构指导.md](doc/架构指导.md)

---

## 🛠️ 技术栈

### 核心技术

- **Qt 6**: 跨平台 UI 框架
- **FFmpeg**: 多媒体处理库
  - libavformat: 封装格式处理
  - libavcodec: 编解码
  - libavutil: 工具函数
  - libswscale: 图像缩放转换
  - libavdevice: 设备输入（摄像头）
- **OpenGL**: 硬件加速渲染

### 未来扩展

- **OpenCV**: 图像处理与计算机视觉（Phase 5+）
- **SQLite**: 数据存储（Phase 7）
- **QXlsx**: Excel 报表生成（Phase 7）

---

## 📊 性能指标

### 当前版本

- **视频解码**: 1080P@30fps
- **CPU 占用**: < 15%（硬解）
- **内存占用**: < 150MB
- **音视频同步**: < 50ms

### 目标性能（Phase 3 完成后）

- **多路播放**: 4 路 1080P@30fps
- **CPU 占用**: < 30%
- **内存占用**: < 500MB
- **渲染延迟**: < 100ms

---

## 🤝 贡献指南

欢迎贡献代码、报告问题或提出建议！

### 开发流程

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

### 代码规范

- 遵循 Qt 编码风格
- 使用智能指针管理资源
- 添加必要的注释
- 编写单元测试

---

## 📝 开发日志

### 2026-05-30
- ✅ 创建项目架构指导文档
- ✅ 确定项目名称：TinyVideoProcessor
- 🚧 开始 Phase 0 架构重构

### 历史版本
- **v0.1** (2026-01): 初始版本，基础播放功能
- **v0.2** (2026-03): 添加硬件解码支持
- **v0.3** (2026-05): 多线程架构优化

---

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

---

## 🙏 致谢

- [FFmpeg](https://ffmpeg.org/) - 强大的多媒体框架
- [Qt](https://www.qt.io/) - 优秀的跨平台框架
- [OpenCV](https://opencv.org/) - 计算机视觉库（未来使用）

---

## 🌟 Star History

如果这个项目对你有帮助，请给个 Star ⭐️

---

<p align="center">
  Made with ❤️ by [Cody]
</p>
