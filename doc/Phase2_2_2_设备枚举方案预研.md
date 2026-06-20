# Phase 2.2.2 摄像头设备枚举方案预研

## 1. 方案对比

### 方案 A：FFmpeg dshow 枚举

**实现方式**：
```bash
ffmpeg -list_devices true -f dshow -i dummy
```

**原理**：
- 调用 `avdevice_list_input_sources()` API
- FFmpeg 内部通过 DirectShow COM API 枚举设备
- 返回设备列表和每个设备的能力

**优点**：
- ✅ 与打开摄像头使用同一套 FFmpeg API，一致性好
- ✅ 无需额外依赖，已有 libavdevice
- ✅ 跨平台（Windows/Linux/macOS），dshow 只是 Windows 实现
- ✅ 可以同时枚举设备和格式选项

**缺点**：
- ❌ FFmpeg API 文档较少
- ❌ 返回的格式选项解析复杂
- ❌ 枚举速度较慢（需要查询每个设备）

**API 示例**：
```cpp
AVDeviceInfoList* deviceList = nullptr;
AVInputFormat* inputFormat = av_find_input_format("dshow");
int ret = avdevice_list_input_sources(inputFormat, nullptr, nullptr, &deviceList);

for (int i = 0; i < deviceList->nb_devices; i++) {
    AVDeviceInfo* device = deviceList->devices[i];
    qDebug() << "Device:" << device->device_name;
    qDebug() << "Description:" << device->device_description;
}

avdevice_free_list_devices(&deviceList);
```

---

### 方案 B：Windows DirectShow COM API

**实现方式**：
- 使用 Windows COM API
- `ICreateDevEnum` 枚举设备类别
- `IEnumMoniker` 遍历设备
- `IBaseFilter` 查询设备能力

**优点**：
- ✅ 原生 Windows API，性能好
- ✅ 可以获取详细的设备信息（友好名称、硬件 ID）
- ✅ 可以查询完整的格式能力
- ✅ 枚举速度快

**缺点**：
- ❌ 只支持 Windows 平台
- ❌ COM 编程复杂，需要处理 COM 初始化、引用计数
- ❌ 代码量大，维护成本高
- ❌ 需要引入 Windows SDK 头文件（dshow.h）

**API 示例**：
```cpp
#include <dshow.h>

HRESULT hr = CoInitialize(nullptr);

ICreateDevEnum* pDevEnum = nullptr;
CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                 IID_ICreateDevEnum, (void**)&pDevEnum);

IEnumMoniker* pEnum = nullptr;
pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);

IMoniker* pMoniker = nullptr;
while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
    IPropertyBag* pPropBag;
    pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);
    
    VARIANT varName;
    VariantInit(&varName);
    pPropBag->Read(L"FriendlyName", &varName, 0);
    // 获取设备名称
    
    VariantClear(&varName);
    pPropBag->Release();
    pMoniker->Release();
}

pEnum->Release();
pDevEnum->Release();
CoUninitialize();
```

---

### 方案 C：Qt Multimedia 枚举

**实现方式**：
```cpp
#include <QMediaDevices>
#include <QCameraDevice>

QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
for (const QCameraDevice& camera : cameras) {
    qDebug() << "ID:" << camera.id();
    qDebug() << "Description:" << camera.description();
}
```

**优点**：
- ✅ Qt API，简单易用
- ✅ 跨平台（Windows/Linux/macOS）
- ✅ 代码量少，维护成本低
- ✅ 与 Qt 生态集成好

**缺点**：
- ❌ 不返回详细的格式选项（分辨率、帧率）
- ❌ 无法查询编码格式（H.264/MJPEG）
- ❌ 设备 ID 可能与 dshow 设备名不一致
- ❌ 需要将 Qt 设备 ID 映射到 dshow 设备名

**问题**：
Qt 返回的 `camera.id()` 是 Qt 内部的设备标识符，不是 dshow 使用的设备名。需要通过某种方式映射：
- Qt ID：`/dev/video0` (Linux) 或 `\\?\usb#...` (Windows)
- dshow 设备名：`HD Pro Webcam C920`

---

## 2. 方案推荐

### 第一版推荐：**方案 A（FFmpeg dshow 枚举）**

**理由**：
1. **一致性最佳**：枚举和打开使用同一套 FFmpeg API
2. **无额外依赖**：已有 libavdevice，无需引入新依赖
3. **跨平台兼容**：未来支持 Linux (v4l2) 或 macOS (avfoundation) 时无需重写
4. **格式查询完整**：可以通过 FFmpeg 查询每个设备支持的格式

**实施计划**：
- 使用 `avdevice_list_input_sources()` 枚举设备列表
- 对每个设备，运行 `ffmpeg -list_options` 或解析 dshow 返回的格式信息
- 构造 `CameraDeviceInfo` 和 `CameraFormatOption` 列表

---

### 备选方案：**方案 B（DirectShow COM）**

**适用场景**：
- 如果 FFmpeg API 枚举性能不足
- 需要更详细的设备硬件信息
- 只关注 Windows 平台

**实施时机**：Phase 2.2 后续优化

---

### 不推荐：**方案 C（Qt Multimedia）**

**原因**：
- 无法获取格式选项（核心需求）
- 设备 ID 映射复杂且不可靠
- 与 FFmpeg dshow 打开方式不兼容

---

## 3. 实施细节

### 3.1 设备枚举实现

```cpp
QList<CameraDeviceInfo> CameraDeviceEnumerator::EnumerateDevices()
{
    QList<CameraDeviceInfo> devices;
    
    AVDeviceInfoList* deviceList = nullptr;
    AVInputFormat* inputFormat = av_find_input_format("dshow");
    
    if (!inputFormat) {
        qWarning() << "dshow input format not found";
        return devices;
    }
    
    int ret = avdevice_list_input_sources(inputFormat, nullptr, nullptr, &deviceList);
    if (ret < 0 || !deviceList) {
        qWarning() << "Failed to enumerate devices";
        return devices;
    }
    
    for (int i = 0; i < deviceList->nb_devices; i++) {
        AVDeviceInfo* device = deviceList->devices[i];
        
        CameraDeviceInfo info;
        info.deviceName = QString::fromUtf8(device->device_name);
        info.friendlyName = QString::fromUtf8(device->device_description);
        
        // 查询设备格式选项（下一步实现）
        info.formats = EnumerateDeviceFormats(info.deviceName);
        
        devices.append(info);
    }
    
    avdevice_free_list_devices(&deviceList);
    return devices;
}
```

### 3.2 格式枚举实现

格式枚举有两种方式：

**方式 1：解析 FFmpeg 命令输出**
```cpp
QProcess process;
process.start("ffmpeg", {"-list_options", "true", "-f", "dshow", 
                         "-i", QString("video=%1").arg(deviceName)});
process.waitForFinished();
QString output = process.readAllStandardError();
// 解析输出获取分辨率、帧率、编码格式
```

**方式 2：使用 FFmpeg API（推荐）**
- 打开设备但不读取数据
- 查询 `AVFormatContext` 的格式能力
- 关闭设备

### 3.3 架构设计

```
CameraDeviceEnumerator (基础设施层)
  ├── EnumerateDevices() -> QList<CameraDeviceInfo>
  ├── EnumerateDeviceFormats(deviceName) -> QList<CameraFormatOption>
  └── [不创建 MediaPipeline，不打开播放流]
```

---

## 4. 已知限制

1. **首次枚举较慢**：FFmpeg 需要查询每个设备的能力，可能需要 1-3 秒
2. **格式信息不完整**：部分摄像头可能不返回完整的格式列表
3. **设备热插拔**：首次枚举后不会自动更新，需要重新枚举

---

## 5. 后续优化方向

- **缓存枚举结果**：避免重复枚举
- **异步枚举**：避免阻塞 UI 线程
- **设备变化监听**：Windows `WM_DEVICECHANGE` 消息
- **格式选项排序**：按常用度排序（1080p30 > 720p30 > ...）

---

## 6. 结论

**第一版采用 FFmpeg dshow 枚举（方案 A）**，理由：
- 与现有 `CameraMediaSource` 一致性最好
- 无需额外依赖
- 跨平台兼容
- 实现复杂度适中

下一步：实现 `CameraDeviceEnumerator` 空壳和 `EnumerateDevices()` 方法。
