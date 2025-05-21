#pragma once
#include <functional>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_timer.h>
#include <esp_intr_alloc.h>
#include "cw2015.h"

class PowerManager {
private:
    esp_timer_handle_t charging_check_timer_; // 充电状态检查定时器
    esp_timer_handle_t power_hold_timer_;     // 电源保持定时器
    std::function<void(bool)> on_charging_status_changed_; // 充电状态变化回调

    gpio_num_t charging_pin_ = GPIO_NUM_NC; // 充电状态检测引脚
    gpio_num_t power_hold_pin_ = GPIO_NUM_NC; // 电源保持引脚
    Cw2015* soc_sensor_ = nullptr; // 电池电量sensor
    bool is_charging_ = false; // 充电状态标志
    bool is_discharging_ = false; //充满状态标志
    int64_t last_high_time_ = 0; // 最后高电平时间
    int8_t battery_level_ = 100; // 电池电量
    int64_t last_power_hold_time_ = 0; // 最后电源保持时间
    bool is_shutting_down_ = false; // 是否正在关机
    int power_hold_pin_state_machine = 0; // 电源保持引脚状态机
    

    static void IRAM_ATTR power_hold_pin_isr_handler(void* arg);  // 中断处理函数
    void CheckChargingStatus();  // 检查充电状态
    static void PowerHoldTimerCallback(void* arg);  // 电源保持定时器回调
    void send_power_hodle_pulse();  // 发送电源保持脉冲
    void CheckBatteryLevel();

public:
    PowerManager(gpio_num_t charging_pin, gpio_num_t power_hold_pin, i2c_master_bus_handle_t i2c_bus);
    ~PowerManager();

    bool IsCharging();
    bool IsDischarging();
    int GetBatteryLevel();
    void OnChargingStatusChanged(std::function<void(bool)> callback);
    void PowerOff();
};
