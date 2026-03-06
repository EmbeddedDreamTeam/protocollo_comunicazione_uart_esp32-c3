#include "utils_communication.h"

void convert_servo_instructions(float angles_arr[], int angles_arr_len){
    int ids_arr_len = get_ids_array_len();
    int ids_arr[ids_arr_len];
    get_ids_array(ids_arr);

    Payload p;
    p.payload_servo.radians = angles_arr[0];
    Msg* msg = create_msg(0, 0, type_servo, p);
    sort_new_msg(msg);
    //todo add to servo queue

    for(int i=1; i<angles_arr_len; i++){ //!immagino che angles_arr[0] sia x ROOT??
        Payload p;
        p.payload_servo.radians = angles_arr[i];
        int id_i = ids_arr[i];
        Msg* msg = create_msg(0, id_i, type_servo, p);
        send_msg_to_slave(msg);
    }
}