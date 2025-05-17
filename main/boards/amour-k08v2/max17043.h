/*****************************************************************************
 *
 * @file:    max17043.h
 * @brief:   
 * @author:  felixliang (felixlian@gmail.com)
 * @date:    2025/03/29
 * @version: v0.0.1
 * @history: 
 *
 ****************************************************************************/

#ifndef __MAX17043_H__
#define __MAX17043_H__

#include "i2c_device.h"

#define MAX17048_I2C_ADDR_DEFAULT 0x36
#define MAX17048_VCELL_REG 0x02    /*!< ADC measurement of VCELL. */
#define MAX17048_SOC_REG 0x04      /*!< Battery state of charge. */
#define MAX17048_MODE_REG 0x06     /*!< Initiates quick-start, reports hibernate mode, and enables sleep mode. */
#define MAX17048_VERSION_REG 0x08  /*!< IC production version. */
#define MAX17048_HIBRT_REG 0x0A    /*!< Controls thresholds for entering and exiting hibernate mode. */
#define MAX17048_CONFIG_REG 0x0C   /*!< Compensation to optimize performance, sleep mode, alert indicators, and configuration. */
#define MAX17048_VALERT_REG 0x14   /*!< Configures the VCELL range outside of which alerts are generated. */
#define MAX17048_CRATE_REG 0x16    /*!< Approximate charge or discharge rate of the battery. */
#define MAX17048_VRESET_REG 0x18   /*!< Configures VCELL threshold below which the IC resets itself, ID is a one-time factory-programmable identifier. */
#define MAX17048_CHIPID_REG 0x19   /*!< Register that holds semi-unique chip ID. */
#define MAX17048_STATUS_REG 0x1A   /*!< Indicates overvoltage, undervoltage, SOC change, SOC low, and reset alerts. */
#define MAX17048_CMD_REG 0xFE      /*!< Sends POR command. */

typedef enum {
    MAX17048_ALERT_FLAG_RESET_INDICATOR = 0x01, /*!< RESET_INDICATOR is set when the device powers up. */
    MAX17048_ALERT_FLAG_VOLTAGE_HIGH = 0x02,    /*!< VOLTAGE_HIGH is set when VCELL has been above ALRT.VALRTMAX. */
    MAX17048_ALERT_FLAG_VOLTAGE_LOW = 0x04,     /*!< VOLTAGE_LOW is set when VCELL has been below ALRT.VALRTMIN. */
    MAX17048_ALERT_FLAG_VOLTAGE_RESET = 0x08,   /*!< VOLTAGE_RESET is set after the device has been reset if EnVr is set. */
    MAX17048_ALERT_FLAG_SOC_LOW = 0x10,         /*!< SOC_LOW is set when SOC crosses the value in CONFIG.ATHD. */
    MAX17048_ALERT_FLAG_SOC_CHARGE = 0x20,      /*!< SOC_CHARGE is set when SOC changes by at least 1% if CONFIG.ALSC is set. */
} max17048_alert_flag_t;

class Max17043 : public I2cDevice {
public:
    Max17043(i2c_master_bus_handle_t i2c_bus, uint8_t addr);
    int get_ic_version();
    esp_err_t rest();
    float get_cell_voltage();
    float get_cell_percent();
    float get_charge_rate();

    esp_err_t get_alert_voltage(float *minv, float *maxv);
    esp_err_t set_alert_volatge(float minv, float maxv);
    esp_err_t get_alert_flag(uint8_t *flag);
    esp_err_t clear_alert_flag(max17048_alert_flag_t flag);
    // bool IsCharging();
    // bool IsDischarging();
    // bool IsChargingDone();
    // int GetBatteryLevel();
    // void PowerOff();

private:
    // int GetBatteryCurrentDirection();
};

#endif
 