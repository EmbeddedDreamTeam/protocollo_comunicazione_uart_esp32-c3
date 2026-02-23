#include "utils.h"

//* _______________________________________LED
void toggle_led(bool s){
    gpio_set_level(LED_GPIO, !s);
}

void init_led(){
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    toggle_led(0);
}

void task_blink_led_once(void *arg){
    int L_DELAY = (int32_t)arg;
    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        toggle_led(1);
        vTaskDelay(pdMS_TO_TICKS(L_DELAY));
        toggle_led(0);
    }
}

void task_blink_led_continously(void *arg){
    int L_DELAY = (int32_t)arg;
    init_led();
    while(1){
        toggle_led(0);
        vTaskDelay(pdMS_TO_TICKS(L_DELAY));
        toggle_led(1);
        vTaskDelay(pdMS_TO_TICKS(L_DELAY));
    }
}