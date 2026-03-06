#include "utils_communication.h"

#include <iostream>
#include <string>
#include <array>
#include <cstdio>

using namespace std;

/* maximum number of nodes */
#define MAX_NODES 10


/* Global array state */
static array<PayloadReport, MAX_NODES> ordered_chain;
static int ordered_chain_size = 0;

static array<PayloadReport, MAX_NODES> buffer;
static int buffer_size = 0;

/* Returns node information as a string */
string get_node_info(const PayloadReport& n){
    char tmp[64];
    snprintf(tmp, sizeof(tmp), " {%d, %d, %d},",
             n.my_master_id,
             n.my_id,
             n.my_slave_id);
    return string(tmp);
}

/* Prints current state of ordered_chain and buffer (debug) */
void print_debug(){
    cout << "PRINTING: ORDERED_CHAIN\n";
    for(int i = 0; i < ordered_chain_size; ++i){
        cout << get_node_info(ordered_chain[i]) << ' ';
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
void remove_ix(int ix, array<PayloadReport, MAX_NODES>& arr, int* arr_len){
    if(ix < 0 || ix >= *arr_len) return;
    for(int i = ix; i < (*arr_len) - 1; ++i){
        arr[i] = arr[i+1];
    }
    (*arr_len)--;
}

/*
 Adds the leaf only if the BUFFER is empty,
 otherwise the ordering is not finished and it is not added
*/
int try_to_add_leaf(){
    if(buffer_size == 0 && ordered_chain_size > 0){
        PayloadReport leaf;
        leaf.my_master_id = ordered_chain[ordered_chain_size-1].my_id;
        leaf.my_id = ordered_chain[ordered_chain_size-1].my_slave_id;
        leaf.my_slave_id  = -1;
        if(ordered_chain_size < MAX_NODES){
            ordered_chain[ordered_chain_size] = leaf;
            ordered_chain_size++;
            return 1;
        }
    }
    return 0;
}

/*
 Handles the possibility of removing the LEAF
 because a new node has been added
*/
void push_to_ordered_chain(const PayloadReport n){
    if(ordered_chain_size > 0 && ordered_chain[ordered_chain_size-1].my_slave_id == -1){
        ordered_chain_size--;
    }

    if(ordered_chain_size < MAX_NODES){
        ordered_chain[ordered_chain_size] = n;
        ordered_chain_size++;
    }
}

/*
 Checks whether n should be added, handling the possibility
 that the last node in the chain is a LEAF
*/
bool check_if_slave(const PayloadReport n){
    // FIX: Changed boundary checks from (size-1 > 0) to (size > 0) 
    // and from (size-2 > 0) to (size > 1)
    return (ordered_chain_size > 0 && ordered_chain[ordered_chain_size-1].my_slave_id == n.my_id) ||
           (ordered_chain_size > 1 && ordered_chain[ordered_chain_size-1].my_slave_id == -1 &&
            ordered_chain[ordered_chain_size-2].my_slave_id == n.my_id);
}

/*
 Checks whether there is a node in the buffer
 that is the slave of the last node in ordered_chain
*/
void search_in_buffer(){
    bool found = true;
    while(found){
        found = false;
        for(int ix = 0; ix < buffer_size; ++ix){
            if(check_if_slave(buffer[ix])){
                push_to_ordered_chain(buffer[ix]);
                remove_ix(ix, buffer, &buffer_size);
                print_debug(); // debug
                found = true;
                break; // restart search from beginning
            }
        }
    }
}

/*
 Receives a new node; validates its fields and,
 if possible, adds it to ordered_chain or places it in the buffer
*/
void recive_new_report(PayloadReport rp){
    if(rp.my_id < 0 || rp.my_master_id < 0 || rp.my_slave_id < 0){
        printf("ERROR (INVALID FIELD):\nnew->id = %d\nnew->master_id = %d\nnew->slave_id = %d\n\n",
               rp.my_id, rp.my_master_id, rp.my_slave_id);
        return;
    }

    if(ordered_chain_size > 0 && check_if_slave(rp)){
        push_to_ordered_chain(rp);
        search_in_buffer();
        try_to_add_leaf();
    } else {
        if(buffer_size < MAX_NODES){
            buffer[buffer_size] = rp;
            buffer_size++;
        }
    }

    print_debug(); //in any case
}


void init_report_handler(int slave_of_root_id){
    PayloadReport root;
    root.my_master_id = -1;
    root.my_id = ROOT_ID;
    root.my_slave_id  = slave_of_root_id;
    push_to_ordered_chain(root);
    try_to_add_leaf();
    print_debug();
}


int get_ids_array_len(){
    return ordered_chain_size;
}

void get_ids_array(int arr[]){
    for(int i=0; i<ordered_chain_size; i++){
        arr[i] = ordered_chain[i].my_id;
    }
}