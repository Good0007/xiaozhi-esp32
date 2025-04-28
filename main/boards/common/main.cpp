#include "lgfx.h"
#include <Adafruit_AHTX0.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>
#include <Audio.h>
#include <HTTPClient.h>
#include <OneButton.h>
#include <map>
#include "USB.h"
#include "USBCDC.h"
// 添加ESP32分区支持
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "NvsSettings.h"

#define PIN_LED 48
#define PIN_RED_LED 47

#define PIN_I2S_SD 4
#define PIN_I2S_DOUT 5
#define PIN_I2S_BCLK 6
#define PIN_I2S_LRC 7

#define PIN_KEY_ADD 9
#define PIN_KEY_MINUS 21
#define PIN_KEY_MODE 14

#define PIN_I2C_SDA 1
#define PIN_I2C_SCL 2

using namespace std;

#define FONT16 &fonts::efontCN_16
#define FM_URL "http://lhttp.qtfm.cn/live/%d/64k.mp3"

USBCDC USBSerial; // 创建CDC串口实例

NvsSettings settings;

typedef struct {
  u32_t id;
  String name;
} RadioItem;

static const char *WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};
long check1s = 0, check10ms = 0, check300ms = 0, check60s = 0;
char buf[128] = {0};
LGFX tft;
LGFX_Sprite sp(&tft);
Audio audio;
int curIndex = 0;
int curVolume = 5;
Adafruit_NeoPixel pixels(4, PIN_LED, NEO_GRB + NEO_KHZ800);
Adafruit_AHTX0 aht;
std::map<u32_t, OneButton *> buttons;
std::vector<RadioItem> radios = {
  {4915, "清晨音乐台",0},
  {1223, "怀旧好声音",0},
  {4866, "浙江音乐调频",0},
  {20211686, "成都年代音乐怀旧好声音",0},
  {1739, "厦门音乐广播",0},
  {1271, "深圳飞扬971",0},
  {20240, "山东经典音乐广播",0},
  {20500066, "年代音乐1022",0},
  {1296, "湖北经典音乐广播",0},
  {267, "上海经典947",0},
  {20212426, "崂山921",0},
  {20003, "天津TIKI FM100.5",0},
  {1111, "四川城市之音",0},
  {4936, "江苏音乐广播PlayFM897",0},
  {4237, "长沙FM101.7城市之声,0"},
  {1665, "山东音乐广播",0},
  {1947, "安徽音乐广播",0},
  {332, "北京音乐广播",0},
  {4932, "山西音乐广播",0},
  {20500149, "两广之声音乐台",0},
  {4804, "怀集音乐之声",0},
  {1649, "河北音乐广播",0},
  {4938, "江苏经典流行音乐",0},
  {1260, "广东音乐之声",0},
  {273, "上海流行音乐LoveRadio",0},
  {274, "上海动感101",0},
  {2803, "苏州音乐广播",0},
  {5021381, "959年代音乐怀旧好声音",0},
  {15318569, "AsiaFM 亚洲粤语台",0},
  {5022308, "500首华语经典,0"},
  {4875, "FM950广西音乐台",0},
  {5022023, "上海KFM981",0},
  {20207761, "80后音悦台",0},
  {1654, "石家庄音乐广播"},0,
  {20212227, "经典FM1008",0},
  {1671, "济南音乐广播FM88.7",0},
  {5021912, "AsiaFM 亚洲经典台",0},
  {1084, "大连1067",0},
  {1831, "吉林音乐广播",0},
  {5022405, "AsiaFM 亚洲音乐台",0},
  {4581, "亚洲音乐成都FM96.5",0},
  {20071, "AsiaFM 亚洲天空台",0},
  {20033, "1047 Nice FM",0},
  {4930, "FM102.2亲子智慧电台",0},
  {4846, "893音乐广播",0},
  {4923, "徐州音乐广播FM91.9",0},
  {4878, "海南音乐广播",0},
  {20211575, "经典983电台",0},
  {20500097, "经典音乐广播FM94.8",0},
  {1975, "MUSIC876",0},
  {5022391, "Easy Fm",0},
  {20211620, "流行音乐广播999正青春",0},
  {20067, "贵州FM91.6音乐广播",0},
  {20500053, "经典958",0},
  {5022520, "盛京FM105.6",0},
  {20091, "中国校园之声",0},
  {4979, "89.3芒果音乐台",0},
  {5022338, "冰城1026哈尔滨古典音乐广播",0},
  {20207762, "河南经典FM",0},
  {4921, "郑州音乐广播",0},
  {4871, "唐山音乐广播",0},
  {1683, "烟台音乐广播FM105.9",0},
  {20212387, "凤凰音乐",0},
  {20500187, "云梦音乐台",0},
};

void inline initAudioDevice() {
  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  audio.setVolume(curVolume);
}

void inline initPixels() {
  pinMode(PIN_RED_LED, OUTPUT);
  digitalWrite(PIN_RED_LED, LOW);
  pixels.begin();
  pixels.setBrightness(40);
  pixels.clear();
  pixels.show();
}

void inline autoConfigWifi() {
  tft.println("Start WiFi Connect!");
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin();
  for (int i = 0; WiFi.status() != WL_CONNECTED && i < 100; i++) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.beginSmartConfig();
    tft.println("Use ESPTouch App!");
    while (WiFi.status() != WL_CONNECTED) {
      delay(100);
    }
    WiFi.stopSmartConfig();
    WiFi.mode(WIFI_MODE_STA);
  }
  tft.println("WiFi Connected, Please Wait...");
}

inline void showCurrentTime() {
  struct tm info;
  getLocalTime(&info);
  sprintf(buf, "%d年%d月%d日 星期%s", 1900 + info.tm_year, info.tm_mon + 1,
          info.tm_mday, WEEK_DAYS[info.tm_wday]);
  sp.createSprite(240, 16);
  sp.drawCentreString(buf, 120, 0);
  sp.pushSprite(0, 110);
  sp.deleteSprite();
  strftime(buf, 36, "%T", &info);
  sp.createSprite(240, 36);
  sp.drawCentreString(buf, 120, 0, &fonts::FreeSans24pt7b);
  sp.pushSprite(0, 140);
  sp.deleteSprite();
}

void inline startConfigTime() {
  const int timeZone = 8 * 3600;
  configTime(timeZone, 0, "ntp6.aliyun.com", "cn.ntp.org.cn", "ntp.ntsc.ac.cn");
  while (time(nullptr) < 8 * 3600 * 2) {
    delay(300);
  }
}

void inline setupOTAConfig() {
  ArduinoOTA.onStart([] {
    audio.stopSong();
    tft.setBrightness(200);
    tft.clear();
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("软件升级", 120, 8, FONT16);
    tft.drawRoundRect(18, 158, 204, 10, 3, TFT_ORANGE);
    tft.drawCentreString("正在升级中，请勿断电...", 120, 190, FONT16);
  });
  ArduinoOTA.onProgress([](u32_t pro, u32_t total) {
    sprintf(buf, "升级进度: %d / %d", pro, total);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawCentreString(buf, 120, 120, FONT16);
    if (pro > 0 && total > 0) {
      int pros = pro * 200 / total;
      tft.fillRoundRect(20, 160, pros, 6, 2, TFT_WHITE);
    }
  });
  ArduinoOTA.onEnd([] {
    tft.clear();
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("升级成功", 120, 60, FONT16);
    tft.drawCentreString("升级已完成，正在重启...", 120, 140, FONT16);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    tft.clear();
    ESP.restart();
  });
  ArduinoOTA.begin();
  sprintf(buf, "%s", WiFi.localIP().toString().c_str());
  tft.println(buf);
  struct tm info;
  getLocalTime(&info);
  strftime(buf, 64, "%c", &info);
  tft.println(buf);
}

// 添加在其他工具函数附近，例如setupOTAConfig()函数后面
void switch_to_other_app() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    
    if (target != NULL) {
      //Serial.printf("当前运行分区: %s (0x%lx)\n", running->label, running->address);
      //Serial.printf("切换至分区: %s (0x%lx)\n", target->label, target->address);
      
      // 在屏幕上显示切换信息
      tft.clear();
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      //sprintf(buf, "当前分区: %s", running->label);
      //tft.drawCentreString(buf, 120, 60, FONT16);
      //sprintf(buf, "切换至: %s", target->label);
      tft.drawCentreString(buf, 120, 100, FONT16);
      tft.drawCentreString("即将切换到小智...", 120, 140, FONT16);
      
      delay(2000); // 显示2秒给用户时间阅读
      
      esp_err_t err = esp_ota_set_boot_partition(target); //设置有问题，下次启动还是从ota1
      if (err == ESP_OK) {
        Serial.println("分区切换成功，准备重启...");
        // 确保设置被标记为永久有效
        const esp_partition_t* next_boot = esp_ota_get_boot_partition();
        if (next_boot != target) {
          sprintf(buf, "设置分区失败: %s\n", next_boot->label);
        }
        delay(500);
        ESP.restart();
      } else {
        Serial.printf("设置启动分区失败: %s\n", esp_err_to_name(err));
        tft.drawCentreString("切换系统失败!", 120, 180, FONT16);
      }
    } else {
      Serial.println("未找到可切换的分区");
      tft.drawCentreString("未找到分区!", 120, 180, FONT16);
    }
}

// 添加长按回调函数
void onButtonLongPress(void *p) {
    u32_t pin = (u32_t)p;
    switch (pin) {
    case PIN_KEY_MODE:
      // 长按MODE键触发系统切换
      switch_to_other_app();
      break;
    default:
      break;
    }
}

void nextVolume(int offset) {
  int vol = curVolume + offset;
  if (vol >= 0 && vol <= 21) {
    curVolume = vol;
    audio.setVolume(curVolume);
    settings.setInt("radio_volume", curVolume);
    sprintf(buf, "音量: %d", curVolume);
    sp.createSprite(120, 16);
    sp.drawString(buf, 8, 0);
    sp.pushSprite(0, 220);
    sp.deleteSprite();
  }
}

void playNext(int offset) {
  int total = radios.size();
  curIndex += offset;
  if (curIndex >= total) {
    curIndex %= total;
  } else if (curIndex < 0) {
    curIndex += total;
  }
  settings.setInt("radio_index", curIndex);
  auto radio = radios[curIndex];
  sprintf(buf, FM_URL, radio.id);
  audio.connecttohost(buf);
  sprintf(buf, "%d.%s", curIndex + 1, radio.name.c_str());
  sp.createSprite(240, 16);
  sp.drawCentreString(buf, 120, 0);
  sp.pushSprite(0, 20);
  sp.deleteSprite();
}

inline void showClientIP() {
  tft.clear();
  sprintf(buf, "%s", WiFi.localIP().toString().c_str());
  sp.createSprite(120, 16);
  sp.drawRightString(buf, 112, 0);
  sp.pushSprite(120, 220);
  sp.deleteSprite();
}

void onButtonClick(void *p) {
  u32_t pin = (u32_t)p;
  switch (pin) {
  case PIN_KEY_MODE:
    audio.pauseResume();
    break;
  case PIN_KEY_ADD:
    //playNext(1);
    nextVolume(1);
    break;
  case PIN_KEY_MINUS:
    //playNext(-1);
    nextVolume(-1);
    break;
  default:
    break;
  }
}

void onButtonDoubleClick(void *p) {
  u32_t pin = (u32_t)p;
  switch (pin) {
  case PIN_KEY_ADD:
    //nextVolume(1);
    playNext(1);
    break;
  case PIN_KEY_MINUS:
    //nextVolume(-1);
    playNext(-1);
    break;
  default:
    break;
  }
}

void inline setupButtons() {
  u32_t btnPins[] = {PIN_KEY_ADD, PIN_KEY_MINUS, PIN_KEY_MODE};
  for (auto pin : btnPins) {
    auto *btn = new OneButton(pin);
    btn->attachClick(onButtonClick, (void *)pin);
    btn->attachDoubleClick(onButtonDoubleClick, (void *)pin);
    // 添加长按事件
    btn->attachLongPressStart(onButtonLongPress, (void *)pin);
    buttons.insert({pin, btn});
  }
}

void inline initTFTDevice() {
  tft.init();
  tft.setBrightness(60);
  tft.setFont(FONT16);
  tft.setColorDepth(8);
  tft.fillScreen(TFT_BLACK);
  sp.setFont(FONT16);
  sp.setColorDepth(8);
}

void inline initAHT20Wire() {
  Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!aht.begin()) {
    Serial.println("Could not find AHT20?");
  }
}

void inline updateAHT20Data() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  sprintf(buf, "气温: %.2f℃ 湿度: %.2f%%\n", temp.temperature,
          humidity.relative_humidity);
  sp.createSprite(240, 16);
  sp.drawCentreString(buf, 120, 0);
  sp.pushSprite(0, 60);
  sp.deleteSprite();
}

void setup() {
  // 在setup()开头添加
  Serial.begin(115200);
  USBSerial.begin(115200);
  USB.begin();
  USBSerial.println("Hello ESP-S3(USB Model)!!");
  settings.begin();
  curVolume = settings.getInt("radio_volume", 5);
  curIndex  = settings.getInt("radio_index", 0);
  initTFTDevice();
  initAHT20Wire();
  setupButtons();
  initPixels();
  initAudioDevice();
  autoConfigWifi();
  startConfigTime();
  setupOTAConfig();
  showClientIP();
  showCurrentTime();
  updateAHT20Data();
  nextVolume(0);
  playNext(0);
}

void loop() {
  audio.loop();
  auto ms = millis();
  if (ms - check60s > 60000) {
    check60s = ms;
    updateAHT20Data();
  }
  if (ms - check1s > 1000) {
    check1s = ms;
    ArduinoOTA.handle();
    digitalWrite(PIN_RED_LED, check1s % 2 ? LOW : HIGH);
  }
  if (ms - check300ms > 300) {
    check300ms = ms;
    showCurrentTime();
    uint16_t rc = rand() % 65536;
    pixels.fill(rc);
    pixels.show();
  }
  if (ms - check10ms >= 10) {
    check10ms = ms;
    for (auto it : buttons) {
      it.second->tick();
    }
  }
}

void audio_info(const char *info) { Serial.println(info); }

void audio_eof_stream(const char *info) { playNext(1); }