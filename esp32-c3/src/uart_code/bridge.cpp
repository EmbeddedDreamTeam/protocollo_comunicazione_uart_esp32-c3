#include "utils_uart_comms.h"
using namespace std;



//*BRIDGE WIFI

// Helper to convert degrees to radians if your servo logic requires it
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

esp_err_t sanitize_angle_commands(float rad){
    //TODO normalize the angles greather than 360° to correct range of the servo
    //TODO check if this command could cause a collision
    return ESP_OK;
}

void convert_servo_instructions(const std::vector<float>& angles){
    int total_nodes = get_ids_array_len();
    int ids_arr[total_nodes];
    get_ids_array(ids_arr, total_nodes);

    // printf("total_nodes %d\n", total_nodes);
    // for(int i=0; i<3; i++){
    //     printf("%d\n", ids_arr[i]);
    // }

    // The vector 'angles' comes from the computer. 
    // We assume angles[0] is for Root (ID 0), angles[1] for first Slave, etc.
    for (size_t i = 0; i < angles.size(); i++) {
        if (i >= (size_t)total_nodes) break; // Safety check

        // Value-initialize the payload to avoid leaking uninitialized stack bytes
        Payload p{};
        // Convert degree (uint16_t) to Radians (float) as expected by your Payload struct
        p.payload_servo.radians = angles[i] * (M_PI / 180.0f); //! ATTENTO ALLA CONVERSIONE IN RADIANTI, LA VUOI VERAMENTE???
        // Provide safe defaults for motion parameters if the sender doesn't set them
        p.payload_servo.speed = 1.0f;           // default normalized speed (1.0 = full)
        p.payload_servo.acceleration = 100.0f;  // reasonable default
        p.payload_servo.jerk = 1500.0f;         // reasonable default

        int target_id = ids_arr[i];

        if (target_id == SELF_ID) {
            // It's for the Root: send to the local servo queue
            Msg* msg = create_msg(SELF_ID, SELF_ID, type_servo, p);
            sort_new_msg(msg);
        } else {
            // It's for a Slave: route it through UART
            Msg* msg = create_msg(SELF_ID, target_id, type_servo, p);
            send_msg_to_slave(msg);
        }
    }
}


//*BRIDGE ???
void send_servo_movement_ack_to_root(int my_id, float radians){
    // Ensure payload is zero-initialized to avoid garbage bytes
    Payload p{};
    p.payload_servo.radians = radians;
    Msg* msg = create_msg(my_id, ROOT_ID, type_servo_ack, p);
    send_msg_to_master(msg);
}
