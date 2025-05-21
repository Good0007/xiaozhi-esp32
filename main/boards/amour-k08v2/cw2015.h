#include <stdint.h>
#include <stdbool.h>
#include "i2c_device.h"

// CW2015 I2C地址
#define CW2015_ADDR             0x62

// CW2015寄存器定义
#define REG_VERSION             0x00
#define REG_VCELL               0x02
#define REG_SOC                 0x04
#define REG_RRT_ALERT           0x06
#define REG_CONFIG              0x08
#define REG_MODE                0x0A
#define REG_BATINFO             0x10

// Mode Masks
#define MODE_SLEEP_MASK         (0x3<<6)
#define MODE_SLEEP              (0x3<<6)
#define MODE_NORMAL             (0x0<<6)
#define MODE_QUICK_START        (0x3<<4)
#define MODE_RESTART            (0xF<<0)

// Config Flags
#define CONFIG_UPDATE_FLG       (0x1<<1)
#define ATHD                    (0x0<<3)  // ATHD = 0%

// Battery Parameters
#define BATTERY_UP_MAX_CHANGE         720
#define BATTERY_DOWN_MIN_CHANGE       60
#define BATTERY_DOWN_MIN_CHANGE_SLEEP 1800
#define SIZE_BATINFO                  64

// 电池参数区（根据实际电池配置）
static const uint8_t cw_bat_config_info[SIZE_BATINFO] = {
    0x15, 0x7E, 0x7C, 0x5C, 0x64, 0x6A, 0x65, 0x5C, 
    0x55, 0x53, 0x56, 0x61, 0x6F, 0x66, 0x50, 0x48,
    0x43, 0x42, 0x40, 0x43, 0x4B, 0x5F, 0x75, 0x7D,
    0x52, 0x44, 0x07, 0xAE, 0x11, 0x22, 0x40, 0x56,
    0x6C, 0x7C, 0x85, 0x86, 0x3D, 0x19, 0x8D, 0x1B,
    0x06, 0x34, 0x46, 0x79, 0x8D, 0x90, 0x90, 0x46,
    0x67, 0x80, 0x97, 0xAF, 0x80, 0x9F, 0xAE, 0xCB,
    0x2F, 0x00, 0x64, 0xA5, 0xB5, 0x11, 0xD0, 0x11
};

// 电池信息结构体
typedef struct {
    uint8_t usb_online;
    uint32_t capacity;
    uint32_t voltage;
    uint8_t alt;
} CW_Battery;

class Cw2015 : public I2cDevice{
public:
    Cw2015(i2c_master_bus_handle_t i2c_bus, uint8_t addr = CW2015_ADDR);

    bool begin();
    bool powerOnReset();
    bool config();
    bool updateConfigInfo();
    bool setAthd(uint8_t new_athd);

    int getCapacity();
    int getVoltage();
    bool updateCapacity();
    bool updateVoltage();
    bool releaseAlert();
    // 获取电池 get_cell_percent
    float get_cell_percent();

    CW_Battery battery;

private:
    int reset_loop = 0;
    int allow_no_charger_full = 0;
    int no_charger_full_jump = 0;
    int allow_charger_always_zero = 0;
    int if_quickstart = 0;
};
