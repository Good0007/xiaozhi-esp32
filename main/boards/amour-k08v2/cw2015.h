#pragma once
#include "i2c_device.h"

// CW2015 默认I2C地址
#define CW2015_I2C_ADDR 0x62

// CW2015寄存器定义
#define CW2015_REG_VERSION     0x00
#define CW2015_REG_VCELL_H     0x02
#define CW2015_REG_VCELL_L     0x03
#define CW2015_REG_SOC_H       0x04
#define CW2015_REG_SOC_L       0x05
#define CW2015_REG_MODE        0x0A
#define CW2015_REG_CONFIG      0x08
#define CW2015_REG_BATINFO     0x10

class Cw2015 : public I2cDevice {
public:
    Cw2015(i2c_master_bus_handle_t i2c_bus, uint8_t addr = CW2015_I2C_ADDR);

    int get_ic_version();
    float get_cell_voltage();
    float get_cell_percent();
    void quick_start();
    void sleep();
    void wakeup();

private:
    uint16_t read_word(uint8_t reg);
};