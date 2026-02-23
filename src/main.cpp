#include "utils_communication.h"

extern "C" void app_main(void){
  init_comunication(); //todo also has test for now
  while(1) { 
    vTaskDelay(pdMS_TO_TICKS(10000)); 
  }
}