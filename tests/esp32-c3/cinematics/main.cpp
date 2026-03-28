#include "servo_controller.h"
#include "utils_uart_comms.h"
#include "servo_tests.h"
#include "esp_log.h"
#include "esp_mac.h"
struct {
    uint8_t mac[6];
} molecube_data;

void init_cube() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI("CUBE_INIT", "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    memcpy(molecube_data.mac, mac, 6); //copying the mac address byte to byte to the molecube_data struct
}
extern "C" void app_main() {
    ESP_LOGI("TEST", "Starting servo tests...");
    init_uart_comms();
    init_cube();
    servo_init();
    while(true){
        vTaskDelay(pdMS_TO_TICKS(5000));
        test_sweep();
        vTaskDelay(pdMS_TO_TICKS(5000));
        test_precision();
        vTaskDelay(pdMS_TO_TICKS(5000));
        test_reactivity();
        vTaskDelay(pdMS_TO_TICKS(5000));
        test_speed_ramp();
        vTaskDelay(pdMS_TO_TICKS(5000));
        test_acceleration();
        vTaskDelay(pdMS_TO_TICKS(5000));
        test_jerk();
    }
}