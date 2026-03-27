#pragma GCC optimize ("Os")
#include "servo_controller.h"
#include "utils_uart_comms.h"
#include "init_wifi.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_log.h"

void init_cube();
// void task_execute_servo(void *arg);

struct {
    uint8_t mac[6];
} molecube_data;

void init_cube() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI("CUBE_INIT", "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    memcpy(molecube_data.mac, mac, 6); //copying the mac address byte to byte to the molecube_data struct
}

void handle_movement_ack(Msg* msg){
    //TODO complete this function, send the ack to the ui?
}

// Task: receive Msg* from the higher-level UART queue and translate into
// servo controller commands by calling move_servo_speed()
// this is needed beacause also the master has a servo

void task_execute_servo(void *arg) {
    (void)arg;
    extern QueueHandle_t h_queue_servo; // declared in utils_uart_comms.h / GLOBAL_VARS.cpp

    while (1) {
        Msg *msg = nullptr;
        if (xQueueReceive(h_queue_servo, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI("EXEC_SERVO", "Received servo message");
            if (msg) {
                float radians = msg->payload.payload_servo.radians;
                float speed = msg->payload.payload_servo.speed;
                esp_err_t err = move_servo_speed(radians, speed, servo_data.max_acc, servo_data.max_jerk);
                if (err != ESP_OK) {
                    ESP_LOGW("EXEC_SERVO", "move_servo_speed failed: %d", err);
                }
                delete msg; // free message allocated by UART layer
            }
        }
    }
}

extern "C" void app_main() {
    // Print reset reason early to determine if the board was reset or app_main returned
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reason_str;
    switch(reason){
        case ESP_RST_UNKNOWN: reason_str = "UNKNOWN"; break;
        case ESP_RST_POWERON: reason_str = "POWERON"; break;
        case ESP_RST_EXT: reason_str = "EXTERNAL_RESET"; break;
        case ESP_RST_SW: reason_str = "SOFTWARE_RESET"; break;
        case ESP_RST_PANIC: reason_str = "PANIC"; break;
        case ESP_RST_INT_WDT: reason_str = "INT_WDT"; break;
        case ESP_RST_TASK_WDT: reason_str = "TASK_WDT"; break;
        case ESP_RST_WDT: reason_str = "WDT"; break;
        case ESP_RST_DEEPSLEEP: reason_str = "DEEPSLEEP"; break;
        case ESP_RST_BROWNOUT: reason_str = "BROWNOUT"; break;
        case ESP_RST_SDIO: reason_str = "SDIO"; break;
        default: reason_str = "OTHER"; break;
    }
    ESP_LOGI("BOOT", "Reset reason: %d (%s)", reason, reason_str);

    //initializing wifi, uart comms, cube data (mac address) and servo controller
    init_wifi();
    init_uart_comms();
    init_cube();
    servo_init();

    // create and start the task that listens for servo messages coming from
    // the UART/protocol layer and forwards movement commands to the
    // servo controller (move_servo_speed)
    xTaskCreate(
        task_execute_servo,
        "ExecServoTask",
        3072,
        NULL,
        2,
        NULL
    );

}

