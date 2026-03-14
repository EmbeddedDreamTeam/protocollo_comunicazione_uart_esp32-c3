#include "utils_uart_comms.h"

//* _______________________________________ EXECUTE COMMANDS

//todo mockup only
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

void task_execute_servo(void *arg){
  while(1){
    Msg *msg = nullptr;
    xQueueReceive(h_queue_servo, &msg, portMAX_DELAY);

    printf("START: execute_servo\n");

    float radians = msg->payload.payload_servo.radians;
    printf("Servo target radians: %f\n", radians);

    vTaskDelay(pdMS_TO_TICKS(3000));

    printf("END: execute_servo\n");

    delete msg;
  }
}

//todo _mockup only