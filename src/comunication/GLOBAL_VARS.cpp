#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"

#include "msg_structs.h"
#include "utils.h"



int MASTER_ID = UNKNOWN_ID;
int SELF_ID = UNKNOWN_ID;
int SLAVE_ID = UNKNOWN_ID;

bool BLINK_ON_RECEIVE_MSG = false;
bool BLINK_ON_SEND_MSG = false;

TaskHandle_t h_task_blink_led;
//code x tutti i tipi di comandi diversi
QueueHandle_t h_queue_command_01;
QueueHandle_t h_queue_command_02;
QueueHandle_t h_queue_handshake;

//code x inviare messaggi
QueueHandle_t h_queue_send_to_slave;
QueueHandle_t h_queue_send_to_master;
