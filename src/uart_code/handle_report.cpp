#include "msg_structs.h"
#include "utils_communication.h"

#include <iostream>
#include <string>
#include <array>
#include <cstdio>

using namespace std;

/* maximum number of nodes */
#define MAX_NODES 10


/* Global array state */
array<PayloadReport, MAX_NODES> ids_array;
int ids_array_size = 0;

array<PayloadReport, MAX_NODES> buffer;
int buffer_size = 0;

/* Returns node information as a string */
const char* get_node_info(const PayloadReport& n){
    static char tmp[64];
    snprintf(tmp, sizeof(tmp), " {%d, %d, %d},",
             n.my_master_id,
             n.my_id,
             n.my_slave_id);
    return tmp;
}

/* Prints current state of ordered_chain and buffer (debug) */
void print_debug(){
    cout << "PRINTING: IDS_ARRAY\n";
    for(int i = 0; i < ids_array_size; ++i){
        cout << get_node_info(ids_array[i]) << ' ';
    }

    cout << "\nPRINTING: BUFFER\n";
    for(int i = 0; i < buffer_size; ++i){
        cout << get_node_info(buffer[i]) << ' ';
    }
    if(buffer_size == 0){
        cout << "EMPTY";
    }
    cout << "\n\n";
}

/* Removes the element at position ix from the array and updates its length */
void remove_ix(int ix, array<PayloadReport, MAX_NODES>* arr, int* arr_len){
    if(ix < 0 || ix >= *arr_len) return;
    for(int i = ix; i < (*arr_len) - 1; ++i){
        (*arr)[i] = (*arr)[i+1];
    }
    (*arr_len)--;
}


bool is_new_leaf(PayloadReport p){
    return ids_array_size>0 && ids_array[ids_array_size-1].my_slave_id == p.my_id && ids_array[ids_array_size-1].my_id == p.my_master_id;
}


bool try_add_element(PayloadReport p){
    for(int ix=0; ix<ids_array_size; ix++){ //prova a vedere se è già dentro la chain se sì ritorna
        PayloadReport* el = &ids_array[ix];
        if(p.my_id == el->my_id){

            if(p.my_master_id == UNKNOWN_ID && p.my_id != ROOT_ID){
                printf("ERRORE: SOLO ROOT PUO' MANDARE UN REPORT CON my_master = -1, %s\n", get_node_info(p));
            }else if(p.my_master_id == el->my_master_id && p.my_slave_id == el->my_slave_id){
                printf("WARNING: HAI MANDATO UN REPORT DUPLICATO DI: %s\n", get_node_info(p));
                remove_ix(ix, &buffer, &buffer_size);
            }else if(p.my_master_id != el->my_master_id || p.my_slave_id != el->my_slave_id){
                ids_array_size = ix; //elimino tutto dopo ix
            }

            break;
        }
    }
    if(is_new_leaf(p) || (p.my_id == 0 && ids_array_size == 0)){ //o è la nuova leaf o è la root
        ids_array[ids_array_size] = p;
        ids_array_size++;
        return true;
    }
    return false;
}


void try_resolve_buffer(){
    for(int i=buffer_size-1; i>=0; i--){
        // cout << buffer[i].my_id << endl;
        bool f = try_add_element(buffer[i]);
        if(f){
            remove_ix(i, &buffer, &buffer_size);
            try_resolve_buffer();
            break;
        }
    }
}


void recive_new_report(PayloadReport p){
    buffer[buffer_size] = p;
    buffer_size++;
    try_resolve_buffer();
    print_debug();
}


void task_handle_report(void* arg){
  if(SELF_ID != ROOT_ID){ //it shouldn't be the case.
    vTaskDelete(nullptr);
  }

  while(1){
  Msg* msg = nullptr;
  xQueueReceive(h_queue_report, &msg, portMAX_DELAY);
    // if(BLINK_LOOP_IF_RECEIVED_REPORT && msg->type == type_report){
    //   resume_loop_blink();
    // }
    printf("REPORT: SLAVE=%d, MY=%d, MASTER=%d\n", msg->payload.payload_report.my_slave_id, msg->payload.payload_report.my_id, msg->payload.payload_report.my_master_id);
    recive_new_report(msg->payload.payload_report);
  }
}


void init_report_handler(){
    PayloadReport root;
    root.my_master_id = -1;
    root.my_id = ROOT_ID;
    root.my_slave_id = -1;
    ids_array[0] = root;
    ids_array_size = 1;
    print_debug();
}


int get_ids_array_len(){
    return ids_array_size;
}

void get_ids_array(int arr[]){
    for(int i=0; i<ids_array_size; i++){
        arr[i] = ids_array[i].my_id;
    }
}
