# Phase 1.2：文件列表管理完善计划

## 目标

在 Phase 1.1 播放控制稳定之后，引入本地文件播放列表能力，让播放器从“单文件播放”推进到“多文件管理和切换”。

Phase 1.2 必须继续遵循 `doc/架构指导.md`：

- `MainWindow` 只负责 UI 交互和事件转发。
- `MediaPlayController` 负责单个媒体源的播放管线控制。
- 播放列表状态不放进 `MainWindow`。
- 文件切换时通过 `IMediaSource` 抽象重新初始化播放管线。
- 不新增全局变量。
- 不让 UI 层直接依赖 demuxer、decoder、AudioPlayer、VideoRender、syncmanager。

## 当前基础

- 文件播放已通过 `FileMediaSource` 接入 `IMediaSource`。
- `MediaPlayController` 已经依赖 `IMediaSource`，不直接依赖文件路径。
- 播放、暂停、Stop、Seek 已逐步收敛到 Controller 编排。
- `MainWindow` 当前仍只需要触发控制命令，不应承担列表数据管理。

## 核心设计

### PlaylistItem

新增播放列表条目结构，保存 UI 和播放切换需要的最小信息：

```cpp
struct PlaylistItem {
    QString filePath;
    QString displayName;
    double durationSeconds = 0.0;
};
```

注意：

- `filePath` 用于创建 `FileMediaSource`。
- `displayName` 用于 UI 展示。
- `durationSeconds` 可以先为空或延迟填充，不要求任务 1 就解析所有文件。

### PlaylistManager

新增应用层或领域层附近的播放列表管理类，建议先放在 `src/application`，因为它承担应用状态编排，不负责 FFmpeg 解析。

建议职责：

```cpp
class PlaylistManager : public QObject {
    Q_OBJECT
public:
    enum class PlayMode {
        Sequential,
        ListLoop,
        SingleLoop
    };

    int Count() const;
    int CurrentIndex() const;
    QList<PlaylistItem> Items() const;
    std::optional<PlaylistItem> CurrentItem() const;

    void AddFile(const QString& filePath);
    void AddFiles(const QStringList& filePaths);
    void RemoveAt(int index);
    void Clear();
    bool SetCurrentIndex(int index);
    bool MoveNext();
    bool MovePrevious();
    void SetPlayMode(PlayMode mode);

signals:
    void PlaylistChanged();
    void CurrentIndexChanged(int index);
    void CurrentItemChanged(const PlaylistItem& item);
    void PlayModeChanged(PlayMode mode);
};
```

第一版可以不做拖拽、不做最近播放、不做持久化，避免 Phase1.2 一开始就扩大范围。

### MediaPlayController 的边界

`MediaPlayController` 不应该拥有整个播放列表。

它只负责当前媒体源：

```cpp
bool Initialize(std::shared_ptr<IMediaSource> mediaSource, QWidget* rendererContainer);
void Start();
void Stop();
void Pause();
void SeekTo(double seconds);
```

当播放列表切换文件时，由应用层协调：

```text
PlaylistManager 选中条目
  -> 创建 FileMediaSource(filePath)
  -> MediaPlayController 释放当前管线
  -> MediaPlayController 初始化新媒体源
  -> 根据用户动作决定是否 Start
```

如果后续发现 `MainWindow` 同时协调 PlaylistManager 和 MediaPlayController 变重，可以再引入更高层的 `PlayerApplicationService` 或 `PlaybackSessionController`。但 Phase1.2 第一版不急着加新抽象。

## 任务拆分

### 任务 1：播放列表数据结构和管理类

- [ ] 新增 `PlaylistItem`。
- [ ] 新增 `PlaylistManager`。
- [ ] 支持 `AddFile()`。
- [ ] 支持 `AddFiles()`。
- [ ] 支持 `RemoveAt()`。
- [ ] 支持 `Clear()`。
- [ ] 支持 `CurrentIndex()` / `CurrentItem()`。
- [ ] 支持 `SetCurrentIndex()`。
- [ ] 增加 `PlaylistChanged()` 和 `CurrentItemChanged()` 信号。
- [ ] 不接 UI，不切换真实播放，只完成列表状态能力。

验收：

- [ ] 添加文件后列表数量正确。
- [ ] 删除当前项后 current index 有明确规则。
- [ ] 清空列表后 current index 归为无效。
- [ ] 不引入 MainWindow 对基础设施层的依赖。

### 任务 2：上一项/下一项选择规则

- [ ] 定义播放模式：顺序播放、列表循环、单曲循环。
- [ ] 实现 `MoveNext()`。
- [ ] 实现 `MovePrevious()`。
- [ ] 明确空列表、单项列表、末尾、开头的行为。
- [ ] 暂不自动播放，只改变当前条目。

验收：

- [ ] 顺序模式到末尾后不越界。
- [ ] 列表循环模式末尾后回到第一项。
- [ ] 单曲循环模式 next/previous 保持当前项。

### 任务 3：文件切换和 Controller 初始化衔接

- [ ] 在应用层建立“当前播放项变化 -> 创建 FileMediaSource -> 初始化 MediaPlayController”的流程。
- [ ] 切换前停止当前播放管线。
- [ ] 切换时释放或重置当前 `MediaPlayController` 资源。
- [ ] 新媒体源初始化失败时保留明确错误状态，不污染列表状态。
- [ ] 切换成功后根据用户动作决定是否播放。

验收：

- [ ] 双击或选择列表项后能切换到对应文件。
- [ ] 切换文件后旧音频、旧画面、旧队列不残留。
- [ ] 切换失败不导致程序崩溃。

### 任务 4：MainWindow 只做 UI 绑定

- [ ] UI 层展示播放列表。
- [ ] 添加文件按钮只负责打开文件选择框并转发路径。
- [ ] 删除按钮只负责转发 index。
- [ ] 双击列表项只转发 index。
- [ ] 上一项/下一项按钮只调用应用层接口。
- [ ] `MainWindow` 不直接创建 demuxer/decoder。
- [ ] `MainWindow` 不直接操作队列。

验收：

- [ ] UI 可以添加、删除、清空文件。
- [ ] UI 可以选择当前播放项。
- [ ] UI 不新增基础设施层 include。

### 任务 5：最近播放记录预留

本任务只做方案，不急着编码。

- [ ] 明确最近播放记录保存哪些字段。
- [ ] 明确保存位置，例如配置文件或 `QSettings`。
- [ ] 明确是否保存上次播放位置。
- [ ] 暂不影响 Phase1.2 主线。

验收：

- [ ] 形成方案后再编码。

## 推荐推进顺序

1. 新增 `PlaylistItem` 和 `PlaylistManager`。
2. 完成添加、删除、清空和当前项选择。
3. 完成上一项/下一项和播放模式规则。
4. 接入文件切换流程。
5. 再接 UI 展示和按钮事件。
6. 最后考虑最近播放记录。

## 不做事项

Phase1.2 暂不处理：

- 摄像头列表。
- 网络流列表。
- 多路同时播放。
- 最近播放持久化实现。
- 拖拽排序。
- 缩略图生成。
- 后台批量解析所有文件时长。

这些能力后续可以在 Phase2、Phase3 或 UI 改进阶段继续扩展。

## 完成标准

- 播放器能管理多个本地文件。
- 可以添加、删除、清空列表。
- 可以切换当前播放文件。
- 上一项/下一项规则明确。
- 文件切换不破坏播放管线稳定性。
- `MainWindow` 仍然只负责 UI 交互。
- 不新增全局变量。
- 不绕过 `IMediaSource` 抽象。
