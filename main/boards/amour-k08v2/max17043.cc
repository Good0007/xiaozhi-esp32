#include "max17043.h"
#include <algorithm>
#include "esp_log.h"
#define TAG "Max17043"
Max17043::Max17043(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
}

int Max17043::get_ic_version() {
    return (ReadReg(MAX17048_VERSION_REG));
}

esp_err_t Max17043::rest() {
    uint8_t buf[2] = {0x54, 0x00};
      WriteRegs(MAX17048_CMD_REG, buf, 2);
     
    return ESP_OK;
}

float Max17043::get_cell_voltage()
{
    float voltage;
    uint8_t buf[2] = {0x00};
    ReadRegs(MAX17048_VCELL_REG, buf, 2);
    uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];
    voltage = 1.0f * result * 78.125f / 1000000.0f;                                       /*!< Unit is 78.125 μV/cell, needs to be converted to volts */
    return voltage;
}

float Max17043::get_cell_percent()
{
    float percent;
    uint8_t buf[2] = {0x00};
    ReadRegs(MAX17048_SOC_REG, buf, 2);
    uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];
    percent = 1.0f * result / 256.0f;   
    // ESP_LOGI(TAG, "get_cell_percent: %f", percent);                                                /*!< Unit is 1%/256 */
    return percent;
}

float Max17043::get_charge_rate()
{
    float percent;
    uint8_t buf[2] = {0x00};
    ReadRegs(MAX17048_CRATE_REG, buf, 2);
    int16_t result = (int16_t)((buf[0] << 8) | buf[1]);
    percent = (float)(result) * 0.208f;                                               /*!< Unit is 0.208%/hr */
    return percent;
}
 
esp_err_t Max17043::get_alert_voltage(float *minv, float *maxv){
    if (minv == NULL || maxv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t result[2] = {0x00};
    ReadRegs(MAX17048_VALERT_REG, result, 2);
    *minv = (float)result[0] * 0.02f;                                                     /*!< Unit is 20mV */
    *maxv = (float)result[1] * 0.02f;                                                     /*!< Unit is 20mV */

    return ESP_OK;
}

esp_err_t Max17043::set_alert_volatge(float minv, float maxv) {
    if (minv == NULL || maxv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t minv_int = (uint8_t)std::min(255, std::max(0, (int)(minv / 0.02f)));
    uint8_t maxv_int = (uint8_t)std::min(255, std::max(0, (int)(maxv / 0.02f)));
    WriteReg(MAX17048_VALERT_REG, minv_int);
    WriteReg(MAX17048_VALERT_REG + 1, maxv_int);
    return ESP_OK;
}

esp_err_t Max17043::get_alert_flag(uint8_t *flag) {
    if (flag == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *flag = ReadReg(MAX17048_STATUS_REG);
    *flag = (*flag) & 0x7F;
    return ESP_OK;
}

esp_err_t Max17043::clear_alert_flag(max17048_alert_flag_t flag) {
    if (flag == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t status = 0;
    status = ReadReg(MAX17048_STATUS_REG);
    status = status & ~flag;
    WriteReg(MAX17048_STATUS_REG, status);
    return ESP_OK;
}


// esp_err_t max17048_clear_alert_status_bit(max17048_handle_t sensor)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t buf[2];
//     esp_err_t ret = i2c_bus_read_bytes(sens->i2c_dev, MAX17048_CONFIG_REG, 2, buf);
//     if (ret != ESP_OK) {
//         return ESP_FAIL;
//     }

//     uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];

//     result &= ~(1 << 5);                                                               /*!< Clear the alert status bit */
//     buf[0] = (uint8_t)(result >> 8);
//     buf[1] = (uint8_t)(result & 0xFF);
//     return i2c_bus_write_bytes(sens->i2c_dev, MAX17048_CONFIG_REG, 2, buf);
// }

// esp_err_t max17048_get_hibernation_threshold(max17048_handle_t sensor, float *threshold)
// {
//     if (sensor == NULL || threshold == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t result = 0;
//     esp_err_t ret = i2c_bus_read_byte(sens->i2c_dev, MAX17048_HIBRT_REG, &result);
//     if (ret != ESP_OK) {
//         return ret;
//     }
//     *threshold = (float)(result) * 0.208f;
//     return ESP_OK;
// }

// esp_err_t max17048_set_hibernation_threshold(max17048_handle_t sensor, float threshold)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t threshold_int = (uint8_t)min(255, max(0, (int)(threshold / 0.208f)));
//     esp_err_t ret = i2c_bus_write_byte(sens->i2c_dev, MAX17048_HIBRT_REG, threshold_int);
//     if (ret != ESP_OK) {
//         return ESP_FAIL;
//     }
//     return ESP_OK;
// }

// esp_err_t max17048_get_active_threshold(max17048_handle_t sensor, float *threshold)
// {
//     if (sensor == NULL || threshold == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t result = 0;
//     esp_err_t ret = i2c_bus_read_byte(sens->i2c_dev, MAX17048_HIBRT_REG + 1, &result);
//     if (ret != ESP_OK) {
//         return ESP_FAIL;
//     }
//     *threshold = (float)(result) * 0.00125f;
//     return ESP_OK;
// }

// esp_err_t max17048_set_active_threshold(max17048_handle_t sensor, float threshold)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t threshold_int = (uint8_t)min(255, max(0, (int)(threshold / 0.00125f)));
//     esp_err_t ret = i2c_bus_write_byte(sens->i2c_dev, MAX17048_HIBRT_REG + 1, threshold_int);
//     if (ret != ESP_OK) {
//         return ESP_FAIL;
//     }
//     return ESP_OK;
// }

// esp_err_t max17048_enter_hibernation_mode(max17048_handle_t sensor)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t buf[2] = {0xFF, 0xFF};
//     return i2c_bus_write_bytes(sens->i2c_dev, MAX17048_HIBRT_REG, 2, buf);
// }

// esp_err_t max17048_exit_hibernation_mode(max17048_handle_t sensor)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t buf[2] = {0x00, 0x00};
//     return i2c_bus_write_bytes(sens->i2c_dev, MAX17048_HIBRT_REG, 2, buf);
// }

// esp_err_t max17048_is_hibernate(max17048_handle_t sensor, bool *is_hibernate)
// {
//     if (sensor == NULL || is_hibernate == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t buf[2];
//     esp_err_t ret = i2c_bus_read_bytes(sens->i2c_dev, MAX17048_MODE_REG, 2, buf);
//     if (ret != ESP_OK) {
//         return ESP_FAIL;
//     }

//     uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];
//     if (result & (1 << 12)) {
//         *is_hibernate = true;
//     } else {
//         *is_hibernate = false;
//     }
//     return ret;
// }

// esp_err_t max17048_get_reset_voltage(max17048_handle_t sensor, float *voltage)
// {
//     if (sensor == NULL || voltage == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t result = 0;
//     esp_err_t ret = i2c_bus_read_byte(sens->i2c_dev, MAX17048_VRESET_REG, &result);
//     if (ret != ESP_OK) {
//         return ESP_FAIL;
//     }

//     *voltage = result * 0.04f;
//     return ESP_OK;
// }

// esp_err_t max17048_set_reset_voltage(max17048_handle_t sensor, float voltage)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t voltage_int = (uint8_t)max(min((int)(voltage / 0.04f), 127), 0);
//     esp_err_t ret = i2c_bus_write_byte(sens->i2c_dev, MAX17048_VRESET_REG, voltage_int);
//     return ret;
// }

// esp_err_t max17048_set_reset_voltage_alert_enabled(max17048_handle_t sensor, bool enabled)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t buf[2] = {0x00};
//     esp_err_t ret = i2c_bus_read_bytes(sens->i2c_dev, MAX17048_STATUS_REG, 2, buf);
//     if (ret != ESP_OK) {
//         return ret;
//     }
//     uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];

//     if (enabled) {
//         result |= (1 << 14);
//     } else {
//         result &= ~(1 << 14);
//     }

//     buf[0] = (uint8_t)(result >> 8);
//     buf[1] = (uint8_t)(result & 0xFF);
//     return i2c_bus_write_bytes(sens->i2c_dev, MAX17048_STATUS_REG, 2, buf);
// }

// esp_err_t max17048_is_reset_voltage_alert_enabled(max17048_handle_t sensor, bool *enabled)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t buf[2] = {0x00};
//     esp_err_t ret = i2c_bus_read_bytes(sens->i2c_dev, MAX17048_STATUS_REG, 2, buf);
//     if (ret != ESP_OK) {
//         return ret;
//     }

//     uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];
//     if (result & (1 << 14)) {
//         *enabled = true;
//     } else {
//         *enabled = false;
//     }
//     return ESP_OK;
// }

// esp_err_t max17048_temperature_compensation(max17048_handle_t sensor, float temperature)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     uint8_t temp_int = 0;
//     if (temperature > 20.0f) {
//         temp_int = 0x97 + (temperature - 20.0f) * -0.5f;
//     } else {
//         temp_int = 0x97 + (temperature - 20.0f) * -5.0f;
//     }
//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     esp_err_t ret = i2c_bus_write_byte(sens->i2c_dev, MAX17048_CONFIG_REG, temp_int);
//     return ret;
// }

// esp_err_t max17048_set_sleep_enabled(max17048_handle_t sensor, bool enabled)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t buf[2];
//     esp_err_t ret = i2c_bus_read_bytes(sens->i2c_dev, MAX17048_MODE_REG, 2, buf);

//     if (ret != ESP_OK) {
//         return ret;
//     }

//     uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];
//     if (enabled) {
//         result |= (1 << 13);
//     } else {
//         result &= ~(1 << 13);
//     }

//     buf[0] = (uint8_t)(result >> 8);
//     buf[1] = (uint8_t)(result & 0xFF);

//     return i2c_bus_write_bytes(sens->i2c_dev, MAX17048_CONFIG_REG, 2, buf);
// }

// esp_err_t max17048_set_sleep(max17048_handle_t sensor, bool sleep)
// {
//     if (sensor == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;
//     uint8_t buf[2];
//     esp_err_t ret = i2c_bus_read_bytes(sens->i2c_dev, MAX17048_CONFIG_REG, 2, buf);    /*!< It is not possible to individually perform read/write operations on address 0x0D */

//     if (ret != ESP_OK) {
//         return ESP_FAIL;
//     }

//     uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];
//     if (sleep) {
//         result |= (1 << 7);
//     } else {
//         result &= ~(1 << 7);
//     }

//     buf[0] = (uint8_t)(result >> 8);
//     buf[1] = (uint8_t)(result & 0xFF);

//     return i2c_bus_write_bytes(sens->i2c_dev, MAX17048_CONFIG_REG, 2, buf);
// }

// esp_err_t max17048_is_sleeping(max17048_handle_t sensor, bool *is_sleeping)
// {
//     if (sensor == NULL || is_sleeping == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     max17048_sensor_t *sens = (max17048_sensor_t *)sensor;

//     uint8_t buf[2];
//     esp_err_t ret = i2c_bus_read_bytes(sens->i2c_dev, MAX17048_CONFIG_REG, 2, buf);    /*!< It is not possible to individually perform read/write operations on address 0x0D */
//     if (ret != ESP_OK) {
//         return ret;
//     }

//     uint16_t result = ((uint16_t)buf[0] << 8) | buf[1];
//     if (result & (1 << 7)) {
//         *is_sleeping = true;
//     } else {
//         *is_sleeping = false;
//     }
//     return ESP_OK;
// }
