#include "utils_communication.h"
#include "init_wifi.h"

extern "C" void app_main(void){

  // init_wifi(); // i have not tested

  //per fare prove con uart, da commentare x usare wifi
  init_comunication(); 

  // init_wifi(); // i have not tested

  printf("\n\n<<<<<<<<<<<<<<<<==========>>>>>>>>>>>>>>>\nTHE END OF APP_MAIN - NOW I LOOP\n<<<<<<<<<<<<<<<<==========>>>>>>>>>>>>>>>\n");
  fflush(stdout);
  while(1) { 
    vTaskDelay(pdMS_TO_TICKS(10000)); 
  }
}