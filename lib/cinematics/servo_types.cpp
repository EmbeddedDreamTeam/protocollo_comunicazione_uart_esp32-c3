#include "servo_types.h"
#include <math.h>


TaskHandle_t xTaskHandle = NULL;
QueueHandle_t xServoQueue = NULL; //queue handler

// single definition of servo_data (shared across translation units)
ServoData servo_data = {
    .duty_res = 0,                // will be set by servo_timer_init()
    .gpio = 5,
    .sgnl_min_duty = 500,
    .sgnl_max_duty = 2500,
    .min_pos = (float)(-30.5/36.0*M_PI) + trim,
    .max_pos = (float)( 30.5/36.0*M_PI) - trim,
    .current_pos = std::atomic<float>(0.0f),
    .current_speed = std::atomic<float>(0.0f),
    .current_acc = std::atomic<float>(0.0f),
    .max_speed = 5.2f,
    .max_acc = 100.0f,
    .max_jerk = 1500.0f, //to have a fluid movement, the servo should be able to reach the target acceleration in 0.1 seconds, so jerk = acc / 0.1s
};
