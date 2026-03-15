#include "servo_controller.h"
#include <esp_task_wdt.h>


extern "C" void app_main() {
    //disable watchdog timer
    esp_task_wdt_deinit();
    servo_init();
    
    
    
}