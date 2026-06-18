# Phase 3.5 音视频格式归一化预研报告

## 阶段目标

Phase 3.5 的目标是在继续推进 `MultiViewRenderer` 和视频帧输出边界之前，先明确音频输出格式和视频帧格式的归一化策略。

当前已经发现两个实际问题：

1. 部分 AAC 音频播放时出现疑似撕裂、变形或杂音。
2. 4K MKV 视频播放时颜色不对，并且只能看到部分画面。

这两个问题都指向同一个架构事实：

```text
当前管线还没有明确的音视频格式归一化边界。
```

如果不先解决这个问题，后续即使引入 `MultiViewRenderer`，也只是把格式兼容问题集中到新的渲染器里。

## 当前音频输出链路

当前音频链路如下：

```text
decoder
    -> AVFrame
    -> AudioPlayer
    -> SwrContext
    -> S16 PCM
    -> QAudioSink
```

`AudioPlayer::InitAudioPlay()` 会读取音频解码参数：

```cpp
m_dstRate = codecPar->sample_rate;
m_dstChannels = codecPar->ch_layout.nb_channels;
```

随后 `InitAudioOutput()` 用这些参数创建 `QAudioFormat`：

```cpp
fmt.setSampleRate(m_dstRate);
fmt.setChannelCount(m_dstChannels);
fmt.setSampleFormat(QAudioFormat::Int16);
```

如果设备不支持该格式，则 fallback 到设备 preferred format，并更新：

```cpp
m_dstRate = fmt.sampleRate();
m_dstChannels = fmt.channelCount();
```

真正创建 `SwrContext` 的时机在收到第一帧音频时：

```cpp
AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
int outRate = m_dstRate;
AVSampleFormat outFmt = m_dstFormat;
```

## 音频当前风险点

### 1. outLayout 与 m_dstChannels 可能不一致

当前 `SwrContext` 输出声道布局写死为 stereo：

```cpp
AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
```

但 PCM 字节数和 Qt 输出格式使用的是：

```cpp
m_dstChannels
```

当 `m_dstChannels != 2` 时，风险很高。例如：

- 输入是单声道。
- 输入是 5.1。
- 设备 preferred format 返回非 2 声道。
- codecPar 的 channel layout 缺失或不标准。

这会导致：

- 重采样器实际输出 stereo。
- PCM 字节数按另一个声道数计算。
- Qt 音频设备按另一个声道数播放。
- 音频时钟按错误采样帧数推进。

表现可能是杂音、撕裂、速度异常、音画同步异常。

### 2. 44100Hz 本身不是问题

如果音频源就是 44100Hz AAC，将输出采样率设置为 44100Hz 不应该导致声音变形。

日志中的：

```text
44100x54001, 编码=aac
```

应理解为：

```text
sample_rate = 44100
bit_rate = 54001
```

这里的风险不在 44100Hz，而在声道布局、采样格式和输出格式是否一致。

### 3. swr 初始化结果未严格检查

当前 `swr_alloc_set_opts2()` 和 `swr_init()` 没有对返回值做严格处理。

如果输入 frame 的 `ch_layout` 异常，或者输出 layout 与实际参数冲突，重采样初始化失败时可能继续向后走，导致后续播放异常。

### 4. 输入 ch_layout 可能缺失

某些 AAC 或旧文件中，`tmp->ch_layout` 可能不完整。后续需要在初始化重采样器前做兜底：

```text
如果输入 AVChannelLayout 无效，则根据声道数构造默认 layout。
```

## 音频归一化建议

短期建议采用一个明确输出规范：

```text
sample format: S16
channel layout: stereo
sample rate: 优先源采样率；设备不支持时使用设备 preferred sample rate
```

关键要求：

```text
QAudioFormat
SwrContext outLayout
m_dstChannels
pcmBytes
bytesPerSampleFrame
时钟推进计算
```

必须全部使用同一套输出参数。

推荐约束：

- 如果决定输出 stereo，则 `m_dstChannels` 必须固定为 2。
- 如果设备 fallback 到 preferred format，则 `SwrContext` 必须按 fallback 后的声道数和采样率重建。
- `AVChannelLayout outLayout` 应由最终 `m_dstChannels` 推导，而不是写死后再另行计算。
- `swr_alloc_set_opts2()` 和 `swr_init()` 必须检查返回值。
- 输入 layout 无效时必须兜底。

## 当前视频输出链路

当前视频链路如下：

```text
decoder
    -> AVFrame
    -> VFrame_ queue
    -> VideoRender
    -> OpenGL Y/U/V texture
```

软解路径中，decoder 基本直接 `av_frame_ref()` 输出原始解码帧。

硬解路径当前被禁用，但如果启用，硬解帧会经过：

```text
hardware frame
    -> av_hwframe_transfer_data()
    -> NV12
    -> sws_scale()
    -> YUV420P
```

`VideoRender` 当前只支持：

```cpp
AV_PIX_FMT_YUV420P
```

如果帧不是 `YUV420P`，直接返回，不渲染：

```cpp
if (f->format != AV_PIX_FMT_YUV420P) return;
```

## 视频当前风险点

### 1. 软解输出没有统一转成 YUV420P

很多视频软解输出不一定是 `AV_PIX_FMT_YUV420P`。4K MKV 常见输出包括：

- `AV_PIX_FMT_YUV420P10LE`
- `AV_PIX_FMT_P010LE`
- `AV_PIX_FMT_NV12`
- `AV_PIX_FMT_YUV444P`
- `AV_PIX_FMT_YUV422P`

当前 `VideoRender` 不支持这些格式。

### 2. 4K MKV 颜色异常符合当前实现现状

4K MKV，尤其 HEVC/H.265，经常包含：

- 10-bit 视频。
- BT.2020 色彩空间。
- HDR metadata。
- limited range。
- 非 `YUV420P` 像素格式。

当前 shader 是固定 YUV 转 RGB，没有根据 frame 的色彩空间、色彩范围和位深调整转换逻辑。

因此颜色不对是预期风险，不是单纯 MKV 容器问题。

### 3. 只能看到部分画面可能来自格式或纹理上传假设不成立

当前 `VideoRender` 假设：

```text
Y plane: width x height
U plane: width/2 x height/2
V plane: width/2 x height/2
8-bit
YUV420P planar
```

如果实际是 10-bit、NV12、P010 或其它格式，这些假设不成立。错误的纹理尺寸、行跨度或数据解释方式都可能导致：

- 画面只显示一部分。
- 颜色错乱。
- 亮度异常。
- 画面比例或采样异常。

当前虽然使用 `setRowLength(linesize)`，但只在 YUV420P 8-bit 假设成立时可靠。

## 视频归一化建议

短期建议在 decoder 输出到 `VFrame_` 前做统一转换：

```text
任意可支持输入像素格式
    -> sws_scale
    -> AV_PIX_FMT_YUV420P
    -> VideoRender
```

这样可以先保证当前 `VideoRender` 接收到稳定格式。

短期目标不是完美支持 HDR，而是先让常见 SDR 和部分 4K 文件有稳定画面。

推荐约束：

- decoder 对视频输出帧做格式归一化。
- `VideoRender` 暂时继续只消费 `AV_PIX_FMT_YUV420P`。
- 对无法转换的像素格式输出明确日志。
- 保留原始 `pts`、`time_base`、`sample_aspect_ratio`、`color_range`、`colorspace` 等元数据。
- 先不在 shader 中直接支持多像素格式，避免和 `MultiViewRenderer` 设计冲突。

中期在 `MultiViewRenderer` 阶段再考虑：

- 原生支持 NV12 / P010。
- 10-bit 纹理上传。
- BT.709 / BT.2020 矩阵选择。
- limited / full range 处理。
- HDR 到 SDR tone mapping。

## 推荐实施顺序

### 1. 音频输出格式归一化

优先级最高。原因是音频异常会直接影响同步主时钟。

建议步骤：

1. 明确最终输出声道数和 layout。
2. 统一 `QAudioFormat`、`SwrContext`、PCM 字节计算。
3. 检查 `swr_alloc_set_opts2()` 和 `swr_init()` 返回值。
4. 增加输入 layout 兜底。
5. 增加输出格式日志。

验收：

- AAC 44100Hz 文件正常。
- 单声道文件正常。
- 立体声文件正常。
- 设备 fallback 后声音不撕裂。
- 音画同步不退化。

### 2. 视频输出格式归一化

建议步骤：

1. 在 decoder 视频输出侧增加软解帧格式转换。
2. 将非 `YUV420P` 帧转换为 `YUV420P`。
3. 保留关键时间戳和显示元数据。
4. 对转换失败输出日志并丢弃该帧。
5. 用 4K MKV、普通 H.264 MP4、无音频视频分别验证。

验收：

- 普通 H.264 MP4 不退化。
- 4K MKV 至少能完整显示画面。
- 色彩明显错误问题改善。
- seek 后画面正常恢复。
- 多路显示不退化。

### 3. 再进入视频帧输出边界抽象

格式归一化完成后，再推进：

```text
MediaPipeline -> 视频帧输出边界 -> MultiViewRenderer
```

这样 `MultiViewRenderer` 先面对稳定的帧格式，而不是同时处理解码格式兼容问题。

## 不建议的做法

不建议直接做：

- 直接让 `VideoRender` 支持所有像素格式。
- 直接在 `MultiViewRenderer` 阶段处理所有格式兼容。
- 音频输出 layout 写死 stereo，但 PCM 字节数按源声道计算。
- 忽略 `swr_init()` 失败。
- 不保留视频帧时间戳和宽高比元数据。
- 一次性实现 HDR tone mapping。

这些都会扩大问题范围，增加定位难度。

## 预研结论

在继续后续渲染架构工作前，应先完成音视频格式归一化的最小实现。

推荐顺序：

```text
音频输出格式归一化
    -> 视频输出格式归一化
    -> 视频帧输出边界抽象
    -> VideoRender 职责拆分
    -> MultiViewRenderer 空壳
    -> MultiViewRenderer 接真实帧
```

这样可以把“格式兼容问题”和“统一渲染架构问题”拆开处理，避免后续改动变成补丁式修复。
