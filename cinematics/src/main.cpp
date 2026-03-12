#include "driver/ledc.h"
#include "math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
struct {
    uint32_t duty_res;
    int8_t gpio=3;
    uint32_t sgnl_min_duty=351;
    uint32_t sgnl_max_duty=2640;
    // these values ranges from -5/6*PI rads to +5/6*PI corresponding to -150 degrees to +150 degrees,
    // with a total range of motion of 300 degrees, not ~309 in order to have some margin
    // because if the potentiometer barely exceeds this value, the servo will execute a +360 degrees rotation
    // in order to go back to the setted position
    float min_pos=-5.0/6.0*M_PI;
    float max_pos=5.0/6.0*M_PI;
} servo_data;
esp_err_t set_servo_pos_deg(float degrees);
esp_err_t set_servo_pos(float rad);
void servo_timer_init();

void servo_timer_init(){
    servo_data.duty_res =ledc_find_suitable_duty_resolution(80000000, 50);//auto clock freq of 80MHz and timer freq of 50Hz, the most common servo frequency
    ledc_timer_config_t timer_config ={
        .speed_mode=LEDC_LOW_SPEED_MODE, //low speed is sufficient for controlling a servo
        .duty_resolution = static_cast<ledc_timer_bit_t>(servo_data.duty_res),
        .timer_num=LEDC_TIMER_0, //timer number
        .freq_hz=50,
        .clk_cfg=LEDC_AUTO_CLK,
        .deconfigure=false,
    };
    ledc_timer_config(&timer_config);
    ledc_channel_config_t channel_config={
        .gpio_num=servo_data.gpio, //signal pin of the servo
        .speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_0,
        .intr_type=LEDC_INTR_DISABLE,
        .timer_sel=LEDC_TIMER_0,
        .duty=0,
        .hpoint=0,
    };
    ledc_channel_config(&channel_config);
}
//this type of servo has a frequency of 50Hz and, considering the trimming zone they accept a signal that
// ranges from ~351ms to ~2640 ms  that corresponde to a range of motion of nearly ~309 degrees 
esp_err_t set_servo_pos(float rad){
    if (rad>=servo_data.min_pos && rad<=servo_data.max_pos){
        double mid_point=(servo_data.sgnl_min_duty+(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty)/2);
        double time= mid_point+rad/servo_data.max_pos*mid_point; //calculating the signal time
        uint32_t max_duty = (1 << servo_data.duty_res);
        uint32_t duty= (uint32_t)(time/20000.0*max_duty); //fraction of the period in micro-seconds
        ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0,
            duty
        );
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        return ESP_OK;
    }
    else{
        return ESP_ERR_INVALID_ARG;
    }

}

extern "C" void app_main() {
    servo_timer_init();
    while (true){
        set_servo_pos(0, false, 351);
        vTaskDelay(pdMS_TO_TICKS(10000));
        set_servo_pos(0, false, 2640);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    
    // uint32_t pos = 2660;
    // while(pos>=2630){
    //     set_servo_pos(0, false, pos);
    //     vTaskDelay(pdMS_TO_TICKS(500));
    //     pos--;
    //     // set_servo_pos(0);
    //     // vTaskDelay(pdMS_TO_TICKS(2000));
    //     // set_servo_pos(270);
    //     // vTaskDelay(pdMS_TO_TICKS(2000));
    // }
}