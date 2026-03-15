#include "servo_controller.h"
#include <freertos/queue.h>

TaskHandle_t xTaskHandle = NULL;

// single definition of servo_data (shared across translation units)
ServoData servo_data = {
    .duty_res = 0,                // will be set by servo_timer_init()
    .gpio = 3,
    .sgnl_min_duty = 500,
    .sgnl_max_duty = 2500,
    .min_pos = (float)(-30.5/36.0*M_PI),
    .max_pos = (float)( 30.5/36.0*M_PI),
    .current_pos = std::atomic<float>(0.0f),
    .max_speed = 5.2f
};

void servo_timer_init();
void move_servo_speed_task(void * pvParameters);
void send_movement_ack(float rad);

void send_movement_ack(float rad){
    //TODO
}

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
/// NOTE this function isn't meant to be used alone, it is used by the move_servo_speed function to set the position of the servo, if you want to set the position directly use move_servo_speed with speed=1.0f
esp_err_t set_servo_pos(float rad){
    if (rad>=servo_data.min_pos && rad<=servo_data.max_pos){
        ESP_LOGI("SERVO_API", "Posizione impostata: %.2f rad", rad);
        double mid_point=servo_data.sgnl_min_duty+(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty)/2.0;
        //double time= mid_point+rad/servo_data.max_pos*(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty)/2.0; //calculating the signal time
        double time= mid_point+rad/(1.5*M_PI)*(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty); //calculating the signal time
        uint32_t max_duty = (1 << servo_data.duty_res);
        uint32_t duty= (uint32_t)(time/20000.0*max_duty); //fraction of the period in micro-seconds
        ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            LEDC_CHANNEL_0,
            duty
        );
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        // Update the logical current position (this is the commanded position,
        // not a physical feedback sample). Use atomic store for clarity.
        servo_data.current_pos.store(rad);
        return ESP_OK;
    }
    else{
        return ESP_ERR_INVALID_ARG;
    }

}

void move_servo_speed_task(void * pvParameters) {
    ServoTaskParams cmd;
    float current_rad = servo_data.current_pos.load();
    
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(20);

    while (1) {
        if (xQueueReceive(xServoQueue, &cmd, portMAX_DELAY)) {
            
            // initializing time: count of ticks since vTaskStartScheduler was called
            xLastWakeTime = xTaskGetTickCount();
            TickType_t xPreviousFrameTick = xLastWakeTime;

            while (fabs(current_rad - cmd.target_rad) > 0.005f) {
                // new commands check
                ServoTaskParams next_cmd;
                if (xQueueReceive(xServoQueue, &next_cmd, 0) == pdTRUE) {
                    cmd = next_cmd;
                }

                // calculating real dt (effective time elapsed)
                TickType_t xCurrentTick = xTaskGetTickCount();
                // calculating real time elapsed in seconds
                float dt = (float)(xCurrentTick - xPreviousFrameTick) * portTICK_PERIOD_MS / 1000.0f;
                xPreviousFrameTick = xCurrentTick;

                // step based on real time elapsed
                float step = cmd.speed * dt; 
                
                if (current_rad < cmd.target_rad) {
                    current_rad += step;
                    if (current_rad > cmd.target_rad) current_rad = cmd.target_rad;
                } else {
                    current_rad -= step;
                    if (current_rad < cmd.target_rad) current_rad = cmd.target_rad;
                }

                set_servo_pos(current_rad);

                // periodically waking this function a xFrequency rate
                vTaskDelayUntil(&xLastWakeTime, xFrequency);
            }
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
    params.speed = speed>servo_data.max_speed?servo_data.max_speed:speed;
    // if the queue is full, we drop the oldest command to make room for the new one, ensuring that the servo always moves towards the most recent target position
    //ensuring reactivity
    if (xQueueSend(xServoQueue, &params, 0) != pdPASS) {
        ServoTaskParams dropped;
        xQueueReceive(xServoQueue, &dropped, 0);
        // trying to send the new command again after dropping the oldest one
        if (xQueueSend(xServoQueue, &params, 0) != pdPASS) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    return ESP_OK;
}


void servo_init(){
    servo_timer_init();

    // ensure logical current position has a known value before task start
    servo_data.current_pos.store(0.1f);

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
    
    move_servo_speed(0.0f, 1.0f);
    vTaskDelay(pdMS_TO_TICKS(1000));
}


