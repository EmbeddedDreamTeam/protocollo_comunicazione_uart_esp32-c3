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

//* _______________________________________ EXECUTE COMMANDS
void task_execute_command_01(void *arg){
  while(1){
    Msg *msg = nullptr;
    xQueueReceive(h_queue_command_01, &msg, portMAX_DELAY);
    printf("START: execute_command_01\n");
    vTaskDelay(pdMS_TO_TICKS(4000));
    printf("END: execute_command_01\n");
    delete msg; 
  }
}

void task_execute_command_02(void *arg){
  while(1){
    Msg *msg = nullptr;
    xQueueReceive(h_queue_command_02, &msg, portMAX_DELAY);
    printf("START: execute_command_02\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("END: execute_command_02\n");
    delete msg; 
  }
}