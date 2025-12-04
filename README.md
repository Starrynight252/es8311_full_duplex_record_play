ES8311 Full Duplex Record & Playback Example

简介

这是一个基于 ES8311 音频编解码器 和 SPH0645LM4H-1-8 数字麦克风 的 Arduino 示例，支持 32bit PCM 全双工录音与播放，同时可以播放 SD 卡上的 MP3 文件。

本工程的录音、回放、I2S 配置框架 基于 ChatGPT 提供的程序模板，并由 Starrynight252 （你）进行了进一步修改、完善与调试，使其适配实际硬件环境。

该示例通过自定义 GIO 引脚 配置，实现 I2S RX + TX 全双工功能，适合开发音频应用、语音处理或小型音箱项目。


作者说明

作者（Author）：Starrynight252
注:使用了AI
部分程序来源：ChatGPT（提供了录音、录音播放的最初代码框架）


本项目内容：在模板基础上进行修改完善，包括：

ES8311 全双工初始化

32bit PCM 录音与对齐存储

PCM 播放

MP3 播放器集成

自定义 GPIO 引脚

PIO 工程整合与调试

功能

I2S RX + TX 全双工模式

录制 10 秒 PCM（32bit） 并回放

播放 SD 卡上的 MP3 文件

支持 ES8311 I2C 初始化

I2S 标准格式（STD）

SD 卡存储 PCM 文件（32bit 对齐存储）

硬件需求

ESP32 / Arduino 兼容开发板

ES8311 音频编解码器

SPH0645LM4H-1-8 数字麦克风

Micro SD 卡

功放模块（可选，用于扬声器输出）

连接线或自定义 PCB

软件依赖

需要以下 Arduino 库：

AudioTools

arduino-libhelix
（提供 MP3 解码）

arduino-audio-driver
（提供 ES8311 驱动）

安装方法：

Arduino IDE -> 工具 -> 管理库 -> 搜索以上库并安装


强烈建议使用 PlatformIO：

将库文件通过 git 拉取至项目根目录 /lib/
PIO 会自动引用本地库。
