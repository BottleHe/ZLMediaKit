# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

ZLMediaKit 是一个基于 C++11 的高性能运营级流媒体服务框架，支持 RTSP/RTMP/HLS/HTTP-FLV/WebSocket-FLV/GB28181/HTTP-TS/WebSocket-TS/HTTP-fMP4/WebSocket-fMP4/MP4/WebRTC 等多种协议。

## 构建命令

### macOS/Linux
```bash
# 创建构建目录
# -G "Unix Makefiles" -DCMAKE_MAKE_PROGRAM=/Applications/CLion.app/Contents/bin/ninja/mac/aarch64/ninja -DCMAKE_INSTALL_PREFIX=/Users/bottle/BinApplication/ZLMediaKit

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Windows
使用 Visual Studio 打开生成的 solution 文件或使用 cmake --build。

### Docker
```bash
bash build_docker_images.sh
```

### CMake 选项
- `-DENABLE_API=ON` - 启用 C API SDK (默认 ON)
- `-DENABLE_SERVER=ON` - 启用 MediaServer (默认 ON)
- `-DENABLE_TESTS=ON` - 启用测试程序 (默认 ON)
- `-DENABLE_WEBRTC=ON` - 启用 WebRTC 支持 (默认 ON)
- `-DENABLE_SRT=ON` - 启用 SRT 支持 (默认 ON)
- `-DENABLE_FFMPEG=ON` - 启用 FFmpeg 拉流代理 (默认 OFF)

### 运行测试程序
测试程序位于 `tests/` 目录，编译后生成在 `release/{os}/{build_type}/` 目录：
- `test_server` - RTSP/RTMP/HTTP 服务器测试
- `test_player` - 带视频渲染的播放器测试
- `test_pusher` - 拉流后再推流的测试客户端
- `test_httpApi` - HTTP API 测试
- `test_bench_*` - 性能测试工具

## 核心架构

### 媒体源 (MediaSource)
核心抽象类 `MediaSource` (src/Common/MediaSource.h) 代表一个媒体源。所有协议（RTSP/RTMP/HLS/WebRTC等）都通过统一的 MediaSource 接口向外提供流。

### 协议层次
```
┌─────────────────────────────────────────┐
│           MediaServer                   │
├─────────────────────────────────────────┤
│  RTSP/RTMP/HLS/HTTP-FLV/WebRTC 等协议  │
├─────────────────────────────────────────┤
│       MediaSource (统一媒体源)           │
├─────────────────────────────────────────┤
│  Track (视频/音频轨道)                   │
├─────────────────────────────────────────┤
│       ZLToolKit (网络/线程/工具库)       │
└─────────────────────────────────────────┘
```

### 关键目录
- `src/Common/` - 核心组件：MediaSource、MediaSink、配置管理、协议选项
- `src/Rtsp/` - RTSP 协议实现 (RtspSession、RtspPlayer、RtspPusher)
- `src/Rtmp/` - RTMP 协议实现 (RtmpSession、播放器、推流器)
- `src/Http/` - HTTP 服务器、WebSocket、HTTP-FLV
- `src/Extension/` - 编解码扩展 (H264/H265/AAC 等 Track)
- `src/Player/` - 通用播放器接口
- `src/Pusher/` - 通用推流器接口
- `src/Record/` - MP4/HLS 录制功能
- `src/Rtp/` - RTP 打包/解包
- `src/Rtcp/` - RTCP 协议
- `src/WebRTC/` - WebRTC 实现 (ICE/DTLS/SCTP/SRTP)
- `src/FMP4/` - fMP4 封装
- `src/TS/` - TS 封装
- `server/` - MediaServer 主程序
- `api/` - C API SDK (api/include/)
- `tests/` - 测试程序
- `3rdpart/ZLToolKit/` - 底层网络库、线程池、日志、配置等工具

### 第三方依赖
- `ZLToolKit` - 底层网络框架 (epoll/kqueue/IOCP)、线程库、SSL、数据库连接池
- `media-server` - ts/fmp4/mp4/ps 容器格式复用解复用
- `jsoncpp` - JSON 处理
- `pybind11` - Python 插件支持

### 配置文件
- `conf/` 或可执行文件同目录的 `ini` 文件
- 默认端口: HTTP 80, HTTPS 443, RTSP 554, RTMP 1935, Shell 9000

## JT/T 1078 协议

JT1078 是基于 RTP 的流媒体传输协议，用于视频监控领域。

### 数据包结构

```
offset 0-3:   帧头标识，固定值：0x30316364 (4 bytes)
offset 4:     V(2bit) + P(1bit) + X(1bit) + CC(4bit)
offset 5:     M(1bit) + PT(7bit)
offset 6-7:   包序号 (2 bytes)
offset 8-13:  SIM卡号 (6 bytes)
offset 14:    逻辑通道号 (1 byte)
offset 15:    数据类型(4bit高) + 分包处理标记(4bit低)
offset 16-23: 时间戳 (8 bytes)
offset 24-25: Last I Frame Interval (2 bytes)
offset 26-27: Last Frame Interval (2 bytes)
offset 28-29: 数据体长度 (2 bytes, big-endian)
offset 30+:   数据体 (n bytes, n <= 950)
```

### 字段说明

| 字段 | 偏移 | 长度 | 说明 |
|------|------|------|------|
| 帧头标识 | 0 | 4 bytes | 固定值 0x30316364 |
| V | 4 | 2 bits | 版本号 |
| P | 4 | 1 bit | 填充标志 |
| X | 4 | 1 bit | 扩展标志 |
| CC | 4 | 4 bits | CSRC 计数 |
| M | 5 | 1 bit | 标记位 |
| PT | 5 | 7 bits | 负载类型 |
| 包序号 | 6-7 | 2 bytes | 序列号 |
| SIM卡号 | 8-13 | 6 bytes | 设备SIM卡号 |
| 逻辑通道号 | 14 | 1 byte | 通道号 |
| 数据类型 | 15 | 4 bits | 00=I帧, 01=P帧, 10=B帧, 11=音频帧 |
| 分包处理标记 | 15 | 4 bits | 00=原子包, 01=第一个分包, 10=最后一个分包, 11=中间分包 |
| 时间戳 | 16-23 | 8 bytes | 时间戳 |
| Last I Frame Interval | 24-25 | 2 bytes | I帧间隔 |
| Last Frame Interval | 26-27 | 2 bytes | 帧间隔 |
| 数据体长度 | 28-29 | 2 bytes | 数据体长度 |
| 数据体 | 30+ | n bytes | 负载数据 |

### 关键说明

1. **变种RTP头**：offset 4-7 是变种RTP头，offset 6-7 是包序号，**不是标准RTP**
3. **端口范围**：配置 `jt1078.port_range` 指定随机端口范围

## 行为准则

用于减少 LLM 编码常见错误的行为指南。与项目特定指令合并使用。

**权衡：** 这些准则偏向谨慎而非速度。对于简单任务，请自行判断。

### 1. 先思考再编码

**不要假设。不要隐藏困惑。主动揭示权衡。**

在实现之前：
- 明确陈述你的假设。如果不确定，就问。
- 如果存在多种理解，把它们都列出来——不要默默地选一个。
- 如果存在更简单的方案，说出来。必要时提出反对意见。
- 如果有不明白的地方，停下来。指出困惑之处，然后提问。

### 2. 简单优先

**用最少的代码解决问题。不做任何推测性的设计。**

- 不添加超出需求的功能。
- 不为只用一次的代码创建抽象。
- 不添加未被要求的"灵活性"或"可配置性"。
- 不为不可能发生的场景写错误处理。
- 如果你写了 200 行而 50 行就够了，那就重写。

问自己："资深工程师会觉得这太复杂了吗？" 如果是，就简化。

### 3. 精准修改

**只改必须改的。只清理自己造成的混乱。**

编辑现有代码时：
- 不要"改进"相邻的代码、注释或格式。
- 不要重构没有问题的东西。
- 匹配现有风格，即使你自己不会这样写。
- 如果你注意到无关的死代码，提一下——但不要删除它。

当你的修改产生了孤立代码时：
- 删除因你的修改而变得未使用的 import/变量/函数。
- 不要删除之前就存在的死代码，除非被要求。

检验标准：每一行改动都应该能追溯到用户的需求。

### 4. 目标驱动执行

**定义成功标准。循环直到验证通过。**

将任务转化为可验证的目标：
- "添加验证" → "为无效输入编写测试，然后让测试通过"
- "修复 bug" → "编写能复现该 bug 的测试，然后让测试通过"
- "重构 X" → "确保重构前后测试都能通过"

对于多步骤任务，陈述简要计划：
```
1. [步骤] → 验证: [检查方式]
2. [步骤] → 验证: [检查方式]
3. [步骤] → 验证: [检查方式]
```

强成功标准让你能独立循环迭代。弱成功标准（"让它能跑就行"）则需要不断确认。

---

**这些准则有效的标志：** diff 中不必要的改动更少，因过度复杂导致的重写更少，澄清问题在实现之前提出而非在犯错之后。