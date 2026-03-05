#include "utils_communication.h"

void send_instructions_to_servos(int* angles_arr, int len){
    int* ids_arr = get_ids_array();
    int ids_arr_len = get_ids_array_len();

    for(int i=0; i<ids_arr_len; i++){
        PayloadServo p;
        p.radians =0;
    }
}