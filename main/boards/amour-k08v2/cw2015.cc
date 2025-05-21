#include "cw2015.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

Cw2015::Cw2015(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : I2cDevice(i2c_bus, addr) {
    battery.usb_online = 0;
    battery.capacity = 0;
    battery.voltage = 0;
    battery.alt = 0;
    }

bool Cw2015::begin() {
    // 检查设备是否存在
    uint8_t val;
    if (!ReadReg(REG_VERSION, &val)) {
        ESP_LOGE("CW2015", "Device not found");
        return false;
    }
    // 配置设备
    if (!config()) {
        ESP_LOGE("CW2015", "Configuration failed");
        return false;
    }
    ESP_LOGI("CW2015", "CW2015 initialized");
    return true;
}

bool Cw2015::powerOnReset() {
    if (!WriteReg(REG_MODE, MODE_SLEEP)) return false;
    vTaskDelay(1 / portTICK_PERIOD_MS);
    if (!WriteReg(REG_MODE, MODE_NORMAL)) return false;
    vTaskDelay(1 / portTICK_PERIOD_MS);
    return config();
}

bool Cw2015::config() {
    uint8_t reg_val;
    // 唤醒
    if (!WriteReg(REG_MODE, MODE_NORMAL)) {
        ESP_LOGE("CW2015", "Wakeup fail");
        return false;
    }
    // 检查ATHD
    if (!ReadReg(REG_CONFIG, &reg_val)) return false;
    if ((reg_val & 0xF8) != ATHD) {
        reg_val &= 0x07;
        reg_val |= ATHD;
        if (!WriteReg(REG_CONFIG, reg_val)) return false;
    }
    // 检查config update flag
    if (!ReadReg(REG_CONFIG, &reg_val)) return false;
    if (!(reg_val & CONFIG_UPDATE_FLG)) {
        ESP_LOGW("CW2015", "Update flag for new battery info needed");
        if (!updateConfigInfo()) return false;
    } else {
        // 校验参数区
        uint8_t i;
        for (i = 0; i < SIZE_BATINFO; i++) {
            if (!ReadReg(REG_BATINFO + i, &reg_val)) return false;
            if (cw_bat_config_info[i] != reg_val) break;
        }
        if (i != SIZE_BATINFO) {
            ESP_LOGW("CW2015", "Update flag for new battery info needed (2)");
            if (!updateConfigInfo()) return false;
        }
    }
    // 检查SOC
    for (uint8_t i = 0; i < 30; i++) {
        if (!ReadReg(REG_SOC, &reg_val)) return false;
        if (reg_val <= 100) break;
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (i >= 29) {
            WriteReg(REG_MODE, MODE_SLEEP);
            return false;
        }
    }
    return true;
}

bool Cw2015::updateConfigInfo() {
    uint8_t reg_val;
    // 确保不在SLEEP
    if (!ReadReg(REG_MODE, &reg_val)) return false;
    if ((reg_val & MODE_SLEEP_MASK) == MODE_SLEEP) return false;
    // 写入参数区
    for (uint8_t i = 0; i < SIZE_BATINFO; i++) {
        if (!WriteReg(REG_BATINFO + i, cw_bat_config_info[i])) return false;
    }
    // 校验参数区
    for (uint8_t i = 0; i < SIZE_BATINFO; i++) {
        if (!ReadReg(REG_BATINFO + i, &reg_val)) return false;
        if (reg_val != cw_bat_config_info[i]) return false;
    }
    // 设置config flags
    if (!ReadReg(REG_CONFIG, &reg_val)) return false;
    reg_val |= CONFIG_UPDATE_FLG;
    reg_val &= 0x07;
    reg_val |= ATHD;
    if (!WriteReg(REG_CONFIG, reg_val)) return false;
    // 复位
    if (!WriteReg(REG_MODE, MODE_RESTART)) return false;
    vTaskDelay(1 / portTICK_PERIOD_MS);
    if (!WriteReg(REG_MODE, MODE_NORMAL)) return false;
    return true;
}

bool Cw2015::setAthd(uint8_t new_athd) {
    uint8_t reg_val;
    if (!ReadReg(REG_CONFIG, &reg_val)) return false;
    new_athd = new_athd << 3;
    reg_val &= 0x07;
    reg_val |= new_athd;
    return WriteReg(REG_CONFIG, reg_val);
}

int Cw2015::getCapacity() {
    uint8_t reg_val;
    uint8_t cw_capacity;
    if (!ReadReg(REG_SOC, &reg_val)) return -1;
    cw_capacity = reg_val;
    if ((cw_capacity < 0) || (cw_capacity > 100)) {
        reset_loop++;
        if (reset_loop > 5) {
            if (!powerOnReset()) return -1;
            reset_loop = 0;
        }
        return battery.capacity;
    } else {
        reset_loop = 0;
    }
    if (((battery.usb_online == 1) && (cw_capacity == (battery.capacity - 1))) ||
        ((battery.usb_online == 0) && (cw_capacity == (battery.capacity + 1)))) {
        if (!((cw_capacity == 0 && battery.capacity <= 2) || 
              (cw_capacity == 100 && battery.capacity == 99))) {
            cw_capacity = battery.capacity;
        }
    }
    if ((battery.usb_online == 1) && (cw_capacity >= 95) && (cw_capacity <= battery.capacity)) {
        allow_no_charger_full++;
        if (allow_no_charger_full >= BATTERY_UP_MAX_CHANGE) {
            uint8_t allow_capacity = battery.capacity + 1;
            cw_capacity = (allow_capacity <= 100) ? allow_capacity : 100;
            no_charger_full_jump = 1;
            allow_no_charger_full = 0;
        } else if (cw_capacity <= battery.capacity) {
            cw_capacity = battery.capacity;
        }
    } 
    else if ((battery.usb_online == 0) && (cw_capacity <= battery.capacity) && 
             (cw_capacity >= 90) && (no_charger_full_jump == 1)) {
        if (battery.usb_online == 0) {
            allow_no_charger_full++;
        }
        if (allow_no_charger_full >= BATTERY_DOWN_MIN_CHANGE) {
            uint8_t allow_capacity = battery.capacity - 1;
            allow_no_charger_full = 0;
            if (cw_capacity >= allow_capacity) {
                no_charger_full_jump = 0;
            } else {
                cw_capacity = (allow_capacity > 0) ? allow_capacity : 0;
            }
        } else if (cw_capacity <= battery.capacity) {
            cw_capacity = battery.capacity;
        }
    } else {
        allow_no_charger_full = 0;
    }
    if ((battery.usb_online > 0) && (cw_capacity == 0)) {
        allow_charger_always_zero++;
        if ((allow_charger_always_zero >= BATTERY_DOWN_MIN_CHANGE_SLEEP) && 
            (if_quickstart == 0)) {
            if (!powerOnReset()) return -1;
            if_quickstart = 1;
            allow_charger_always_zero = 0;
        }
    } 
    else if ((if_quickstart == 1) && (battery.usb_online == 0)) {
        if_quickstart = 0;
    }
    return cw_capacity;
}

int Cw2015::getVoltage() {
    uint32_t ad_value = 0;
    uint32_t ad_buff = 0;
    uint32_t ad_value_min = 0;
    uint32_t ad_value_max = 0;
    uint8_t reg_val[2];
    for (uint8_t i = 0; i < 3; i++) {
        if (!ReadReg(REG_VCELL, &reg_val[0])) return -1;
        if (!ReadReg(REG_VCELL + 1, &reg_val[1])) return -1;
        ad_buff = (reg_val[0] << 8) + reg_val[1];
        if (i == 0) {
            ad_value_min = ad_buff;
            ad_value_max = ad_buff;
        }
        if (ad_buff < ad_value_min) ad_value_min = ad_buff;
        if (ad_buff > ad_value_max) ad_value_max = ad_buff;
        ad_value += ad_buff;
    }
    ad_value -= ad_value_min;
    ad_value -= ad_value_max;
    ad_value = ad_value * 305 / 1000;
    return ad_value;
}

bool Cw2015::releaseAlert() {
    uint8_t reg_val;
    if (!ReadReg(REG_RRT_ALERT, &reg_val)) return false;
    battery.alt = reg_val & 0x80;
    reg_val &= 0x7F;
    return WriteReg(REG_RRT_ALERT, reg_val);
}

bool Cw2015::updateCapacity() {
    int capacity = getCapacity();
    if (capacity == -1) return false;
    if ((capacity >= 0) && (capacity <= 100) && (battery.capacity != capacity)) {
        battery.capacity = capacity;
    }
    return true;
}

bool Cw2015::updateVoltage() {
    int voltage = getVoltage();
    if (voltage == -1) return false;
    if (voltage == 1) {
        // 保持上一次电压
    } else if (battery.voltage != voltage) {
        battery.voltage = voltage;
    }
    return true;
}

//get_cell_percent float
float Cw2015::getCellPercent() {
    int capacity = getCapacity();
    if (capacity < 0) {
        return 0.0f;
    }
    // 计算百分比（范围0~100）
    float percent = (float)capacity;
    if (percent > 100.0f) percent = 100.0f;
    if (percent < 0.0f) percent = 0.0f;
    return percent;
}

// ...existing code...
bool Cw2015::WriteReg(uint8_t reg, uint8_t value) {
    try {
        I2cDevice::WriteReg(reg, value);
        return true;
    } catch (...) {
        return false;
    }
}

bool Cw2015::ReadReg(uint8_t reg, uint8_t* value) {
    try {
        *value = I2cDevice::ReadReg(reg);
        return true;
    } catch (...) {
        return false;
    }
}

