#include "msg_structs.h"
#include "utils_uart_comms.h"
#include "protocol_manager.h"

#include <iostream>
#include <string>
#include <array>
#include <cstdio>
#include <algorithm>

using namespace std;

#define MAX_NODES 10

int ids_array[MAX_NODES]; //ordinati
int ids_array_len = 0;

PayloadReport dict[MAX_NODES]; //disordinati
bool is_dict_ix_empty[MAX_NODES];
//dict[ix] ix = my_id del nodo contenuto



const char* get_node_info(const PayloadReport& n){
    static char tmp[64];
    snprintf(tmp, sizeof(tmp), " {MASTER: %d, SELF: %d, SLAVE: %d}",
             n.my_master_id,
             n.my_id,
             n.my_slave_id);
    return tmp;
}

void print_node_info(PayloadReport& p){
    cout << get_node_info(p) << endl;
}

/* Prints current state of ordered_chain and buffer (debug) */
void print_ids_array(){
    cout << "PRINTING: IDS_ARRAY\n";
    for(int i = 0; i < ids_array_len; i++){
        cout << ids_array[i] << ' ';
    }
    cout << endl;
}


void compute_ids_array(){
    // Rebuild ids_array starting from ROOT_ID by searching for nodes that
    // declare the current node as their master. This makes the algorithm
    // independent from the order in which reports arrive and robust to
    // temporary reordering.
    ids_array[0] = ROOT_ID; // ROOT_ID == 0
    ids_array_len = 1; // reset the array

    // Track which nodes have already been appended to avoid loops
    bool used[MAX_NODES];
    for (int i = 0; i < MAX_NODES; ++i) used[i] = false;
    used[ROOT_ID] = true;

    int curr_node_id = ROOT_ID;
    for (int step = 0; step < MAX_NODES - 1; ++step) {
        int found = -1;
        // find a node j such that dict[j].my_master_id == curr_node_id
        for (int j = 0; j < MAX_NODES; ++j) {
            if (used[j]) continue;
            if (is_dict_ix_empty[j]) continue;
            if (dict[j].my_master_id == curr_node_id) {
                found = j;
                break;
            }
        }

        if (found == -1) break; // no successor found, chain ends here

        // append found to ids_array
        ids_array[ids_array_len++] = found;
        used[found] = true;
        curr_node_id = found;
    }
}


void receive_new_report(PayloadReport p){
    dict[p.my_id] = p;
    is_dict_ix_empty[p.my_id] = false;

    compute_ids_array();
    cout << "RECEIVED NEW REPORT:" << endl;
    print_node_info(p);
    print_ids_array();
    cout << "___RECEIVED NEW REPORT:" << endl;
    ProtocolManager::set_num_servos((uint8_t)get_ids_array_len());
}


int get_ids_array_len(){
    return ids_array_len;
}

void get_ids_array(int* arr, int len){ //copia ids_array in arr
    for(int i=0; i<min(len, ids_array_len); i++){
        arr[i] = ids_array[i];
    }
}


void init_report_handler(int* default_ids, int default_ids_len, bool use_default_ids){
    for(int i = 0; i < MAX_NODES; i++) {
        is_dict_ix_empty[i] = true;
    }

    if(use_default_ids){
        for(int i=0; i<default_ids_len; i++){
            ids_array[i] = default_ids[i];
        }
        ids_array_len=default_ids_len;

    }else{
        //*INIT IDS_ARRAY[] (ROOT IS ALONE)
        ids_array[0] = ROOT_ID; //ROOT_ID == 0
        ids_array_len = 1; 

        //*INIT DICT[] e IS_DICT_EMPTY[] (ROOT IS ALONE)
        PayloadReport pr;
        pr.my_id = ROOT_ID;
        pr.my_master_id = UNKNOWN_ID;
        pr.my_slave_id = UNKNOWN_ID;
        dict[0] = pr;
        is_dict_ix_empty[0] = false;
    }
}

void task_handle_report(void* arg){
  if(SELF_ID != ROOT_ID){ //it shouldn't be the case.
    vTaskDelete(nullptr);
  }

  while(1){
  Msg* msg = nullptr;
  xQueueReceive(h_queue_report, &msg, portMAX_DELAY);
    
    // printf("REPORT: SLAVE=%d, MY=%d, MASTER=%d\n", msg->payload.payload_report.my_slave_id, msg->payload.payload_report.my_id, msg->payload.payload_report.my_master_id);
    receive_new_report(msg->payload.payload_report);
    // free the message allocated by the UART layer
    delete msg;
  }
}

