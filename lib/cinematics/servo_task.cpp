#include "servo_controller.h"
#include "msg_structs.h"
#include "utils_uart_comms.h"
#include <freertos/queue.h>
#include "esp_log.h"
#include <cmath>



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


 void move_servo_speed_task_state_machine(void *pvParameters) {
    ServoTaskParams cmd;

    TickType_t xFrequency  = pdMS_TO_TICKS(20);

    while (1) {
        // by passing the portMAX_DELAY to xQueueReceive, we ensure that the task will be blocked until
        // there is a new command in the queue
        // (the third parameter is xTicksToWait that specify the maximum amount of time the task should
        // be blocked waiting for a command)
        if (!xQueueReceive(xServoQueue, &cmd, portMAX_DELAY)) continue;
        servo_data.moving.store(true);
        bool backlash_compensation=false;

        bool restart;
        do {
            restart = false;
            backlash_compensation=false;
            // initial state
            float pos    = servo_data.current_pos.load();
            float target = cmd.target_rad;

            if (target < pos){
                target= max(target - backlash, servo_data.min_pos);
                backlash_compensation=true;
            }
            ESP_LOGI("Servo", "New command: target=%.4f, speed=%.3f, acc=%.3f, jerk=%.3f, backlash_comp=%s", target, cmd.speed, cmd.acc, cmd.jerk, backlash_compensation ? "YES" : "NO");

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
                    // seting the flag to restart the FSM
                    restart = true;
                    // breaking the loop to restart the FSM with the new command
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
                //making sure that the servo accepts the new position command,
                // if the new position signal is different from the previous one less than the deadzone
                // the servo will drop that command and keep the previous one
                if (fabsf(pos - target) > servo_deadzone){
                    set_servo_pos(pos);
                }
                else{
                    set_servo_pos(target);
                }
                if (!done) vTaskDelayUntil(&xLastWake, xFrequency);
            }
        } while (restart);
        set_servo_pos(cmd.target_rad); // ensure we end up in the exact target position (corrections for numerical errors)
        // updating final servo state with speed and acc = 0
        servo_data.current_speed.store(0.0f);
        servo_data.current_acc.store(0.0f);
        servo_data.moving.store(false);
        if (backlash_compensation){
            // if we have done a backlash compensation, we need to move the servo back to the original target position to compensate for the backlash
            vTaskDelay(pdMS_TO_TICKS(500)); 
            ServoTaskParams backlash_cmd;
            backlash_cmd.target_rad = cmd.target_rad;
            backlash_cmd.speed = 0.5f;
            backlash_cmd.acc = 10.0f;
            backlash_cmd.jerk = 30.0f;
            xQueueSend(xServoQueue, &backlash_cmd, 0); // we can send the command directly to the queue, the FSM will take care of executing it immediately
        }
        else{
            send_movement_ack();
        }
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
    // sanitizing input parameters to ensure they are within the servo limits
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
    servo_data.current_pos.store(-0.1f);

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
    ESP_LOGI("SERVO_INIT", "Servo deadzone %f", servo_deadzone);
    //random delay to avoid all the servos to start at the same time and cause a big current absorption peak that could reset the board
    vTaskDelay(pdMS_TO_TICKS(rand()%3000)); 
    
    move_servo_speed(0.0f, 1.0f, servo_data.max_acc, servo_data.max_jerk); //moving the servo to the initial position with max speed, acc and jerk to ensure a fast initialization
    vTaskDelay(pdMS_TO_TICKS(1000));
}
