#include "servo_controller.h"
#include "msg_structs.h"
#include "utils_uart_comms.h"
#include <freertos/queue.h>

TaskHandle_t xTaskHandle = NULL;
QueueHandle_t xServoQueue = NULL; //queue handler

const float trim = 0.07f*M_PI;

// single definition of servo_data (shared across translation units)
ServoData servo_data = {
    .duty_res = 0,                // will be set by servo_timer_init()
    .gpio = 5,
    .sgnl_min_duty = 500,
    .sgnl_max_duty = 2500,
    .min_pos = (float)(-30.5/36.0*M_PI) + trim,
    .max_pos = (float)( 30.5/36.0*M_PI) - trim,
    .current_pos = std::atomic<float>(0.0f),
    .current_speed = std::atomic<float>(0.0f),
    .current_acc = std::atomic<float>(0.0f),
    .max_speed = 5.2f,
    .max_acc = 100.0f,
    .max_jerk = 1500.0f, //to have a fluid movement, the servo should be able to reach the target acceleration in 0.1 seconds, so jerk = acc / 0.1s
};

//  S-curve (jerk-limited) motion profile – state machine with 7 phases

//   ACCEL_JUP   jerk > 0 : acc  0  -> +a_max
//   ACCEL_CONST jerk = 0 : acc  = +a_max          (skipped if v_max < a²/j)
//   ACCEL_JDN   jerk < 0 : acc  +a_max -> 0
//   CRUISE      jerk = 0 : acc  = 0, vel = v_max  (skipped for short paths)
//   DECEL_JUP   jerk < 0 : acc  0  -> −a_max
//   DECEL_CONST jerk = 0 : acc  = −a_max          (skipped if vel_peak < a²/j)
//   DECEL_JDN   jerk > 0 : acc  −a_max -> 0


typedef enum {
    PH_ACCEL_JUP = 0,
    PH_ACCEL_CONST,
    PH_ACCEL_JDN,
    PH_CRUISE,
    PH_DECEL_JUP,
    PH_DECEL_CONST,
    PH_DECEL_JDN,
} MotionPhase;


//TODO trim servo in order to have the middle point aligned correctly
float decel_distance(float v, float a_max, float j_max, float v_max);
float decel_distance_sim(float v, float acc_init, float a_max, float j_max, float v_max);
float decel_distance_with_acc(float v, float a, float a_max, float j_max, float v_max);
void servo_timer_init();
void move_servo_speed_task_state_machine(void * pvParameters);
void send_movement_ack();



/// @brief  Numerically estimate the stopping distance with jerk and acceleration limits.
/// using analytic formulas with quantized time steps led to some big errors. This
/// function simulates the deceleration with time steps as close as possible to the control loop frequency.
float decel_distance_sim(float v_init, float acc_init, float a_max, float j_max, float v_max) {
    if (v_init <= 0.0f) return 0.0f;

    const float dt = 0.002f; 
    // starting speed and acceleration
    float v = v_init;
    float a = acc_init;
    // distance covered so far
    float x = 0.0f;
    // capping the number of iterations to avoid infinite loops
    const int max_iters = 20000;
    //interrupting the simulation if the velocity is very low
    for (int i = 0; i < max_iters && v > 1e-6f; ++i) {
        //target acceleration is the maximum allowed acceleration but is negative because we want to decelerate
        const float target_a = -a_max;
        // if the target acceleration is already reached, we don't need to apply jerk
        float j = 0.0f;
        if (a > target_a) j = -j_max;
        // updating the acceleration of the next step and clamping it to the target acceleration
        float a_next = a + j * dt;
        if (a_next < target_a) a_next = target_a;
        // we use the average of the acceleration because the acceleration changes linearly during the time step
        // so the average acceleration is the best estimate of the actual acceleration during the time step
        // because the area under the acceleration curve is the change in velocity,
        // because the area is a triangle with base dt and height a_next-a, the average acceleration is a + (a_next - a) / 2 = (a + a_next) / 2
        float a_avg = 0.5f * (a + a_next);
        float v_next = v + a_avg * dt;
        
        // clamping velocity to max
        if (v_next > v_max) v_next = v_max;
        // if the speed correctly goes to zero
        if (v_next <= 0.0f) {
            // calculating the exact time to stop with protection against division by 0
            float t_stop = (a_avg == 0.0f) ? dt : (-v / a_avg);
            // sanitizing t_stop
            if (t_stop < 0.0f) t_stop = dt;
            // adding the final bit of distance covered until full stop with linear accelerated motion
            x += v * t_stop + 0.5f * a_avg * t_stop * t_stop;
            return x;
        }
        // updating distance with linear accelerated motion
        x += v * dt + 0.5f * a_avg * dt * dt;
        v = v_next;
        a = a_next;
    }
    return x;
}

/// @brief Calculates the distance required to decelerate from a given velocity to zero
float decel_distance(float v, float a_max, float j_max, float v_max) {
    return decel_distance_sim(v, 0.0f, a_max, j_max, v_max);
}

/// @brief Calculates the distance required to decelerate from a given velocity and acceleration to zero
float decel_distance_with_acc(float v, float a, float a_max, float j_max, float v_max) {
    // if we're not currently accelerating (a <= 0) the fallback is the same
    // as decel_distance.
    if (a <= 0.0f) return decel_distance(v, a_max, j_max, v_max);
    return decel_distance_sim(v, a, a_max, j_max, v_max);
}

//TODO forse questa funzione era già stata fatta da cesare?
void send_movement_ack(){
    PayloadServoAck ack;
    ack.sender_id=SELF_ID;
    Msg* msg = create_msg(SELF_ID, MASTER_ID, type_servo_ack, Payload{.payload_servo_ack=ack});
    send_msg_to_master(msg);
    // Do NOT delete msg here: ownership is transferred to the send subsystem.
    // The message pointer is queued and will be deleted by the task that
    // actually sends UART bytes (task_send_uart). Deleting it here causes
    // a use-after-free and intermittent crashes.
}

//TODO add task to send continuous updates about the position

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
        ESP_LOGI("SERVO_API", "Posizione impostata: %.4f rad", rad);
        double mid_point=servo_data.sgnl_min_duty+(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty)/2.0;
        //double time= mid_point+rad/servo_data.max_pos*(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty)/2.0; //calculating the signal time
        double time= mid_point+(rad+trim)/(1.5*M_PI)*(servo_data.sgnl_max_duty-servo_data.sgnl_min_duty); //calculating the signal time
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

 void move_servo_speed_task_state_machine(void *pvParameters) {
    ServoTaskParams cmd;

    TickType_t xFrequency  = pdMS_TO_TICKS(20);

    while (1) {
        if (!xQueueReceive(xServoQueue, &cmd, portMAX_DELAY)) continue;

        bool restart;
        do {
            restart = false;

            // initial state
            float pos    = servo_data.current_pos.load();
            float target = cmd.target_rad;

            // Clamping parameters 
            const float j = cmd.jerk > 0.0f ? cmd.jerk : servo_data.max_jerk;
            const float a = cmd.acc > 0.0f ? cmd.acc : servo_data.max_acc;
            const float v = cmd.speed > 0.0f ? cmd.speed : servo_data.max_speed;

            // Target reached
            if (fabsf(target - pos) < 0.005f) break;

            const float dir = (target > pos) ? 1.0f : -1.0f;
            float vel  = servo_data.current_speed.load();   // modulr of speed, alway ≥ 0
            float acc  = servo_data.current_acc.load();   // acc with sign [rad/s²]: + accel, − decel

            MotionPhase phase = PH_ACCEL_JUP; //not too sure
            bool done = false;

            TickType_t xLastWake = xTaskGetTickCount();
            TickType_t xPrevTick = xLastWake;

            // main loop with state machine for motion profiling
            while (!done) {
                 // if there is a new command in the queue, preempt the current motion and start the new one immediately
                ServoTaskParams next;
                if (xQueueReceive(xServoQueue, &next, 0) == pdTRUE) {
                    servo_data.current_pos.store(pos);
                    cmd = next;
                    restart = true;
                    break;
                    // we have to preserve the previous state
                }

                // calculating actual dt
                const TickType_t now = xTaskGetTickCount();
                float dt = (float)(now - xPrevTick) * (portTICK_PERIOD_MS / 1000.0f);
                xPrevTick = now;

                // remaining distance
                const float rem = fabsf(target - pos);

                // distance necessary to stop
                const float d_stop = (acc > 0.0f)
                    ? decel_distance_with_acc(vel, acc, a, j, cmd.speed)
                    : decel_distance(vel, a, j, cmd.speed);

                // distance necessary to stop + delay to start to decelerate ?
                const float d_trig = d_stop;

                switch (phase) {

                case PH_ACCEL_JUP:
                    //this means that we have just enough space to stop
                    if (rem <= d_trig) { 
                        ESP_LOGI("Servo", "Switching to decel_jup phase (remaining distance: %f, trigger distance: %f, acc: %f, vel: %f)", rem, d_trig, acc, vel);
                        phase = PH_DECEL_JUP;
                        break; 
                    }

                    acc += j * dt; //updating acceleration

                    if (acc >= a) { //capping acceleration to a
                        acc = a;
                        // speed after ajerk-up phase
                        // if the speed after the jerk-up phase is higher than the target speed, we have to go directly to jerk-down (should not happen)
                        // otherwise we can enter the constant acceleration phase
                        float vp = vel + (a * a) / (2.0f * j);
                        if (vp >= v) {
                            ESP_LOGI("Servo", "Switching to ACCEL_JDN phase (vp: %f, v: %f, acc: %f, vel: %f)", vp, v, acc, vel);
                            phase = PH_ACCEL_JDN;
                        } else {
                            ESP_LOGI("Servo", "Switching to ACCEL_CONST phase (vp: %f, v: %f, acc: %f, vel: %f)", vp, v, acc, vel);
                            phase = PH_ACCEL_CONST;
                        }
                    } else {
                        // checking for overshoot
                        float vp = vel + (acc * acc) / (2.0f * j);
                        if (vp >= v) {
                            ESP_LOGI("Servo", "Switching to ACCEL_JDN phase (vp: %f, v: %f, acc: %f, vel: %f)", vp, v, acc, vel);
                            phase = PH_ACCEL_JDN;
                        }
                    }
                    break;

                case PH_ACCEL_CONST:
                    // if we have just enough space to stop, we have to start decelerating
                    if (rem <= d_trig) { 
                        ESP_LOGI("Servo", "Switching to DECEL_JUP phase (remaining distance: %f, trigger distance: %f, acc: %f, vel: %f)", rem, d_trig, acc, vel);
                        phase = PH_DECEL_JUP;
                        break;
                    }
                    // if with the deceleration the speed would be too high, we have to start reducing acceleration
                    if (vel + (a * a) / (2.0f * j) >= v) {
                        ESP_LOGI("Servo", "Switching to ACCEL_JDN phase (vel: %f, v: %f, acc: %f, vel: %f)", vel + (a * a) / (2.0f * j), v, acc, vel);
                        phase = PH_ACCEL_JDN;
                    }
                    break;
                 case PH_ACCEL_JDN:
                    // if we have just enough space to stop, we have to start decelerating
                    if (rem <= d_trig) { 
                        ESP_LOGI("Servo", "Switching to DECEL_JUP phase (remaining distance: %f, trigger distance: %f, acc: %f, vel: %f)", rem, d_trig, acc, vel);
                        phase = PH_DECEL_JUP; 
                        break; 
                    }
                    acc -= j * dt;
                    if (acc <= 0.0f) {
                        acc = 0.0f;
                        vel = v;      // snap at the precise speed
                        ESP_LOGI("Servo", "Switching to CRUISE phase (acc: %f, vel: %f)", acc, vel);
                        phase = PH_CRUISE; //linear motion phase
                    }
                    break;

                case PH_CRUISE:
                    // if we have just enough space to stop, we have to start decelerating
                    if (rem <= d_trig) {
                        ESP_LOGI("Servo", "Switching to DECEL_JUP phase (remaining distance: %f, trigger distance: %f, acc: %f, vel: %f)", rem, d_trig, acc, vel);
                        phase = PH_DECEL_JUP;
                    }
                    break;

                

                case PH_DECEL_JUP:
                    // starting deceleration ramp: acc goes from 0 to -a
                    acc -= j * dt;
                    if (acc < -a) acc = -a;

                    // in this case we have just enough space to stop, so we can skip the constant deceleration phase and go directly to jerk-down
                    if (vel <= (acc * acc) / (2.0f * j)) {
                        ESP_LOGI("Servo", "Switching to DECEL_JDN phase (vel: %f, acc: %f, j: %f)", vel, acc, j);
                        phase = PH_DECEL_JDN;       // triangular profile
                    } else if (acc <= -a) {
                        ESP_LOGI("Servo", "Switching to DECEL_CONST phase (acc: %f, a: %f, vel: %f)", acc, a, vel);
                        phase = PH_DECEL_CONST;     // trapezoidal profile
                    }
                    break;

                case PH_DECEL_CONST:
                    // if we have just enough space to stop, we have to start reducing deceleration
                    if (vel <= (a * a) / (2.0f * j)) {
                        ESP_LOGI("Servo", "Switching to DECEL_JDN phase (vel: %f, a: %f, j: %f, acc: %f)", vel, a, j, acc);
                        phase = PH_DECEL_JDN;
                    }
                    break;

                case PH_DECEL_JDN:
                    // deceleration ramp: acc goes from -a to 0
                    acc += j * dt;
                    if (acc >= 0.0f) {
                        acc = 0.0f;
                        vel = 0.0f;
                        ESP_LOGI("Servo", "Switching to DONE phase (acc: %f, vel: %f)", acc, vel);
                        done = true;    // now we can stop, we have reached the target
                    }
                    break;
                }

                // calculating new speed and position with protections
                vel += acc * dt;
                if (vel < 0.0f) vel = 0.0f;
                if (vel > v)    vel = v;

                pos += dir * vel * dt;

                // correcting overshoot
                if ((dir > 0.0f && pos >= target) || (dir < 0.0f && pos <= target)) {
                    pos = target;
                    vel = 0.0f;
                    acc = 0.0f;
                    done = true;
                }
                // updating servo state
                servo_data.current_speed.store(vel);
                servo_data.current_acc.store(acc);
                servo_data.current_pos.store(pos);
                set_servo_pos(pos);

                if (!done) vTaskDelayUntil(&xLastWake, xFrequency);
            }
        } while (restart);
        set_servo_pos(cmd.target_rad); // ensure we end up in the exact target position (corrections for numerical errors)
        // updating final servo state with speed and acc = 0
        servo_data.current_speed.store(0.0f);
        servo_data.current_acc.store(0.0f);
        send_movement_ack();
    }
}


esp_err_t move_servo_speed(float rad, float speed, float acc, float jerk){
    ESP_LOGI("SERVO_API", "move_servo_speed called with rad=%.2f, speed=%.2f, acc=%.2f, jerk=%.2f", rad, speed, acc, jerk);
    ESP_LOGI("SERVO_API", "Current servo state: pos=%.2f, speed=%.2f, acc=%.2f", servo_data.current_pos.load(), servo_data.current_speed.load(), servo_data.current_acc.load());
    if (xServoQueue == NULL) {
        ESP_LOGE("SERVO_API", "Errore: Coda non inizializzata!");
        return ESP_ERR_INVALID_STATE;
    }

    ServoTaskParams params;
    params.target_rad = rad;
    params.speed = speed>servo_data.max_speed?servo_data.max_speed:speed;
    params.acc = acc>servo_data.max_acc?servo_data.max_acc:acc;
    params.jerk = jerk>servo_data.max_jerk?servo_data.max_jerk:jerk;
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
        move_servo_speed_task_state_machine,
        "ServoMotorTask",
        3072, // Stack size
        NULL, //parameters
        1,
        &xTaskHandle
    );
    //random delay to avoid all the servos to start at the same time and cause a big current absorption peak that could reset the board
    vTaskDelay(pdMS_TO_TICKS(rand()%3000)); 
    
    move_servo_speed(0.0f, 1.0f, servo_data.max_acc, servo_data.max_jerk); //moving the servo to the initial position with max speed, acc and jerk to ensure a fast initialization
    vTaskDelay(pdMS_TO_TICKS(1000));
}


