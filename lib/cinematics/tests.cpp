#include "servo_controller.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
void test_sweep() {
    ESP_LOGI("TEST", "Inizio Sweep: da MIN a MAX...");
    // Muove il servo da MIN a MAX a velocità moderata (1.5 rad/s)
    move_servo_speed(servo_data.max_pos, 1.5f, servo_data.max_acc, servo_data.max_jerk);
    vTaskDelay(pdMS_TO_TICKS(4000)); // Attende che finisca
    
    move_servo_speed(servo_data.min_pos, 1.5f, servo_data.max_acc, servo_data.max_jerk);
    vTaskDelay(pdMS_TO_TICKS(4000));
}

void test_precision() {
    ESP_LOGI("TEST", "Test precisione: 0 -> 0.5rad -> 0 -> -0.5rad");
    
    move_servo_speed(0.5f, 2.0f, servo_data.max_acc, servo_data.max_jerk);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    move_servo_speed(0.0f, 2.0f, servo_data.max_acc, servo_data.max_jerk);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    move_servo_speed(-0.5f, 2.0f, servo_data.max_acc, servo_data.max_jerk);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    move_servo_speed(0.0f, 2.0f, servo_data.max_acc, servo_data.max_jerk);
}

void test_reactivity() {
    ESP_LOGI("TEST", "Test reattività: cambio direzione al volo");
    
    // Invia ordine di andare a +1.0 rad
    move_servo_speed(1.0f, 1.0f, servo_data.max_acc, servo_data.max_jerk);
    vTaskDelay(pdMS_TO_TICKS(500)); // Aspetta mezzo secondo
    
    // Invia ordine contrario: dovrebbe ignorare il primo e invertire
    move_servo_speed(-1.0f, 2.0f, servo_data.max_acc, servo_data.max_jerk);
}

void test_speed_ramp() {
    ESP_LOGI("TEST", "Test velocità crescente");
    
    float speeds[] = {0.1f, 1.5f, 3.0f}; // Rad/s
    float pos;
    for(int i=0; i<3; i++) {
        pos=servo_data.current_pos.load();
        ESP_LOGI("TEST", "Test velocità crescente: speed=%f", speeds[i]);
        ESP_LOGI("TEST", "Posizione attuale: %f rad, posizione target: %f rad", pos, servo_data.max_pos);
        move_servo_speed(servo_data.max_pos, speeds[i], servo_data.max_acc, servo_data.max_jerk); //TODO qualche problema con le velocità e i tempi di attesa, inoltre sembrano esserci problemi di costanza nelle velocità
        ESP_LOGI("TEST", "Attesa per raggiungere la posizione: %f ms", (fabs(pos - servo_data.max_pos) / speeds[i]) * 1000.0);
        vTaskDelay(pdMS_TO_TICKS(round((fabs(pos - servo_data.max_pos) / speeds[i]) * 1000.0+1000.0))); // wait until it should have reached the position plus 1s buffer
        pos=servo_data.current_pos.load();
        ESP_LOGI("TEST", "Posizione attuale: %f rad, posizione target: %f rad", pos, servo_data.min_pos);
        move_servo_speed(servo_data.min_pos, speeds[i], servo_data.max_acc, servo_data.max_jerk);
        ESP_LOGI("TEST", "Attesa per raggiungere la posizione: %f ms", (fabs(pos - servo_data.min_pos) / speeds[i]) * 1000.0);
        vTaskDelay(pdMS_TO_TICKS(round((fabs(pos - servo_data.min_pos) / speeds[i]) * 1000.0+1000.0))); // wait until it should have reached the position plus 1s buffer
    }
}

void test_acceleration() {
    ESP_LOGI("TEST", "Test accelerazione crescente");
    
    float accs[] = {0.5f, 2.0f, 5.0f}; // Rad/s^2
    for(int i=0; i<3; i++) {
        ESP_LOGI("TEST", "Test accelerazione crescente: acc=%f", accs[i]);
        move_servo_speed(servo_data.max_pos, servo_data.max_speed, accs[i], servo_data.max_jerk);
        vTaskDelay(pdMS_TO_TICKS(5000)); // Attende che finisca
        move_servo_speed(servo_data.min_pos, servo_data.max_speed, accs[i], servo_data.max_jerk);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}   

void test_jerk() {
    ESP_LOGI("TEST", "Test jerk crescente");
    float jerks[] = {0.5f, 800.0f, 1500.0f}; // Rad/s^3
    for(int i=0; i<3; i++) {
        ESP_LOGI("TEST", "Test jerk crescente: jerk=%f", jerks[i]);
        move_servo_speed(servo_data.max_pos, servo_data.max_speed, servo_data.max_acc, jerks[i]);
        vTaskDelay(pdMS_TO_TICKS(5000)); // Attende che finisca
        move_servo_speed(servo_data.min_pos, servo_data.max_speed, servo_data.max_acc, jerks[i]);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
