#include "servo_controller.h"
void test_sweep() {
    ESP_LOGI("TEST", "Inizio Sweep: da MIN a MAX...");
    // Muove il servo da MIN a MAX a velocità moderata (1.5 rad/s)
    move_servo_speed(servo_data.max_pos, 1.5f);
    vTaskDelay(pdMS_TO_TICKS(4000)); // Attende che finisca
    
    move_servo_speed(servo_data.min_pos, 1.5f);
    vTaskDelay(pdMS_TO_TICKS(4000));
}

void test_precision() {
    ESP_LOGI("TEST", "Test precisione: 0 -> 0.5rad -> 0 -> -0.5rad");
    
    move_servo_speed(0.5f, 2.0f);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    move_servo_speed(0.0f, 2.0f);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    move_servo_speed(-0.5f, 2.0f);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    move_servo_speed(0.0f, 2.0f);
}

void test_reactivity() {
    ESP_LOGI("TEST", "Test reattività: cambio direzione al volo");
    
    // Invia ordine di andare a +1.0 rad
    move_servo_speed(1.0f, 1.0f);
    vTaskDelay(pdMS_TO_TICKS(500)); // Aspetta mezzo secondo
    
    // Invia ordine contrario: dovrebbe ignorare il primo e invertire
    move_servo_speed(-1.0f, 2.0f); 
}

void test_speed_ramp() {
    ESP_LOGI("TEST", "Test velocità crescente");
    
    float speeds[] = {0.5f, 1.5f, 3.0f}; // Rad/s
    
    for(int i=0; i<3; i++) {
        move_servo_speed(servo_data.max_pos, speeds[i]);
        vTaskDelay(pdMS_TO_TICKS(3000));
        move_servo_speed(servo_data.min_pos, speeds[i]);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

extern "C" void app_main() {
    servo_init();

    while(1) {
        test_sweep();
        vTaskDelay(pdMS_TO_TICKS(5000));

        test_precision();
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        test_reactivity();
        vTaskDelay(pdMS_TO_TICKS(5000));

        test_speed_ramp();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}