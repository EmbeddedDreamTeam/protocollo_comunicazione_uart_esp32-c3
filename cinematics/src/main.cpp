#include "driver/ledc.h"
#include "math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
uint32_t duty_res;
uint32_t servo_gpio=3;
void set_servo_pos(int32_t degrees, bool deg, uint32_t ms);
void servo_timer_init();

void servo_timer_init(){
    duty_res =ledc_find_suitable_duty_resolution(80000000, 50);//auto clock freq of 80MHz and timer freq of 50Hz
    ledc_timer_config_t timer_config ={
        .speed_mode=LEDC_LOW_SPEED_MODE, //low speed is sufficient for controlling a servo
        .duty_resolution = static_cast<ledc_timer_bit_t>(duty_res),
        .timer_num=LEDC_TIMER_0, //timer number
        .freq_hz=50,
        .clk_cfg=LEDC_AUTO_CLK,
        .deconfigure=false,
    };
    ledc_timer_config(&timer_config);
    ledc_channel_config_t channel_config={
        .gpio_num=3, //signal pin of the servo
        .speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_0,
        .intr_type=LEDC_INTR_DISABLE,
        .timer_sel=LEDC_TIMER_0,
        .duty=0,
        .hpoint=0,
    };
    ledc_channel_config(&channel_config);
}

void set_servo_pos(int32_t degrees,  bool deg, uint32_t ms){
    ESP_LOGI("DEBUG_SERVO", "gradi: %i", ms);
    if (deg){
        double time= 500.0+2000.0*(degrees/270.0);
        uint32_t max_duty = (1 << duty_res);
        uint32_t duty= (uint32_t)(time/20000.0*max_duty);
        ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0,
            duty
        );
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    else{
        uint32_t max_duty = (1 << duty_res);
        uint32_t duty= (uint32_t)(ms/20000.0*max_duty);
        ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0,
            duty
        );
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    }
}

extern "C" void app_main() {
    servo_timer_init();
    uint32_t pos = 2660;
    while(pos>=2630){
        set_servo_pos(0, false, pos);
        vTaskDelay(pdMS_TO_TICKS(500));
        pos--;
        // set_servo_pos(0);
        // vTaskDelay(pdMS_TO_TICKS(2000));
        // set_servo_pos(270);
        // vTaskDelay(pdMS_TO_TICKS(2000));
    }
}