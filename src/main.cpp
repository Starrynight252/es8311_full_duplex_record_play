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
#include "AudioTools/AudioLibs/I2SCodecStream.h" // I2S 编解码流
#include "AudioTools/Disk/AudioSourceSPIFFS.h"   // SPIFFS 音频源
#include "AudioTools/AudioCodecs/CodecWAV.h"     //wav解码器
#include "AudioTools/Disk/AudioSourceSD.h"       // SD 卡音频源

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
#define RECORD_SECONDS 5

// 采样率，单位 Hz，这里设置为 16kHz
#define SAMPLE_RATE 16000 // 16kHz

// 通道数，单声道为1
#define CHANNELS 1

// 每个采样的位数，这里使用 32bit PCM
#define BITS_PER_SAMPLE 32 // 16;

// 每个采样的字节数（32bit = 4字节）
#define BYTES_PER_SAMPLE 4 // 2;

// 总采样数 = 录音时间 * 采样率
#define TOTAL_SAMPLES RECORD_SECONDS *SAMPLE_RATE

// WVA_RECORD 缓冲区 大小
#define WVA_RECORD_BUFFER_LENGTH 512
//===========================================================
// 音乐文件路径 & PCM 文件路径
//===========================================================
const char *startFilePath = "/music"; // SD 卡/ SPIFFS 音乐文件夹路径
const char *ext = "test.wav";         // 默认 WAV 文件名
#define RECORD_FILE_PATH "/rec.wav"   // WAV 录音文件存储路径

//===========================================================
// I2S 音频信息配置（麦克风输入）
//===========================================================
AudioInfo info(16000, 1, 32); // SPH0645 LM4H，单声道，16kHz，32bit PCM

WAVEncoder encoder; //  EncoderWAV 编码器对象--用于录音保存为 WAV 文件

//===========================================================
// SD 卡音源初始化
//===========================================================
#if MP3_FILE_SD_OR_SPIFFS
SPIClass mySPI = SPIClass(1);    // 使用第二组 SPI 接口
AudioSourceSD *source = nullptr; // SD 卡音源指针
#else
AudioSourceSPIFFS *source = nullptr; // SPIFFS 音源指针
#endif

//===========================================================
// MP3 解码器 & 音频引脚管理
//===========================================================
WAVDecoder decoder; // wav解码
DriverPins my_pins; // 自定义引脚对象

//===========================================================
// 音频板 & I2S 编解码器初始化
//===========================================================
AudioBoard *audio_board = nullptr;
I2SCodecStream *i2s_out_stream = nullptr; // I2S 编解码流对象指针
TwoWire myWire = TwoWire(0);              // 通用 I2C 接口

//===========================================================
// 音乐播放器对象
//===========================================================
AudioPlayer *player = nullptr; // 音乐播放器对象指针

static bool recordingDone = false;
static bool playRecDone = false;
static bool playMusicDone = false;
uint8_t WVA_RECORDBuf[WVA_RECORD_BUFFER_LENGTH];

/**
 * @brief 在录音前播放一个短暂的静音 WAV 文件，用于清空 I2S 缓冲区
 *
 * 该函数会：
 * 1. 创建一个短时静音 WAV 文件（几毫秒）。
 * 2. 使用 WAV 编码器写入静音数据。
 * 3. 停止功放输出，防止噪声干扰。
 * 4. 播放生成的静音 WAV 文件，确保 I2S 编解码器缓冲区被清空。
 *
 * 使用场景：
 * - 在从播放 WAV 音乐切换到录音前调用，避免录音噪声。
 *
 * 注意：
 * - 需要确保 player、encoder、I2S 编解码器对象已经初始化。
 * - 静音 WAV 文件会临时写入 SD 卡路径 `currentFilePath_WVA_RECORD`。
 *
 */
void flushI2SWithSilentWAV();

// ====================== WAV 编码器 ======================
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
  audio_board = new AudioBoard(AudioDriverES8311, my_pins);    // 创建音频板对象
  i2s_out_stream = new I2SCodecStream(audio_board);            // 创建 I2S 编解码流对象
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
  i2s_config.copyFrom(info);                                  // 应用麦克风参数
  i2s_config.i2s_format = I2S_STD_FORMAT;                     // I2S 标准格式
  i2s_out_stream->begin(i2s_config);                          // 启动 I2S
  i2s_out_stream->setVolume(0.55);                            // I2S 初始音量

  //===========================================================
  // 播放器增益设置
  //===========================================================
  player->setVolume(1.0); // 设置播放器音量

  //===========================================================
  // WAV 文件初始化（加载 test.wav，但不播放）
  //===========================================================
  player->begin(0, 0);
  // std::string filepath = "/music/a1.wav"; // 指向新的 WAV 文件
  // player->setPath(filepath.c_str());      // 重新设置播放路径

  delay(1000); // 等待系统准备完毕
}

void loop()
{

  // =====================================================
  // 1️⃣ 录音 → 保存为 WAV
  // =====================================================
  if (!recordingDone)
  {
    Serial.println("开始录音 WAV");

    // 停止播放器，确保 I2S RX 可用
    player->end();

    File recFile = SD.open(RECORD_FILE_PATH, FILE_WRITE);
    if (!recFile)
    {
      Serial.println("无法创建 rec.wav");
      return;
    }

    encoder.begin(info);
    encoder.setOutput(recFile);

    int samples_recorded = 0;

    while (samples_recorded < TOTAL_SAMPLES)
    {
      size_t bytes = i2s_out_stream->readBytes(WVA_RECORDBuf, sizeof(WVA_RECORDBuf)); // 从 I2S 读取音频数据
      if (bytes < BYTES_PER_SAMPLE)                                                   // 数据不足，继续读取
        continue;

      size_t aligned = (bytes / BYTES_PER_SAMPLE) * BYTES_PER_SAMPLE;

      encoder.write(WVA_RECORDBuf, aligned); // 写入 WAV 编码器
      samples_recorded += aligned / BYTES_PER_SAMPLE;
    }

    encoder.end(); // 写 WAV 头
    recFile.close();

    recordingDone = true;
    Serial.println("录音完成：rec.wav");
    delay(1000);
  }

  // =====================================================
  // 2️⃣ 播放录音 WAV
  // =====================================================
  if (recordingDone && !playRecDone)
  {
    Serial.println("播放录音 WAV");

    player->setPath(RECORD_FILE_PATH);
    player->play();

    while (player->copy())
    {
      // AudioPlayer 内部自动解码 WAV → I2S
    }

    playRecDone = true;
    Serial.println("录音 WAV 播放完成");
    delay(1000);
  }

  // =====================================================
  // 3️⃣ 播放 SD 卡 WAV 音乐
  // =====================================================
  if (!playMusicDone)
  {
    Serial.println("播放 SD WAV 音乐");

    // 使用你 setup 里定义的 source/ext
    player->setPath("/music/test.wav");
    player->play();

    while (player->copy())
    {
    }

    playMusicDone = true;
    Serial.println("音乐 WAV 播放完成");
  }

  delay(2000);
}

void flushI2SWithSilentWAV()
{
  // 清空数据
  memset(WVA_RECORDBuf, 0, WVA_RECORD_BUFFER_LENGTH);

  File WVA_RECORDFile = SD.open(RECORD_FILE_PATH, FILE_WRITE);
  if (!WVA_RECORDFile)
  {
    return;
  }

  encoder.begin(info);               // 使用与输入相同的音频信息初始化编码器
  encoder.setOutput(WVA_RECORDFile); // 设置输出文件

  digitalWrite(I2S_PA_EN, 0); // 停止功放

  //单位8ms(与音频info设置有关)
  for (int o = 0; o < 1; o++)
  {
    // 写入一个空数据
    encoder.write(WVA_RECORDBuf, WVA_RECORD_BUFFER_LENGTH); // 512 总结大概8ms
  }

  encoder.end(); // 写 WAV 头
  // 关闭文件
  WVA_RECORDFile.close();
  vTaskDelay(5 / portTICK_PERIOD_MS); // 等待完成
  // 设置文件路径
  player->setPath(RECORD_FILE_PATH);
  player->play();
  player->copyAll(); // 播放

  vTaskDelay(5 / portTICK_PERIOD_MS); // 等待完成
}

