#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H
#include "driver/ledc.h"
#include "math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <atomic>
#include "freertos/queue.h"

#define SERVO_QUEUE_LEN 5

typedef struct {
    float target_rad;
    float speed;
} ServoTaskParams;

struct {
    uint32_t duty_res;
    int8_t gpio=3;
    uint32_t sgnl_min_duty=351;
    uint32_t sgnl_max_duty=2640;
    // these values ranges from -5/6*PI rads to +5/6*PI corresponding to -150 degrees to +150 degrees,
    // with a total range of motion of 300 degrees, not ~309 in order to have some margin
    // because if the potentiometer barely exceeds this value, the servo will execute a +360 degrees rotation
    // in order to go back to the setted position
    float min_pos=-5.0/6.0*M_PI;
    float max_pos=5.0/6.0*M_PI;
    std::atomic<float> current_pos; //this ensure thread safety
    float max_speed=5.2;
} servo_data;

static QueueHandle_t xServoQueue = NULL; //queue handler
extern TaskHandle_t xTaskHandle; //task handler
esp_err_t set_servo_pos(float rad);
esp_err_t move_servo_speed(float rad, float speed); //speed is in rad/s
float rad_from_deg(int32_t degrees);
void servo_init();

#endif