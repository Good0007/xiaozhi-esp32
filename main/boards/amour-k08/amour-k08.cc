#include "wifi_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "AmourK08LCD"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);

class AmourK08LCD : public WifiBoard {
private:
    
    Button boot_button_;
    Button volume_up_button_;    // 添加为成员变量
    Button volume_down_button_;  // 添加为成员变量
    Button model_button_;        // 添加为成员变量
    bool volume_up_pressed_ = false;  // 跟踪音量+按钮状态
    bool volume_down_pressed_ = false; // 跟踪音量-按钮状态
    bool screen_on_ = true;  // 跟踪屏幕状态
    LcdDisplay* display_;

    void switch_to_other_app() {
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
        
        if (target != NULL) {
            ESP_LOGI(TAG, "当前运行分区: %s (0x%lx)", running->label, running->address);
            ESP_LOGI(TAG, "切换至分区: %s (0x%lx)", target->label, target->address);
            
            //if (display_) {
                char message[100];
                //snprintf(message, sizeof(message), "切换至分区: %s", target->label);
                //display_->SetChatMessage("system", message);
                //display_->ShowNotification(message, 3000);
            //}
            
            // 等待显示完成
            //vTaskDelay(pdMS_TO_TICKS(2000));
            
            esp_err_t err = esp_ota_set_boot_partition(target);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "分区切换成功，准备重启...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            } else {
                ESP_LOGE(TAG, "设置启动分区失败: %s", esp_err_to_name(err));
                if (display_) {
                    display_->SetChatMessage("system", "系统切换失败!");
                }
            }
        } else {
            ESP_LOGE(TAG, "未找到可切换的分区");
            if (display_) {
                display_->SetChatMessage("system", "未找到分区!");
            }
        }
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);
 

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_16_4,
                                        .icon_font = &font_awesome_16_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
#endif
                                    });
    }


 
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
        boot_button_.OnPressDown([this]()
            { Application::GetInstance().StartListening(); });
        boot_button_.OnPressUp([this]()
            { Application::GetInstance().StopListening(); });

        // 音量增加按钮
        volume_up_button_.OnPressDown([this]() {
            ESP_LOGI(TAG, "Volume Up Button Pressed");
            volume_up_pressed_ = true;
            
            // 检查是否两个音量按钮都被按下
            if (volume_up_pressed_ && volume_down_pressed_) {
                ESP_LOGI(TAG, "Both volume buttons pressed - resetting WiFi");
                ResetWifiConfiguration();
                // 显示重置提示
                if (display_) {
                    display_->SetChatMessage("system", "WiFi配置已重置，请重新配网");
                }
            } else {
                // 正常音量增加逻辑
                auto codec = GetAudioCodec();
                if (codec) {
                    int volume = codec->output_volume();
                    codec->SetOutputVolume(volume + 5);
                    ESP_LOGI(TAG, "Volume increased to %d", volume + 5);
                }
            }
        });
        volume_up_button_.OnPressUp([this]() {
            ESP_LOGI(TAG, "Volume Up Button Released");
            volume_up_pressed_ = false;
        });

        // 音量减少按钮
        volume_down_button_.OnPressDown([this]() {
            ESP_LOGI(TAG, "Volume Down Button Pressed");
            volume_down_pressed_ = true;
            
            // 检查是否两个音量按钮都被按下
            if (volume_up_pressed_ && volume_down_pressed_) {
                ESP_LOGI(TAG, "Both volume buttons pressed - resetting WiFi");
                ResetWifiConfiguration();
                // 显示重置提示
                if (display_) {
                    display_->SetChatMessage("system", "WiFi配置已重置，请重新配网");
                }
            } else {
                // 正常音量减少逻辑
                auto codec = GetAudioCodec();
                if (codec) {
                    int volume = codec->output_volume();
                    codec->SetOutputVolume(volume - 5);
                    ESP_LOGI(TAG, "Volume decreased to %d", volume - 5);
                }
            }
        });
        
        volume_down_button_.OnPressUp([this]() {
            ESP_LOGI(TAG, "Volume Down Button Released");
            volume_down_pressed_ = false;
        });


        model_button_.OnClick([this]() {
            ESP_LOGI(TAG, "MODEL button clicked");
            if (!screen_on_) {
                // 打开屏幕
                ESP_LOGI(TAG, "Turning screen ON");
                if (GetBacklight()) {
                    GetBacklight()->RestoreBrightness();
                }
                
                if (display_) {
                    esp_lcd_panel_handle_t panel = display_->GetPanel();
                    if (panel) {
                        esp_lcd_panel_disp_on_off(panel, true);
                    }
                }
                screen_on_ = true;
            } else {
                // 关闭屏幕
                ESP_LOGI(TAG, "Turning screen OFF");
                if (GetBacklight()) {
                    GetBacklight()->SetBrightness(0);
                }
                
                if (display_) {
                    esp_lcd_panel_handle_t panel = display_->GetPanel();
                    if (panel) {
                        esp_lcd_panel_disp_on_off(panel, false);
                    }
                }
                screen_on_ = false;
            }
        });

        model_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "MODEL button long press");
            if (display_) {
                display_->SetChatMessage("system", "即将切到网络收音机...");
            }
            switch_to_other_app();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        //注册RadioPlayer
        thing_manager.AddThing(iot::CreateThing("RadioPlayer"));
        // thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

public:
    AmourK08LCD() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
        model_button_(MODEL_BUTTON_GPIO)
        {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeIot();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(AmourK08LCD);