#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"

#include "nvs_flash.h"

#include "esp_bt.h"

#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"

#define VERSION "0.1-beta"
#define GPIO_PORT_CHARGE_RELAY 5
#define GPIO_PORT_VENTILATOR_RELAY 6
#define GPIO_PORT_THERMOSTAT 7

#define BLE_ADVERTISING_NAME "ILS3000"

#define HIGH 1
#define LOW 0

#define GATTS_VAR_LEN_MAX 1024

#define GATTS_SERVICE_UUID_TEST_CONFIG   0x00FF
#define GATTS_CHAR_UUID_TEST_CONFIG      0xFF01
#define GATTS_DESCR_UUID_TEST_CONFIG     0x3333
#define GATTS_NUM_HANDLE_TEST_CONFIG     4

#define GATTS_SERVICE_UUID_TEST_INFO     0x00EE
#define GATTS_CHAR_UUID_TEST_INFO        0xEE01
#define GATTS_DESCR_UUID_TEST_INFO       0x2222
#define GATTS_NUM_HANDLE_TEST_INFO       4

static const char* LOG_TAG = "ils3000-arduino.main";

static uint8_t char1_str[] = {0x11,0x22,0x33};
static esp_gatt_char_prop_t config_property = 0;
static esp_gatt_char_prop_t info_property = 0;

static esp_attr_value_t gatts_demo_char1_val =
        {
                // idk why 40 but google said so
                .attr_max_len = 0x40,
                .attr_len     = sizeof(char1_str),
                .attr_value   = char1_str,
        };

static uint8_t adv_config_done = 0;
#define adv_config_flag      (1 << 0)
#define scan_rsp_config_flag (1 << 1)

static esp_ble_adv_params_t ble_adv_params = {
        .adv_int_min        = 0x20,
        .adv_int_max        = 0x40,
        .adv_type           = ADV_TYPE_NONCONN_IND,
        .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
        .channel_map        = ADV_CHNL_ALL,
        .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t adv_service_uuid128[32] = {
        /* LSB <--------------------------------------------------------------------------------> MSB */
        //first uuid, 16bit, [12],[13] is the value
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xEE, 0x00, 0x00, 0x00,
        //second uuid, 32bit, [12], [13], [14], [15] is the value
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

static esp_ble_adv_data_t ble_adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = false,
        .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
        .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
        .appearance = 0x00,
        .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
        .p_manufacturer_data =  NULL, //&test_manufacturer[0],
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(adv_service_uuid128),
        .p_service_uuid = adv_service_uuid128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// scan response data
static esp_ble_adv_data_t scan_rsp_data = {
        .set_scan_rsp = true,
        .include_name = true,
        .include_txpower = true,
        //.min_interval = 0x0006,
        //.max_interval = 0x0010,
        .appearance = 0x01,
        .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
        .p_manufacturer_data =  NULL, //&test_manufacturer[0],
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(adv_service_uuid128),
        .p_service_uuid = adv_service_uuid128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// PROFILE_NUM = |profiles|
#define PROFILE_NUM 2
#define PROFILE_CONFIG_APP_ID 0
#define PROFILE_INFO_APP_ID  1

static void gatts_profile_config_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_info_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
        [PROFILE_CONFIG_APP_ID] = {
                .gatts_cb = gatts_profile_config_event_handler,
                .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        },
        [PROFILE_INFO_APP_ID] = {
                .gatts_cb = gatts_profile_info_event_handler,
                .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        },
};


int charge_relay_state = LOW;
int ventilator_relay_state = LOW;

int swap_high_low(int* state)
{
    *state = 1 - *state;
    return *state;
}

char* stringify_high_low(int state)
{
    return state == HIGH ? "HIGH" : "LOW";
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) 
{
    switch (event) {
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            //advertising start complete event to indicate advertising start successfully or failed
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(LOG_TAG, "Advertising start failed");
            } else {
                ESP_LOGI(LOG_TAG, "Successfully started advertising");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(LOG_TAG, "Advertising stop failed");
            } else {
                ESP_LOGI(LOG_TAG, "Stop advertising successfully");
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(LOG_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                     param->update_conn_params.status,
                     param->update_conn_params.min_int,
                     param->update_conn_params.max_int,
                     param->update_conn_params.conn_int,
                     param->update_conn_params.latency,
                     param->update_conn_params.timeout);
            break;
        default:
            ESP_LOGI(LOG_TAG, "received event %d", event);
            break;
    }
}

void bluetooth_advertise_ils()
{
    esp_err_t ret;
    ret = esp_ble_gap_config_adv_data(&ble_adv_data);
    if(ret)
    {
        ESP_LOGE(LOG_TAG, "%s failed to set advertising data (ble_adv_data) on function call esp_ble_gap_config_adv_data: %s", __func__, esp_err_to_name(ret));
    }
    esp_ble_gap_start_advertising(&ble_adv_params);
}

static void gatts_profile_info_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{

}

static void gatts_profile_config_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(LOG_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
            esp_ble_gap_start_advertising(&ble_adv_params);
            break;
        case ESP_GATTS_REG_EVT:
            // https://github.com/espressif/esp-idf/blob/v5.2.1/examples/bluetooth/bluedroid/ble/gatt_server/tutorial/Gatt_Server_Example_Walkthrough.md#creating-services
            ESP_LOGI(LOG_TAG, "REGISTER_APP_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);
            gl_profile_tab[PROFILE_CONFIG_APP_ID].service_id.is_primary = true;
            gl_profile_tab[PROFILE_CONFIG_APP_ID].service_id.id.inst_id = 0x00;
            gl_profile_tab[PROFILE_CONFIG_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_CONFIG_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_CONFIG;

            esp_err_t ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
            esp_ble_gap_set_device_name(BLE_ADVERTISING_NAME);
            adv_config_done |= adv_config_flag;

            esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_CONFIG_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_CONFIG);
        break;
        case ESP_GATTS_CREATE_EVT:
            ESP_LOGI(LOG_TAG, "CREATE_SERVICE_EVT, status %d, service_handle %d", param->create.status, param->create.service_handle);
            gl_profile_tab[PROFILE_CONFIG_APP_ID].service_handle = param->create.service_handle;
            gl_profile_tab[PROFILE_CONFIG_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_CONFIG_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_CONFIG;

            esp_ble_gatts_start_service(gl_profile_tab[PROFILE_CONFIG_APP_ID].service_handle);
            config_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
            esp_err_t add_char_ret =
                    esp_ble_gatts_add_char(gl_profile_tab[PROFILE_CONFIG_APP_ID].service_handle,
                                           &gl_profile_tab[PROFILE_CONFIG_APP_ID].char_uuid,
                                           ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                           config_property,
                                           &gatts_demo_char1_val,
                                           NULL);
            if (add_char_ret){
                ESP_LOGE(LOG_TAG, "add char failed, error code =%x",add_char_ret);
            }
            break;
        case ESP_GATTS_ADD_CHAR_EVT: {
            uint16_t length = 0;
            const uint8_t *prf_char;

            ESP_LOGI(LOG_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d",
                     param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
            gl_profile_tab[PROFILE_CONFIG_APP_ID].char_handle = param->add_char.attr_handle;
            gl_profile_tab[PROFILE_CONFIG_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_CONFIG_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_err_t get_attr_ret = esp_ble_gatts_get_attr_value(param->add_char.attr_handle, &length, &prf_char);
            if (get_attr_ret == ESP_FAIL){
                ESP_LOGE(LOG_TAG, "ILLEGAL HANDLE");
            }
            ESP_LOGI(LOG_TAG, "the gatts demo char length = %x", length);
            for(int i = 0; i < length; i++){
                ESP_LOGI(LOG_TAG, "prf_char[%x] = %x",i,prf_char[i]);
            }
            esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(
                    gl_profile_tab[PROFILE_CONFIG_APP_ID].service_handle,
                    &gl_profile_tab[PROFILE_CONFIG_APP_ID].descr_uuid,
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                    NULL,NULL);
            if (add_descr_ret){
                ESP_LOGE(LOG_TAG, "add char descr failed, error code = %x", add_descr_ret);
            }
            break;
        }
        case ESP_GATTS_CONNECT_EVT: {
            ESP_LOGI(LOG_TAG, "received ESP_GATTS_CONNECT_EVT %s", __func__);
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.latency = 0;
            conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = 400;     // timeout = 400*10ms = 4000ms

            ESP_LOGI(LOG_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                     param->connect.conn_id,
                     param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                     param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            gl_profile_tab[PROFILE_CONFIG_APP_ID].conn_id = param->connect.conn_id;
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        }
        case ESP_GATTS_START_EVT:
            ESP_LOGI(LOG_TAG, "SERVICE_START_EVT, status %d, service_handle %d",
                     param->start.status, param->start.service_handle);
        default:
            ESP_LOGI(LOG_TAG, "Received Event %d", event);
            break;
    }
}

// https://github.com/espressif/esp-idf/blob/5a40bb8746633477c07ff9a3e90016c37fa0dc0c/examples/bluetooth/bluedroid/ble/gatt_server/main/gatts_demo.c#L650
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
    ESP_LOGI(LOG_TAG, "----> gatts_event_handler %d", event);
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            ESP_LOGI(LOG_TAG, "reg.app_id=%d", param->reg.app_id);
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGI(LOG_TAG, "Reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }

    /* If the gatts_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gatts_if == gl_profile_tab[idx].gatts_if) {
                if (gl_profile_tab[idx].gatts_cb) {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}


void setup_bluetooth()
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s failed to start nvs (nvs_flash_init) %s", __func__, esp_err_to_name(ret));
        return;
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));


    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    // only one client allowed at once
    //bt_cfg.ble_max_conn = 1;

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s initialize controller failed", __func__);
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s enable controller failed", __func__);
        return;
    }

    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();

    ret = esp_bluedroid_init_with_cfg(&bluedroid_config);

    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s failed to enable bluetooth (esp_bluedroid_init_with_cfg) %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "%s enable bluetooth failed (esp_bluedroid_enable) %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_dev_set_device_name(BLE_ADVERTISING_NAME);
    if(ret)
    {
        ESP_LOGE(LOG_TAG, "%s failed to set bluetooth advertising name (esp_bt_dev_set_device_name) %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler); // todo ret
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "gap register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatts_register_callback(gatts_event_handler); // todo ret
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "gatts register error, error code = %x", ret);
        return;
    }
    // PROFILE_CONFIG_APP_ID = 0
    ret = esp_ble_gatts_app_register(PROFILE_CONFIG_APP_ID); // todo ret
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "gatts CONFIG app register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatts_app_register(PROFILE_INFO_APP_ID); // todo ret
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "gatts app register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatt_set_local_mtu(500); // todo ret
    if (ret)
    {
        ESP_LOGE(LOG_TAG, "set local  MTU failed, error code = %x", ret);
    }
}


void measure_temperature()
{

}

void toggle_charge_relay()
{
    ESP_LOGI(LOG_TAG, "Will toggle charging relay. (%s -> %s)", stringify_high_low(charge_relay_state), stringify_high_low(swap_high_low(&charge_relay_state)));

}

void toggle_ventilator_relay()
{
    ESP_LOGI(LOG_TAG, "Will toggle ventilator relay. (%s -> %s)", stringify_high_low(ventilator_relay_state), stringify_high_low(swap_high_low(&ventilator_relay_state)));
}

void print_gpio_ports_to_serial()
{
    ESP_LOGI(LOG_TAG, "Following GPIO ports will be used:");
    ESP_LOGI(LOG_TAG, "GPIO_PORT_CHARGE_RELAY: %d", GPIO_PORT_CHARGE_RELAY);
    ESP_LOGI(LOG_TAG, "GPIO_PORT_VENTILATOR_RELAY: %d", GPIO_PORT_VENTILATOR_RELAY);
    ESP_LOGI(LOG_TAG, "GPIO_PORT_THERMOSTAT: %d", GPIO_PORT_THERMOSTAT);
}

void app_main(void)
{
    ESP_LOGI(LOG_TAG, "**** ILS3000 ARDUINO CONTROLLER (%s)****", VERSION);
    print_gpio_ports_to_serial();

    setup_bluetooth();
    bluetooth_advertise_ils();
}
