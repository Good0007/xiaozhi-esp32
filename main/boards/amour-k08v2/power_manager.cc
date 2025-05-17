#include "power_manager.h"
#include <esp_timer.h>
#include <esp_intr_alloc.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"


#define TAG "PowerManager"

void IRAM_ATTR PowerManager::power_hold_pin_isr_handler(void* arg) {
    // ESP_LOGI(TAG, "Power hold pin ISR triggered");
    PowerManager* self = static_cast<PowerManager*>(arg);
    self->last_high_time_ = esp_timer_get_time();
    self->is_charging_ = true;
    if (self->on_charging_status_changed_) {
        self->on_charging_status_changed_(true);
    }
}

void PowerManager::PowerHoldTimerCallback(void* arg) {
    PowerManager* self = static_cast<PowerManager*>(arg);
    gpio_set_level(self->power_hold_pin_, 0);
}

void PowerManager::send_power_hodle_pulse(){
    gpio_set_level(power_hold_pin_, 1);
    esp_timer_start_once(power_hold_timer_, 50000); // 50ms
}

void PowerManager::CheckBatteryLevel() {
    #if USE_BATTERY_GAUGE_SENSOR
    int battery_level = max_17043_->get_cell_percent();
    // 限制电池电量范围在0-100之间
    if (battery_level > 100) battery_level = 100;
    if (battery_level < 0) battery_level = 0; 
    battery_level_ = battery_level;
    #else
    battery_level_ = 100;   
    #endif

}

void PowerManager::CheckChargingStatus() {
    int64_t current_time = esp_timer_get_time();
    int pin_level = gpio_get_level(charging_pin_);
    
    //检查电池电量
    CheckBatteryLevel();
    // 每20秒输出一个50ms的高电平
    if (current_time - last_power_hold_time_ > 20000000) { // 20秒 = 20000000微秒
        send_power_hodle_pulse();
        last_power_hold_time_ = current_time;
    }
    // 如果引脚为高电平，更新最后高电平时间
    if (pin_level == 1) {
        if (current_time - last_high_time_ > 2000000) { 
            //超过两秒为高电平，识别为充满
            is_discharging_ = true;
        }
        else{
            is_discharging_ = false;
        }
        last_high_time_ = current_time;
        is_charging_ = true;
        return;
    }
    // 如果引脚为低电平，检查是否超过2秒
    if (pin_level == 0) {
        if (current_time - last_high_time_ > 2000000) { // 2秒 = 2000000微秒
            //2秒以上低电平为不充电状态
            is_charging_ = false;
            is_discharging_ = false;
             
            // if (on_charging_status_changed_) {
            //     on_charging_status_changed_(false);
            // }
        }
    }
}



PowerManager::PowerManager(gpio_num_t charging_pin, gpio_num_t power_hold_pin, i2c_master_bus_handle_t i2c_bus) 
    : charging_pin_(charging_pin), power_hold_pin_(power_hold_pin) {
    #if USE_BATTERY_GAUGE_SENSOR
    max_17043_ = new Max17043(i2c_bus, 0x36);
    #endif
    // 初始化充电引脚
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE;  // 上升沿触发
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << charging_pin_);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; 
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;     
    gpio_config(&io_conf);

    // 初始化 power_hold 引脚
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << power_hold_pin_);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(power_hold_pin_, 0); // 初始状态设为低电平

    // 安装 GPIO 中断服务
    gpio_install_isr_service(0);
    // 添加中断处理程序
    gpio_isr_handler_add(charging_pin_, power_hold_pin_isr_handler, this);
    // 启用中断
    gpio_intr_enable(charging_pin_);

    // 创建充电状态检查定时器
    esp_timer_create_args_t charging_timer_args = {
        .callback = [](void* arg) {
            PowerManager* self = static_cast<PowerManager*>(arg);
            self->CheckChargingStatus();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "charging_check_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&charging_timer_args, &charging_check_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(charging_check_timer_, 1000000)); // 每1s检查一次

    // 创建 power_hold 定时器
    esp_timer_create_args_t power_hold_timer_args = {
        .callback = PowerHoldTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "power_hold_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&power_hold_timer_args, &power_hold_timer_));
    ESP_ERROR_CHECK(esp_timer_start_once(power_hold_timer_, 20000000));
}

PowerManager::~PowerManager() {
    if (charging_check_timer_) {
        esp_timer_stop(charging_check_timer_);
        esp_timer_delete(charging_check_timer_);
    }
    if (power_hold_timer_) {
        esp_timer_stop(power_hold_timer_);
        esp_timer_delete(power_hold_timer_);
    }
    // 移除中断处理程序
    gpio_isr_handler_remove(charging_pin_);
}

bool PowerManager::IsCharging() {
    return is_charging_;
}

bool PowerManager::IsDischarging() {
    return is_discharging_;
}

int PowerManager::GetBatteryLevel() {
    return battery_level_;
}

void PowerManager::OnChargingStatusChanged(std::function<void(bool)> callback) {
    on_charging_status_changed_ = callback;
}

void PowerManager::PowerOff() {
    if (is_shutting_down_) {
        return; // 已经在关机过程中
    }
    ESP_LOGI(TAG, "Start PowerOff");
    is_shutting_down_ = true;
    // 第一个脉冲
    gpio_set_level(power_hold_pin_, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // 50ms高电平
    gpio_set_level(power_hold_pin_, 0);
    
    // 等待400ms
    vTaskDelay(pdMS_TO_TICKS(400));
    
    // 第二个脉冲
    gpio_set_level(power_hold_pin_, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // 50ms高电平
    gpio_set_level(power_hold_pin_, 0);
    
    is_shutting_down_ = false;
    ESP_LOGI(TAG, "PowerOff completed");
} 