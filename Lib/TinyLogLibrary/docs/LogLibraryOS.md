# 日志系统学习笔记
这份笔记的目标不是把每一行代码都讲完，而是帮助你用**自己以后也能设计一个日志系统**的角度来理解这个项目中的日志模块。

项目里的日志系统主要在：

+ FC/Logging/
+ FC/App/LoggerManager.cpp

可以先记住一句话：  
**Logger 管判断和分发，Recorder 表示一条日志，Sink 管写到哪里，Stylizer 管输出格式。**

---

## 1. 先从最简单的问题开始
我们平时写日志，大概是这样：

```cpp
logging::Info("加载文件成功: {}", fileName);
logging::Error("打开文件失败: {}", reason);
```

或者这样：

```cpp
LOGGING_INFO << "加载完成";
LOGGING_ERROR << "发生错误";
```

表面看只是输出一句话，但一个完整日志系统背后至少要解决几个问题：

1. 这条日志是什么等级？
2. 这条日志是谁打印的？
3. 这条日志发生在什么时间？
4. 这条日志来自哪个文件、哪一行？
5. 当前日志等级允许它输出吗？
6. 它应该输出到控制台，还是文件？
7. 输出格式应该长什么样？
8. 文件太大了怎么办？
9. 多线程同时写日志怎么办？

所以日志系统看起来复杂，是因为它把这些问题拆成了多个模块。

---

## 2. 整体流程
可以先看这个最简流程：

```plain
业务代码
    |
    | logging::Info("hello")
    v
Logging.h 入口函数
    |
    v
Logger 日志器
    |
    | 判断等级
    v
Recorder 一条日志
    |
    v
Sink 输出目标
    |
    v
Stylizer 格式化
    |
    v
控制台 / 日志文件
```

更具体一点：

```plain
logging::Info("加载成功")
    |
    v
DefaultLoggerRaw()->Print(...)
    |
    v
Logger::Print()
    |
    v
判断 ShouldLog(level)
    |
    v
创建 Recorder
    |
    v
Logger::LogItImpl()
    |
    v
遍历所有 Sink
    |
    v
Sink::SinkIt()
    |
    v
Stylizer::Stylize()
    |
    v
写入 std::cout 或日志文件
```

---

## 3. 主要角色
### 3.1 Logging.h：给用户用的入口
文件：

+ FC/Logging/Logging.h

它提供了最常用的接口：

```cpp
logging::Info("xxx");
logging::Warn("xxx");
logging::Error("xxx");
logging::Debug("xxx");
```

例如：

```cpp
template<typename... Args>
inline void Info(fmt::format_string<Args...> fmt, Args&&... args) {
    DefaultLoggerRaw()->Print(LEVEL_INFO, fmt, std::forward<Args>(args)...);
}
```

这段代码的意思是：  
用户调用 `logging::Info(...)`  
→ 拿到默认 Logger  
→ 调用 `Logger::Print(...)`  
→ 把日志等级设为 `LEVEL_INFO`

所以 Logging.h 是门面，用户一般只接触它，不需要直接操作内部类。

---

### 3.2 Recorder：表示一条日志
文件：

+ FC/Logging/Recorder.h

Recorder 可以理解为**一条日志的包裹**。

它里面保存了：

+ 日志内容 payload
+ 日志等级 level
+ 日志器名字 logger_name
+ 时间 time_point
+ 源码位置 file / function / line
+ 线程 ID
+ 颜色范围

简化理解可以写成这样：

```cpp
struct LogRecord {
    LogLevel level;
    std::string message;
    std::string loggerName;
    std::string file;
    int line;
    std::chrono::system_clock::time_point time;
};
```

项目中的 Recorder 比这个复杂，因为它还支持：

+ fmt 格式化写入
+ 流式 `<<` 写入
+ 移动语义
+ 源码位置记录
+ raw 输出

在项目代码中，日志源码位置来自这个宏：

```cpp
#define SOURCE_LOCATION logging::source_location{__FILE__, ([] { return __func__; })(), __LINE__}
```

它会记录：

+ `__FILE__` 当前文件
+ `__func__` 当前函数
+ `__LINE__` 当前行号

所以最终日志可以显示类似：  
`[Application.cpp:88] 程序启动成功`

---

### 3.3 Logger：判断和分发
文件：

+ FC/Logging/Logger.h
+ FC/Logging/Logger.cpp

Logger 是核心。

它主要做三件事：

1. 判断这条日志要不要输出
2. 如果要输出，把日志交给所有 Sink
3. 必要时 Flush

关键逻辑是：

```cpp
inline bool ShouldLog(Level recorder_level)
{
    return recorder_level >= GetLevel();
}
```

也就是说，日志等级不够就直接丢掉。

例如当前 Logger 等级是 Info：

+ Trace     不输出
+ Debug     不输出
+ Info      输出
+ Warn      输出
+ Error     输出
+ Critical  输出

Logger 内部有一个 sink 列表：

```cpp
std::vector<std::shared_ptr<ISink>> m_sinks{};
```

这说明一个 Logger 可以同时输出到多个地方：

```plain
Logger
  |-- 控制台 Sink
  |-- 文件 Sink
  |-- 回调 Sink
```

这就是为什么一条日志可以同时显示在控制台，又写入日志文件。

---

### 3.4 Sink：输出目标
相关文件：

+ FC/Logging/ISink.h
+ FC/Logging/BaseSink.h
+ FC/Logging/Sinks/

Sink 是**日志写到哪里**的抽象。

接口大概是：

```cpp
class ISink
{
public:
    virtual void SinkIt(Recorder&& recorder) = 0;
    virtual void Flush() = 0;
    virtual void SetStylizer(std::unique_ptr<Stylizer> stylizer) = 0;
};
```

常见 Sink：

+ StdOutSink          输出到控制台
+ StdErrSink          输出到错误流
+ BasicFileSink       输出到普通文件
+ RotatingFileSink    输出到滚动日志文件
+ CallbackSink        输出到回调函数
+ ColorSink           彩色控制台输出

以 RotatingFileSink 为例，它负责：

1. 打开日志文件
2. 写入格式化后的日志
3. 判断文件大小是否超过限制
4. 超过后进行日志轮转

日志轮转可以理解为：

```plain
app.log      -> app1.log
app1.log     -> app2.log
app2.log     -> app3.log
超过数量后删除最旧文件
```

在 LoggerManager.cpp 中，默认创建的是：

```cpp
std::make_shared<logging::RotatingFileSinkMT>(
    log_filename,
    1024 * 1024 * 10,
    10
);
```

含义是：

+ 单个日志文件最大 10MB
+ 最多保留 10 个日志文件

---

### 3.5 Stylizer：控制日志格式
文件：

+ FC/Logging/Stylizer.h
+ FC/Logging/Stylizer.cpp

Stylizer 负责把 Recorder 变成最终字符串。

例如：  
`[2026-05-07 10:30:00] [App] [Info] [Application.cpp:88] 程序启动成功`

它的职责不是写文件，也不是判断等级，只负责**长什么样**。

这点很重要。  
如果把格式化逻辑写死在 FileSink 或 ConsoleSink 里面，以后想改格式就很麻烦。

所以项目把格式化单独抽出来：

+ Recorder 负责保存数据
+ Stylizer 负责变成字符串
+ Sink 负责写出去

---

### 3.6 Registry：管理所有 Logger
文件：

+ FC/Logging/Detail/Registry.h
+ FC/Logging/Detail/Registry.cpp

Registry 是全局注册表。

它负责管理：

+ 所有 logger
+ 默认 logger
+ 全局日志等级
+ 全局 flush 等级
+ 全局格式化器
+ 异步日志线程池

可以理解成：

```plain
Registry
  |-- default Logger
  |-- Base Logger
  |-- App Logger
  |-- Render Logger
```

当你调用：

```cpp
logging::GetLogger("App");
```

本质上就是去 Registry 里面查找名字叫 "App" 的 Logger。

---

### 3.7 LoggerManager：应用层初始化
文件：

+ FC/App/LoggerManager.cpp

底层 Logging 模块只是一个通用日志库。  
而 LoggerManager 是 CAD 应用层对日志系统的配置。

它启动时主要做这些事：

1. 清空 default logger 默认 sink
2. 判断是否开启安静模式
3. 如果没有开启安静模式，就添加控制台输出
4. 添加滚动文件输出
5. 注册 Base / App / Render 等模块日志器
6. 根据参数变化调整日志等级

初始化流程：

```plain
Application::Init()
    |
    v
Application::InitConfig()
    |
    v
创建 LoggerManager
    |
    v
LoggerManager::Initialize()
    |-- 添加控制台输出
    |-- 添加日志文件输出
    |-- 注册模块日志器
```

它还支持环境变量：  
`_LOGGING_QUIET=1`

开启后：

+ 控制台不打印日志
+ 文件日志仍然写入

适合自动化测试。

---

## 4. 一条日志完整走一遍
假设业务代码写了：

```cpp
logging::Info("加载文件成功: {}", fileName);
```

大致流程是：

1. 调用 logging::Info
2. 获取默认 Logger
3. 调用 Logger::Print
4. Logger 判断当前等级是否允许 Info 输出
5. 创建 Recorder
6. Recorder 保存日志内容、等级、时间等信息
7. Logger 遍历所有 Sink
8. 每个 Sink 调用 Stylizer 格式化日志
9. Sink 把文本写到控制台或文件

画成图：

```plain
logging::Info(...)
    |
    v
Logger::Print(...)
    |
    v
ShouldLog(Info)?
    |
    |-- 否：丢弃
    |
    |-- 是
         |
         v
       Recorder
         |
         v
       Logger::LogItImpl()
         |
         v
       Sink::SinkIt()
         |
         v
       Stylizer::Stylize()
         |
         v
       std::cout / log file
```

---

## 5. 这个日志系统为什么这么拆
如果是简单项目，可以直接这样写：

```cpp
std::cout << "hello" << std::endl;
```

但是这样很快会遇到问题：

+ 不能控制等级
+ 不能输出到文件
+ 不能记录文件名和行号
+ 不能统一格式
+ 不能多线程安全
+ 不能滚动日志
+ 不能为不同模块创建不同日志器

所以这个项目把日志系统拆成多个角色：

+ Logging.h    提供简单入口
+ Recorder     保存一条日志
+ Logger       判断和分发
+ Sink         输出位置
+ Stylizer     输出格式
+ Registry     全局管理
+ LoggerManager 应用初始化

这样的好处是：

+ 要新增输出位置，只需要新增 Sink
+ 要改变格式，只需要换 Stylizer
+ 要新增模块日志，只需要注册 Logger
+ 要调整等级，只需要改 Logger 的 level

---

## 6. C++ 语法重点解释
下面这些语法在日志系统里很常见，也是理解代码的关键。

### 6.1 可变参数模板
代码中经常看到：

```cpp
template<typename... Args>
inline void Info(fmt::format_string<Args...> fmt, Args&&... args)
{
    DefaultLoggerRaw()->Print(LEVEL_INFO, fmt, std::forward<Args>(args)...);
}
```

这里的 `typename... Args` 表示**任意数量、任意类型的模板参数**。

例如：

```cpp
logging::Info("x = {}", x);
logging::Info("x = {}, y = {}", x, y);
logging::Info("name = {}, age = {}", name, age);
```

参数数量不固定，类型也不固定，所以需要可变参数模板。

简单例子：

```cpp
template<typename... Args>
void printAll(Args... args)
{
    // args 可以是一组任意参数
}
```

调用：  
`printAll(1, "hello", 3.14);`

此时：

+ Args... = int, const char*, double
+ args... = 1, "hello", 3.14

### 6.2 完美转发
代码里有：

```cpp
std::forward<Args>(args)...
```

这是完美转发。  
它的作用是：**尽量保持参数原本的值类别**。

+ 左值传进来，按左值转发
+ 右值传进来，按右值转发

示例：

```cpp
template<typename T>
void wrapper(T&& value)
{
    realFunction(std::forward<T>(value));
}
```

在日志系统中，完美转发用于把用户传参原样交给 fmt 格式化，避免不必要拷贝。

### 6.3 万能引用 / 转发引用
写法：`Args&&... args`  
在模板类型推导中，**T&& 不是单纯右值引用**，是万能引用：

+ 可以接收左值
+ 可以接收右值

这也是日志接口能适配各种参数的核心原因。

### 6.4 移动语义
代码常见：`std::move(recorder)`  
`std::move` 作用：**把对象转换成右值，允许资源移动**，减少拷贝开销。

日志中 Recorder 在 Logger、Sink、Stylizer 之间全程移动传递，提升性能。

### 6.5 右值引用参数
```cpp
virtual void SinkIt(Recorder&& recorder) = 0;
```

`Recorder&&` 接收可移动的日志对象，设计意图：日志移交 Sink 后不再拷贝，提升多线程日志性能。

### 6.6 std::shared_ptr
日志中大量使用：

+ `std::shared_ptr<Logger>`
+ `std::shared_ptr<ISink>`

代表**共享所有权**，同一个 Sink 可以被多个 Logger 共用。

### 6.7 std::unique_ptr
`std::unique_ptr<Stylizer>`  
代表**独占所有权**，一个 Sink 独占一个格式化器，多 Sink 会通过 Clone 复制独立格式化器。

### 6.8 std::atomic
```cpp
std::atomic_int m_level;
std::atomic_int m_flush_level;
```

原子变量，解决多线程下日志等级读写的数据竞争问题，保证线程安全。

### 6.9 模板 Mutex 参数
```cpp
template<typename Mutex>
class BaseSink : public ISink
{
protected:
    Mutex m_mutex;
};
```

衍生两种版本：

+ `BaseSink<std::mutex>` 多线程安全版
+ `BaseSink<Detail::NullMutex>` 单线程空锁版

通过模板参数复用一套代码，区分单/多线程场景。

---

## 7. 如果自己实现，建议从简化版开始
不要一上来就做得和 CAD 一样完整。

第一版只需要这些东西：

+ LogLevel
+ LogRecord
+ Formatter
+ Sink
+ ConsoleSink
+ FileSink
+ Logger
+ Logging.h

目录设计：

```plain
logging/
  LogLevel.h
  LogRecord.h
  Formatter.h
  Sink.h
  ConsoleSink.h
  FileSink.h
  Logger.h
  Logging.h
```

简化版核心代码示例：

```cpp
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

struct LogRecord {
    LogLevel level;
    std::string message;
    const char* file;
    int line;
    std::chrono::system_clock::time_point time;
};

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const LogRecord& record) = 0;
    virtual void flush() = 0;
};

class Logger {
public:
    void log(LogLevel level, LogRecord record) {
        if (level < level_) {
            return;
        }
        for (auto& sink : sinks_) {
            sink->write(record);
        }
    }

private:
    LogLevel level_ = LogLevel::Info;
    std::vector<std::shared_ptr<Sink>> sinks_;
};
```

这就是 CAD 日志系统的核心思想，只是简化了很多。

---

## 8. 学习顺序建议
### 第一阶段：先理解怎么用
+ FC/Logging/Logging.h
+ FC/App/LoggerManager.cpp  
目标：知道业务怎么打日志、项目怎么配置日志。

### 第二阶段：理解主流程
+ FC/Logging/Logger.h
+ FC/Logging/Logger.cpp
+ FC/Logging/Recorder.h  
目标：等级判断、日志封装、流转流程。

### 第三阶段：理解输出
+ FC/Logging/ISink.h
+ FC/Logging/BaseSink.h
+ FC/Logging/Sinks/OStreamSink.h
+ FC/Logging/Sinks/RotatingFileSink.h  
目标：Sink 抽象、控制台/文件输出、滚动日志设计。

### 第四阶段：理解格式化和管理
+ FC/Logging/Stylizer.h
+ FC/Logging/Stylizer.cpp
+ FC/Logging/Detail/Registry.h  
目标：日志格式生成、全局 Logger 管理。

### 第五阶段：再看高级功能
+ FC/Logging/AsyncLogger.h
+ FC/Logging/Detail/ThreadPool.h
+ FC/Logging/Detail/MPMCQueue.h  
目标：理解异步日志，后期再深挖。

---

## 9. 哪些东西可以先跳过
刚开始学习可以暂时跳过：

+ AsyncLogger
+ ThreadPool
+ MPMCQueue
+ ColorSink / WinColorSink / AnsiColorSink
+ PeriodicWorker

先掌握主线链路：  
**Logging.h -> Logger -> Recorder -> Sink -> Stylizer -> 控制台/文件**

---

## 10. 最值得学习的设计思想
1. 对外接口简单，内部实现复杂
2. 一条日志用 Recorder 封装
3. Logger 只做等级判断和分发
4. Sink 只关心输出目标
5. Stylizer 只关心输出格式
6. Registry 负责全局 logger 管理
7. LoggerManager 负责应用启动配置

自己实现日志系统迭代顺序：  
控制台日志 → 文件日志 → 日志等级 → 文件名行号 → 多 Sink → 滚动日志 → 多 Logger → 异步日志

不要一开始就堆砌所有高级功能。

---

## 11. 最后用一句话收束
CAD 的日志系统可以这样理解：  
业务代码只负责说**我要记一条日志**；  
Recorder 负责把这条日志打包；  
Logger 负责判断这条日志该不该输出；  
Sink 负责决定写到哪里；  
Stylizer 负责决定写成什么样；  
LoggerManager 负责在应用启动时把这些东西组装好。

学习重点不是记类的细节，而是**理解职责拆分的设计思路**，看懂流程后再回头啃 C++ 语法特性会轻松很多。



