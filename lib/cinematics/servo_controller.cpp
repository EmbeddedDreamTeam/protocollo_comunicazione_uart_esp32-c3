#include "servo_controller.h"
#include "msg_structs.h"
#include "utils_uart_comms.h"
#include <freertos/queue.h>

TaskHandle_t xTaskHandle = NULL;
QueueHandle_t xServoQueue = NULL; //queue handler

// single definition of servo_data (shared across translation units)
ServoData servo_data = {
    .duty_res = 0,                // will be set by servo_timer_init()
    .gpio = 5,
    .sgnl_min_duty = 500,
    .sgnl_max_duty = 2500,
    .min_pos = (float)(-30.5/36.0*M_PI),
    .max_pos = (float)( 30.5/36.0*M_PI),
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
float decel_distance(float v, float a_max, float j_max);
float decel_distance_with_acc(float v, float a, float a_max, float j_max);
void servo_timer_init();
void move_servo_speed_task(void * pvParameters);
void send_movement_ack();


/// @brief  Calculates the distance required to decelerate from a given velocity to zero
/// @param v The initial velocity
/// @param a_max The acceleration
/// @param j_max The jerk
/// @return The distance required to decelerate to zero
float decel_distance_alg(float v, float a_max, float j_max) {
    if (v <= 0.0f) return 0.0f;
    // if the velocity is lower than this, we won't reach max acceleration and the profile is triangular
    // t=a/j => v=a*t/2
    float v_full = (a_max * a_max) / (2.0f*j_max);   
    if (v <= v_full) {
        // triangular profile
        // v=a_peak*t' => a_peak=j*t'=> t'=a_peak/j => v=a_peak^2/j => a_peak=sqrt(v*j)
        // distance = 4/3 * v^(3/2) / sqrt(j)
        return (powf(v, 1.5f) / sqrtf(j_max))*4.0f/3.0f;
    }
    // trapezoidal profile
    const float t1 = a_max / j_max; // time to reach max acceleration
    const float t2 = v / a_max - t1; // time at constant acceleration
    const float v1 = v - (a_max * a_max) / (2.0f * j_max);  // speed at the end of the jerk-up phase
    const float v2 = v1 - a_max * t2; // speed at the end of the constant acceleration phase
    const float x1 = v  * t1 - (j_max * t1 * t1 * t1) / 6.0f; // distance covered during the jerk-up phase
    const float x2 = v1 * t2 - 0.5f * a_max * t2 * t2; // distance covered during the constant acceleration phase
    const float x3 = v2 * t1 - 0.5f * a_max * t1 * t1 + (j_max * t1 * t1 * t1) / 6.0f; // distance covered during the jerk-down phase
    return x1 + x2 + x3;
}

/// @brief Calculates the distance required to decelerate from a given velocity and acceleration to zero
/// @param v The initial velocity
/// @param a The initial acceleration
/// @param a_max The maximum acceleration
/// @param j_max The maximum jerk
/// @return The distance required to decelerate to zero
float decel_distance_with_acc_alg(float v, float a, float a_max, float j_max) {
    if (a <= 0.0f) return decel_distance_alg(v, a_max, j_max);
    // time to go from a to 0 with jerk j_max
    const float t_ramp  = a / j_max;
    // distance covered during the ramp down of acceleration, until acceleration reaches 0
    const float d_ramp  = v * t_ramp
                        + (a   * t_ramp * t_ramp) / 2.0f
                        - (j_max * t_ramp * t_ramp * t_ramp) / 6.0f;
    // speed reached when acc = 0  (v + a²/2j)
    const float v_after = v + (a * a) / (2.0f * j_max);
    return d_ramp + decel_distance_alg(v_after, a_max, j_max);
}

/// @brief  Numerically estimate the stopping distance with jerk and acceleration limits.
///
/// The original analytic formulas produced edge cases that caused the state
/// machine to trigger deceleration too early or too late. A short numeric
/// integration is more robust and easier to reason about. The functions below
/// keep the same signatures as before so the rest of the code doesn't need to
/// change.
static float decel_distance_sim(float v_init, float acc_init, float a_max, float j_max) {
    if (v_init <= 0.0f) return 0.0f;

    // Simulation timestep: trade-off between accuracy and CPU cost. 1..2ms is
    // sufficient for our kinematics and keeps the loop short.
    const float dt = 0.002f; // 2 ms
    float v = v_init;
    float a = acc_init; // signed acceleration relative to motion direction
    float x = 0.0f;

    // iterate until velocity reaches zero (or a safety iteration limit is hit)
    const int max_iters = 20000; // safety
    for (int i = 0; i < max_iters && v > 1e-6f; ++i) {
        // To stop in the shortest distance we want to reduce acceleration as
        // fast as allowed (jerk = -j_max) until we reach the maximum braking
        // acceleration (-a_max). After that we hold a = -a_max. This is a
        // greedy but effective policy for braking distance estimation.
        const float target_a = -a_max;
        float j = 0.0f;
        if (a > target_a) j = -j_max; // push acceleration down toward -a_max

        // integrate acceleration (clamp to target)
        float a_next = a + j * dt;
        if (a_next < target_a) a_next = target_a;

        // integrate velocity using average acceleration for better accuracy
        float a_avg = 0.5f * (a + a_next);
        float v_next = v + a_avg * dt;

        if (v_next <= 0.0f) {
            // stop inside this timestep; compute fractional distance
            float t_stop = (a_avg == 0.0f) ? dt : (-v / a_avg);
            if (t_stop < 0.0f) t_stop = dt;
            x += v * t_stop + 0.5f * a_avg * t_stop * t_stop;
            return x;
        }

        // integrate position using average velocity
        x += v * dt + 0.5f * a_avg * dt * dt;

        // advance
        v = v_next;
        a = a_next;
    }

    return x;
}

/// @brief Calculates the distance required to decelerate from a given velocity to zero
float decel_distance(float v, float a_max, float j_max) {
    return decel_distance_sim(v, 0.0f, a_max, j_max);
}

/// @brief Calculates the distance required to decelerate from a given velocity and acceleration to zero
float decel_distance_with_acc(float v, float a, float a_max, float j_max) {
    // if we're not currently accelerating (a <= 0) the fallback is the same
    // as decel_distance.
    if (a <= 0.0f) return decel_distance(v, a_max, j_max);
    return decel_distance_sim(v, a, a_max, j_max);
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

void move_servo_speed_task(void *pvParameters) {
    ServoTaskParams cmd;

    TickType_t xFrequency  = pdMS_TO_TICKS(20);
    // Anticipo di 1 tick nominale nel trigger della decelerazione:
    // a 20 ms e v_max=5.2 rad/s si percorrono ~0.104 rad per tick,
    // quindi il margine è fondamentale per non superare il target.
    float dt_nominal = 20.0f / 1000.0f;

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
            float vel  = 0.0f;   // modulr of speed, alway ≥ 0
            float acc  = 0.0f;   // acc with sign [rad/s²]: + accel, − decel

            MotionPhase phase = PH_ACCEL_JUP; //not too sure
            bool done = false;

            TickType_t xLastWake = xTaskGetTickCount();
            TickType_t xPrevTick = xLastWake;

            // Main loop: simplified jerk-limited controller.
            //
            // Rationale: the previous 7-phase state machine used many analytic
            // heuristics that proved brittle in corner cases (preemption,
            // scheduler jitter, and small remaining distances). Here we use a
            // simple, robust control law: at each timestep either drive the
            // acceleration toward +a (to speed up) or toward -a (to brake),
            // using the maximum allowed jerk. The decision is based on a
            // numeric estimate of the stopping distance (decel_distance*),
            // plus a small safety margin to account for scheduling/tick
            // latency.
            while (!done) {
                // preemption: a new command arrived, restart main loop
                ServoTaskParams next;
                if (xQueueReceive(xServoQueue, &next, 0) == pdTRUE) {
                    servo_data.current_pos.store(pos);
                    cmd = next;
                    restart = true;
                    break;
                }

                // actual dt (s)
                const TickType_t now = xTaskGetTickCount();
                float dt = (float)(now - xPrevTick) * ((float)portTICK_PERIOD_MS / 1000.0f);
                if (dt <= 0.0f) dt = dt_nominal; // fallback to nominal if scheduler jitter
                xPrevTick = now;

                // remaining distance
                const float rem = fabsf(target - pos);

                // stopping distance (numeric)
                const float d_stop = (acc > 0.0f)
                    ? decel_distance_with_acc(vel, acc, a, j)
                    : decel_distance(vel, a, j);
                const float d_stop_alg= (acc > 0.0f)
                    ? decel_distance_with_acc_alg(vel, acc, a, j)
                    : decel_distance_alg(vel, a, j);
                ESP_LOGI("Servo", "Stopping distance (numeric): %f, (algorithmic): %f", d_stop, d_stop_alg);
                // safety margin to account for 1 scheduler tick and a tiny cushion
                const float safety_margin = vel * ((float)portTICK_PERIOD_MS / 1000.0f) + 0.005f;
                const float d_trig = d_stop + safety_margin;

                // choose whether to accelerate or decelerate
                if (rem <= d_trig) {
                    // brake as fast as possible
                    float desired_acc = -a; // full braking in motion direction
                    acc += -j * dt;
                    if (acc < desired_acc) acc = desired_acc;
                } else {
                    // try to reach cruising speed
                    float desired_acc = a;
                    acc += j * dt;
                    if (acc > desired_acc) acc = desired_acc;
                }

                // integrate velocity and clamp
                vel += acc * dt;
                if (vel < 0.0f) vel = 0.0f;
                if (vel > v) vel = v;

                // predict step and guard overshoot
                float next_pos = pos + dir * vel * dt;
                if ((dir > 0.0f && next_pos >= target) || (dir < 0.0f && next_pos <= target)) {
                    pos = target;
                    vel = 0.0f;
                    acc = 0.0f;
                    done = true;
                } else {
                    pos = next_pos;
                }

                // update servo state and command hardware
                servo_data.current_speed.store(vel);
                servo_data.current_acc.store(acc);
                servo_data.current_pos.store(pos);
                set_servo_pos(pos);

                if (!done) vTaskDelayUntil(&xLastWake, xFrequency);
            }

        } while (restart);

        // updating final servo state with speed and acc = 0
        servo_data.current_speed.store(0.0f);
        servo_data.current_acc.store(0.0f);
        send_movement_ack();
    }
}

void move_servo_speed_task_state_machine(void *pvParameters) {
    ServoTaskParams cmd;

    TickType_t xFrequency  = pdMS_TO_TICKS(20);
    // Anticipo di 1 tick nominale nel trigger della decelerazione:
    // a 20 ms e v_max=5.2 rad/s si percorrono ~0.104 rad per tick,
    // quindi il margine è fondamentale per non superare il target.
    float dt_nominal = 20.0f / 1000.0f;

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

            // Main loop: simplified jerk-limited controller.
            //
            // Rationale: the previous 7-phase state machine used many analytic
            // heuristics that proved brittle in corner cases (preemption,
            // scheduler jitter, and small remaining distances). Here we use a
            // simple, robust control law: at each timestep either drive the
            // acceleration toward +a (to speed up) or toward -a (to brake),
            // using the maximum allowed jerk. The decision is based on a
            // numeric estimate of the stopping distance (decel_distance*),
            // plus a small safety margin to account for scheduling/tick
            // latency.
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
                    ? decel_distance_with_acc(vel, acc, a, j)
                    : decel_distance(vel, a, j);

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
                        // speed after a jerk-up phase
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

        // updating final servo state with speed and acc = 0
        servo_data.current_speed.store(0.0f);
        servo_data.current_acc.store(0.0f);
        send_movement_ack();
    }
}

//TODO implement deceleration
// void move_servo_speed_task(void * pvParameters) {
//     ServoTaskParams cmd;
    
//     TickType_t xLastWakeTime;
//     TickType_t xPreviousFrameTick;
//     const TickType_t xFrequency = pdMS_TO_TICKS(20);

//     while (1) {
//         if (xQueueReceive(xServoQueue, &cmd, portMAX_DELAY)) {
//             float current_rad = servo_data.current_pos.load();
//             float start_rad = current_rad;
//             float mid_point;
//             int dir;
//             int target_speed_rads=0;
//             if (cmd.target_rad > current_rad){
//                 dir = 1;
//                 mid_point = current_rad + (cmd.target_rad - current_rad) / 2.0f;
//             }
//             else{
//                 dir = -1;
//                 mid_point = cmd.target_rad + (current_rad - cmd.target_rad) / 2.0f;
//             }
//             // initializing time: count of ticks since vTaskStartScheduler was called
//             //number of ticks of the previus command
//             xLastWakeTime = xTaskGetTickCount();
//             //saving the tick count at the start of the movement frame, to calculate the effective time elapsed in each iteration and use it for the speed control, ensuring a more precise control over the servo movement
//             xPreviousFrameTick = xLastWakeTime;

//             while (fabs(current_rad - cmd.target_rad) > 0.005f) {
//                 // new commands check
//                 ServoTaskParams next_cmd;
//                 if (xQueueReceive(xServoQueue, &next_cmd, 0) == pdTRUE) {
//                     cmd = next_cmd;
//                 }

//                 // calculating real dt (effective time elapsed)
//                 TickType_t xCurrentTick = xTaskGetTickCount();
//                 // calculating real time elapsed in seconds
//                 float dt = (float)(xCurrentTick - xPreviousFrameTick) * portTICK_PERIOD_MS / 1000.0f;
//                 //now the previous tick count is updated to the current one
//                 xPreviousFrameTick = xCurrentTick;

//                 // step based on real time elapsed
//                 //float step = cmd.speed * dt; 
//                 float current_speed = servo_data.current_speed.load();
//                 float current_acc = servo_data.current_acc.load();
//                 float step = current_speed * dt; 
//                 if(dir*current_rad<dir*mid_point){
//                     if (current_speed<cmd.speed) {
//                         // if we are below the target speed, we need to accelerate
//                         // this speed will be used in the next iteration to calculate the step
//                         current_speed += current_acc * dt;
//                         if (current_speed > cmd.speed) current_speed = cmd.speed; // limit speed
//                         //need to implement negative jerk
//                     }
//                     else{
//                         target_speed_rads = current_rad-start_rad;
//                     }
//                     if(current_acc < cmd.acc){
//                         // if we are below the target acceleration, we need to increase jerk
//                         current_acc += cmd.jerk * dt;
//                         if (current_acc > cmd.acc) current_acc = cmd.acc; // limit acceleration
//                     }
//                 }
//                 else if (current_rad> mid_point+target_speed_rads){
//                     //here we need to decelerate
//                     current_acc += cmd.jerk * dt;
//                     if (current_acc > cmd.acc) current_acc = cmd.acc; // limit acceleration
//                     current_speed -= current_acc * dt;
//                     if (current_speed < 0.0f) current_speed = 0.0f; // limit speed
//                 }
//                 else{
//                     servo_data.current_acc.store(0.0f);
//                     current_acc = 0.0f;
//                 }
                
//                     if (current_rad < cmd.target_rad) {
//                         current_rad += step;
//                         if (current_rad > cmd.target_rad) current_rad = cmd.target_rad;
//                     } else {
//                         current_rad -= step;
//                         if (current_rad < cmd.target_rad) current_rad = cmd.target_rad;
//                     }
                
//                 servo_data.current_speed.store(current_speed);
//                 servo_data.current_acc.store(current_acc);

//                 set_servo_pos(current_rad);
//                 // periodically waking this function at a xFrequency rate
//                 vTaskDelayUntil(&xLastWakeTime, xFrequency);
//             }
//             servo_data.current_speed.store(0.0f); // when the target position is reached, the speed is set to 0
//             servo_data.current_acc.store(0.0f); // when the target position is reached, the acceleration is set to 0
//             send_movement_ack();
//         }
//     }
// }


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
    
    move_servo_speed(0.0f, 1.0f, servo_data.max_acc, servo_data.max_jerk); //moving the servo to the initial position with max speed, acc and jerk to ensure a fast initialization
    vTaskDelay(pdMS_TO_TICKS(1000));
}


