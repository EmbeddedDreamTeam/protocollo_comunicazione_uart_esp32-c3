#include "utils_uart_comms.h"
#include "init_wifi.h"

extern "C" void app_main(void){

  if(SELF_ID == ROOT_ID){
    init_wifi();
  }

  init_uart_comms(); 

  printf("\n\n<<<<<<<<<<<<<<<<==========>>>>>>>>>>>>>>>\nTHE END OF APP_MAIN - NOW I LOOP\n<<<<<<<<<<<<<<<<==========>>>>>>>>>>>>>>>\n");
  fflush(stdout);
  while(1) { 
    vTaskDelay(pdMS_TO_TICKS(10000)); 
  }
}