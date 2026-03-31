#ifndef SERVO_TASK_H
#define SERVO_TASK_H

#include "servo_types.h"

// forward declarations
void move_servo_speed_task_state_machine(void * pvParameters);
esp_err_t move_servo_speed(float rad, float speed, float acc, float jerk);
void servo_init();
void send_movement_ack();

#endif
