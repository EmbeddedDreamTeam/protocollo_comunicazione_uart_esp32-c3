#include "driver/ledc.h"
#include "math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <atomic>
#include "freertos/queue.h"

#define SERVO_QUEUE_LEN 5

typedef struct {
    float target_rad;
    float speed;
} ServoTaskParams;

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
    std::atomic<float> current_pos; //this ensure thread safety
} servo_data;

static QueueHandle_t xServoQueue = NULL; //queue handler
TaskHandle_t xTaskHandle = NULL; //task handler
esp_err_t set_servo_pos(float rad);
esp_err_t move_servo_speed(float rad, float speed); //speed is in rad/s
void move_servo_speed_task(void * pvParameters);
float rad_from_deg(int32_t degrees);
void servo_timer_init();
void servo_init();

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
//converting degrees to radians
float rad_from_deg(int32_t degrees){
    return ((float)degrees/360.0 * 2*M_PI);
}

//this type of servo has a frequency of 50Hz and, considering the trimming zone they accept a signal that
// ranges from ~351ms to ~2640 ms  that corresponde to a range of motion of nearly ~309 degrees 
esp_err_t set_servo_pos(float rad){
    if (rad>=servo_data.min_pos && rad<=servo_data.max_pos){
        double mid_point=(servo_data.sgnl_min_duty+(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty)/2.0);
        double time= mid_point+rad/servo_data.max_pos*(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty)/2.0; //calculating the signal time
        uint32_t max_duty = (1 << servo_data.duty_res);
        uint32_t duty= (uint32_t)(time/20000.0*max_duty); //fraction of the period in micro-seconds
        ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0,
            duty
        );
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        servo_data.current_pos=rad;
        return ESP_OK;
    }
    else{
        return ESP_ERR_INVALID_ARG;
    }

}

void move_servo_speed_task(void * pvParameters) {
    ServoTaskParams cmd;
    float current_rad = servo_data.current_pos;
    
    // updating frequency of 50Hz
    TickType_t xLastWakeTime = xTaskGetTickCount(); //The count of ticks since vTaskStartScheduler was called.
    const TickType_t xFrequency = pdMS_TO_TICKS(20);

    while (1) {
        // receiving a message from the queue => 0 busy waiting
        // if there is no message the task will wait to receive one for maximun portMAX_DELAY tiks
        if (xQueueReceive(xServoQueue, &cmd, portMAX_DELAY)) {
            
            //ESP_LOGI("SERVO_TASK", "Nuovo target ricevuto: %.2f rad", cmd.target_rad);

            while (fabs(current_rad - cmd.target_rad) > 0.005f) {
                ServoTaskParams next_cmd;
                if (xQueueReceive(xServoQueue, &next_cmd, 0) == pdTRUE) { //if there is a new message
                    cmd = next_cmd; // updating new target
                    //ESP_LOGI("SERVO_TASK", "Target aggiornato in corsa: %.2f rad", cmd.target_rad);
                }
                // Calcculating steps
                // 0.02 = 20ms period
                float step = cmd.speed * 0.02f; 
                
                if (current_rad < cmd.target_rad) current_rad += step;
                else current_rad -= step;

                set_servo_pos(current_rad);

                vTaskDelayUntil(&xLastWakeTime, xFrequency);
            }
            //ESP_LOGI("SERVO_TASK", "Target raggiunto.");
        }
    }
}


esp_err_t move_servo_speed(float rad, float speed){
    if (xServoQueue == NULL) {
        //ESP_LOGE("SERVO_API", "Errore: Coda non inizializzata!");
        return ESP_ERR_INVALID_STATE;
    }

    ServoTaskParams params;
    params.target_rad = rad;
    params.speed = speed;

    // sending new params to the task, if the queue is full immediatly return
    if (xQueueSend(xServoQueue, &params, 0) != pdPASS) {
        //ESP_LOGW("SERVO_API", "Coda piena, comando scartato o in attesa");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}


void servo_init(){
    servo_timer_init();

    // creating the queue with the designed lenght
    xServoQueue = xQueueCreate(SERVO_QUEUE_LEN, sizeof(ServoTaskParams));

    // creating the persistent task
    xTaskCreate(
        move_servo_speed_task,
        "ServoMotorTask",
        3072, // Stack size
        NULL, //parameters
        1,
        &xTaskHandle
    );

    vTaskDelay(pdMS_TO_TICKS(200));
    move_servo_speed(servo_data.max_pos, 1.0f);
    vTaskDelay(pdMS_TO_TICKS(1000));
    move_servo_speed(servo_data.min_pos, 1.0f);
    vTaskDelay(pdMS_TO_TICKS(1000));
    move_servo_speed(0.0f, 1.0f);
}


extern "C" void app_main() {
    
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