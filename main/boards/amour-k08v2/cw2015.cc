#include "cw2015.h"

Cw2015::Cw2015(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : I2cDevice(i2c_bus, addr) {}

int Cw2015::get_ic_version() {
    uint8_t ver = ReadReg(CW2015_REG_VERSION);
    return ver;
}

float Cw2015::get_cell_voltage() {
    uint16_t vcell = (ReadReg(CW2015_REG_VCELL_H) << 8) | ReadReg(CW2015_REG_VCELL_L);
    // CW2015电压单位: 305uV/bit
    return vcell * 0.305f / 1000.0f;
}

float Cw2015::get_cell_percent() {
    uint8_t soc_h = ReadReg(CW2015_REG_SOC_H);
    uint8_t soc_l = ReadReg(CW2015_REG_SOC_L);
    // SOC_H: 百分比整数部分，SOC_L: 小数部分(1/256)
    return soc_h + soc_l / 256.0f;
}

void Cw2015::quick_start() {
    uint8_t mode = ReadReg(CW2015_REG_MODE);
    mode |= 0x30; // 设置 QUICK_START 位
    WriteRegs(CW2015_REG_MODE, &mode, 1);
}

void Cw2015::sleep() {
    uint8_t mode = ReadReg(CW2015_REG_MODE);
    mode |= 0x80; // 设置 SLEEP 位
    WriteRegs(CW2015_REG_MODE, &mode, 1);
}

void Cw2015::wakeup() {
    uint8_t mode = ReadReg(CW2015_REG_MODE);
    mode &= ~0x80; // 清除 SLEEP 位
    WriteRegs(CW2015_REG_MODE, &mode, 1);
}