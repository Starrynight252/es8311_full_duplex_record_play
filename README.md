# ES8311 Full Duplex Record & Playback Example

## 简介

这是一个基于 **ES8311 音频编解码器** 和 **SPH0645LM4H-1-8 数字麦克风** 的 Arduino 示例，支持 **32bit PCM** 全双工录音与播放，同时可以播放 SD 卡上的 MP3 文件。  

该示例通过自定义 **GIO 引脚** 配置，实现 I2S RX + TX 全双工功能，适合开发音频应用、语音处理或小型音箱项目。

---

## 功能

- I2S RX + TX **全双工模式**
- 录制 **10 秒 PCM（32bit）** 并回放
- 播放 SD 卡上的 MP3 文件
- 支持 **ES8311 I2C 初始化**
- I2S 标准格式（STD）
- SD 卡存储 PCM 文件（32bit 对齐存储）

---

## 硬件需求

- ESP32/Arduino 兼容开发板
- **ES8311 音频编解码器**
- **SPH0645LM4H-1-8 数字麦克风**
- Micro SD 卡
- 功放模块（可选，用于扬声器输出）
- 连接线与面包板（或自定义 PCB）

---

## 软件依赖

需要以下 Arduino 库：

1. [AudioTools](https://github.com/pschatzmann/arduino-audio-tools)  
2. [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix) （提供 MP3 解码）  
3. [arduino-audio-driver](https://github.com/pschatzmann/arduino-audio-driver) （提供 ES8311 驱动）  

安装方法：

```text
Arduino IDE -> 工具 -> 管理库 -> 搜索以上库并安装
推荐使用PIO 使用git拉取对应库文件到 根目录\lib\
pio就会自动引用。
