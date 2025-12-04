/**
 * @file es8311_full_duplex_record_play.cpp
 * @brief ES8311 + SPH0645LM4H-1-8 音频编解码器示例（32bit PCM）
 *        自定义 GIO 引脚，支持录音、回放 PCM 以及 MP3 播放
 *
 * 使用库文件：
 *  1. AudioTools 库 (https://github.com/pschatzmann/arduino-audio-tools)
 *  2. arduino-libhelix      提供 MP3 解码支持
 *  3. arduino-audio-driver  提供 ES8311 驱动
 *
 * 功能演示：
 *  - I2S RX + TX 全双工模式
 *  - 录制 10 秒 PCM（32bit）并回放
 *  - 播放 SD 卡 MP3 文件
 *  - 支持 ES8311 I2C 初始化
 *  - I2S 标准格式（STD）
 *  - SD 卡存储 PCM 文件
 *  - 32bit PCM（对齐存储）
 *
 * 作者：Starrynight252（仓库作者）
 * 录音 / 播放 PCM 的实现来源于 ChatGPT 提供的程序模板，
 * 并在此基础上进行了适配与功能扩展。
 */


#include "AudioTools.h"   
#include "AudioTools/AudioLibs/I2SCodecStream.h"       // I2S 编解码流
#include "AudioTools/Disk/AudioSourceSPIFFS.h"        // SPIFFS 音频源
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"     // MP3 解码
#include "AudioTools/Disk/AudioSourceSD.h"            // SD 卡音频源

//===========================================================
// 存储选择
//===========================================================
#define MP3_FILE_SD_OR_SPIFFS 1 // 1: SD 卡, 0: SPIFFS

//===========================================================
// I2C 配置（ES8311 控制）
//===========================================================
#define SDAPIN 10       // I2C 数据线 SDA
#define SCLPIN 11       // I2C 时钟线 SCL
#define I2CSPEED 100000 // I2C 时钟频率 100 kHz
#define ES8311ADDR 0x18 // ES8311 I2C 地址

//===========================================================
// I2S 配置（音频数据传输）
//===========================================================
#define MCLKPIN 16 // 主时钟 MCLK
#define BCLKPIN 14 // 位时钟 BCLK
#define WSPIN 13   // 采样选择 WS
#define DOPIN 12   // 数据输出 DI (连接到 ES8311 MISO)
#define DIPIN 15   // 数据输入 DO (连接到 ES8311 MOSI)

//===========================================================
// SPI 配置（SD 卡）
//===========================================================
#define SD_SPI_MOSI 47
#define SD_SPI_MISO 21
#define SD_SPI_SCK 26
#define SD_SPI_CS 33

//===========================================================
// 功放控制
//===========================================================
#define I2S_PA_EN (3) // GPIO 控制功放使能

//===========================================================
// 录音/解码 控制
//===========================================================
// 录音时间（秒）
#define RECORD_SECONDS  10

// 采样率，单位 Hz，这里设置为 16kHz
#define SAMPLE_RATE  16000 // 16kHz

// 通道数，单声道为1
#define CHANNELS 1

// 每个采样的位数，这里使用 32bit PCM 
#define BITS_PER_SAMPLE  32 //16;

// 每个采样的字节数（32bit = 4字节）
#define BYTES_PER_SAMPLE  4  //2;

// 总采样数 = 录音时间 * 采样率
#define TOTAL_SAMPLES  RECORD_SECONDS * SAMPLE_RATE

//===========================================================
// 音乐文件路径 & PCM 文件路径
//===========================================================
const char *startFilePath = "/music";    // SD 卡/ SPIFFS 音乐文件夹路径
const char *ext = "test.mp3";            // 默认 MP3 文件名
#define RECORD_FILE_PATH "/rec.pcm"      // PCM 录音文件存储路径

//===========================================================
// I2S 音频信息配置（麦克风输入）
//===========================================================
AudioInfo info(16000, 1, 32); // SPH0645 LM4H，单声道，16kHz，32bit PCM

//===========================================================
// SD 卡音源初始化
//===========================================================
#if MP3_FILE_SD_OR_SPIFFS
SPIClass mySPI = SPIClass(1);                 // 使用第二组 SPI 接口
AudioSourceSD *source = nullptr;             // SD 卡音源指针
#else
AudioSourceSPIFFS *source = nullptr;         // SPIFFS 音源指针
#endif

//===========================================================
// MP3 解码器 & 音频引脚管理
//===========================================================
MP3DecoderHelix decoder;                      // MP3 解码器对象
DriverPins my_pins;                           // 自定义引脚对象

//===========================================================
// 音频板 & I2S 编解码器初始化
//===========================================================
AudioBoard *audio_board = nullptr;           
I2SCodecStream *i2s_out_stream = nullptr;    // I2S 编解码流对象指针
TwoWire myWire = TwoWire(0);                 // 通用 I2C 接口

//===========================================================
// 音乐播放器对象
//===========================================================
AudioPlayer *player = nullptr;               // 音乐播放器对象指针


/* 
 * StreamCopy 拷贝数据对象，用于将 I2S 输入流复制到 I2S 输出流。
 *
 * 这里传入同一个 i2s_out_stream，理论上可实现从麦克风 I2S 输入到喇叭 I2S 输出的
 * 全双工直通（Passthrough）模式。
 *
 * ⚠ 实际效果说明：
 *   - 本程序中没有调用 copier.copy() 或 copier.copyAll()，因此该对象不会自动进行任何数据拷贝。
 *   - 注释掉该语句也不影响录音与播放，因为录音和播放是你手动用 read()/write() 完成的。
 *
 * 来源参考：
 *   arduino-audio-tools/examples/streams/streams-i2s-i2s
 *   该示例中也是把 I2S 输入直接复制到 I2S 输出，需要显式调用 copy() 才会生效。
 *
 * 示例（若需要实时直通）：
 *   copier.copy();     // 单步拷贝一帧数据
 *   或
 *   copier.copyAll();  // 连续实时拷贝
 */
//StreamCopy copier(*i2s_out_stream, *i2s_out_stream); // I2S 输入→输出




void setup()
{
  //===========================================================
  // 串口初始化（用于调试日志）
  //===========================================================
  Serial.begin(115200);

  //===========================================================
  // SD 或 SPIFFS 音源初始化
  //===========================================================
#if MP3_FILE_SD_OR_SPIFFS
  // 初始化 SPI 接口
  mySPI.begin(SD_SPI_SCK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);
  // 创建 SD 音源对象
  source = new AudioSourceSD(startFilePath, ext, SD_SPI_CS, mySPI);
#else
  // SPIFFS 音源对象
  source = new AudioSourceSPIFFS(startFilePath, ext);
#endif

  // 初始化 SD 卡
  SD.begin(SD_SPI_CS, mySPI);

  //===========================================================
  // 音频板和 I2S 初始化
  //===========================================================
  audio_board = new AudioBoard(AudioDriverES8311, my_pins);   // 创建音频板对象
  i2s_out_stream = new I2SCodecStream(audio_board);           // 创建 I2S 编解码流对象
  player = new AudioPlayer(*source, *i2s_out_stream, decoder); // 创建播放器对象

  //===========================================================
  // 日志系统初始化
  //===========================================================
  AudioLogger::instance().begin(Serial, AudioLogger::Warning);
  AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Warning);

  delay(2000); // 等待系统稳定

  //===========================================================
  // 功放使能
  //===========================================================
  pinMode(I2S_PA_EN, OUTPUT);    // 设置为输出
  digitalWrite(I2S_PA_EN, HIGH); // 拉高使能

  //===========================================================
  // 配置 I2C 和 I2S 引脚
  //===========================================================
  my_pins.addI2C(PinFunction::CODEC, SCLPIN, SDAPIN, ES8311ADDR, I2CSPEED, myWire); // I2C 编解码器
  my_pins.addI2S(PinFunction::CODEC, MCLKPIN, BCLKPIN, WSPIN, DOPIN, DIPIN);        // I2S 编解码器

  //===========================================================
  // 初始化引脚
  //===========================================================
  my_pins.begin();

  //===========================================================
  // 初始化音频板
  //===========================================================
  audio_board->begin();

  //===========================================================
  // I2S 配置并启动
  //===========================================================
  auto i2s_config = i2s_out_stream->defaultConfig(RXTX_MODE); // 获取默认配置
  i2s_config.copyFrom(info);                                   // 应用麦克风参数
  i2s_config.i2s_format = I2S_STD_FORMAT;                      // I2S 标准格式
  i2s_out_stream->begin(i2s_config);                           // 启动 I2S
  i2s_out_stream->setVolume(0.55);                             // I2S 初始音量

  //===========================================================
  // 播放器增益设置
  //===========================================================
  player->setVolume(1.0);  // 设置播放器音量

  //===========================================================
  // MP3 文件初始化（加载 test.mp3，但不播放）
  //===========================================================
  player->begin(0, 0); 
  std::string filepath = "/music/a1.mp3";  // 指向新的 MP3 文件
  player->setPath(filepath.c_str());       // 重新设置播放路径

  delay(1000); // 等待系统准备完毕
}


void loop()
{
  // 静态变量，确保录音和播放只执行一次
  static bool recordingDone = false;
  static bool playbackDone = false;

  // ============================================
  //      2. 开始录音 (只录一次)
  // ============================================
  if (!recordingDone)
  {
    Serial.println("开始录音，保存 PCM 到 SD");

    // 打开文件用于写入 PCM 数据
    File recFile = SD.open(RECORD_FILE_PATH, FILE_WRITE);
    if (!recFile)
    {
      Serial.println("错误：无法打开文件进行录音！");
      return;
    }

    uint8_t buf[512];           // 缓冲区
    int samples_recorded = 0;   // 已录制样本数

    while (samples_recorded < TOTAL_SAMPLES)
    {
      // 从 I2S 麦克风读取数据
      size_t bytes = i2s_out_stream->readBytes(buf, sizeof(buf));

      if (bytes < BYTES_PER_SAMPLE)
        continue; // 数据不足一个采样，跳过

      // 保证按照每个采样对齐（32bit）
      size_t aligned_bytes = (bytes / BYTES_PER_SAMPLE) * BYTES_PER_SAMPLE;

      // 写入 SD 卡
      recFile.write(buf, aligned_bytes);

      // 更新已录制样本数
      samples_recorded += aligned_bytes / BYTES_PER_SAMPLE;

      // 每录制 1600 个样本打印一次进度
      if ((samples_recorded % 1600) == 0)
      {
        Serial.printf("录制进度: %d / %d 样本\n", samples_recorded, TOTAL_SAMPLES);
      }
    }

    recFile.close();
    Serial.println("录音完成");
    recordingDone = true;
  }

  // ============================================
  //      3. 播放录音 PCM
  // ============================================
  if (recordingDone && !playbackDone)
  {
    Serial.println("开始播放录音 PCM");

    // 打开录音文件用于读取
    File playFile = SD.open(RECORD_FILE_PATH, FILE_READ);
    if (!playFile)
    {
      Serial.println("错误：无法打开录音文件！");
      return;
    }

    uint8_t buf[512];  // 缓冲区

    while (playFile.available())
    {
      // 读取文件数据到缓冲区
      size_t bytesRead = playFile.read(buf, sizeof(buf));

      if (bytesRead < BYTES_PER_SAMPLE)
        continue; // 数据不足一个采样，跳过

      // 按采样对齐
      size_t aligned = (bytesRead / BYTES_PER_SAMPLE) * BYTES_PER_SAMPLE;

      // 写入 I2S 输出
      i2s_out_stream->write(buf, aligned);
    }

    playFile.close();
    Serial.println("PCM 播放完成");
    playbackDone = true;
  }

  // 播放 MP3 文件
  player->play();  // 准备播放 

  // 播放整个音频流
  // copyAll 会一次性播放完毕
  // 推荐在 FreeRTOS 任务中使用 copy() 并挂起任务，避免频繁调用 copyAll
  player->copyAll();  

  delay(20); // 延时，避免占满 CPU
}
