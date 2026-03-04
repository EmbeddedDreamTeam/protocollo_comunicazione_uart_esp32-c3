#include "utils_communication.h"

#include <iostream>
#include <string>
#include <array>
#include <cstdio>

using namespace std;

/* maximum number of nodes */
#define MAX_NODES 10

struct node {
    int master_id;
    int id;
    int slave_id;
};

/* Global array state */
static array<node, MAX_NODES> ordered_chain;
static int ordered_chain_size = 0;

static array<node, MAX_NODES> buffer;
static int buffer_size = 0;

/* Returns node information as a string */
string get_node_info(const node& n){
    char tmp[64];
    snprintf(tmp, sizeof(tmp), " {%d, %d, %d},", n.master_id, n.id, n.slave_id);
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
void remove_ix(int ix, array<node, MAX_NODES>& arr, int* arr_len){
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
        node leaf = { ordered_chain[ordered_chain_size-1].id,
                      ordered_chain[ordered_chain_size-1].slave_id,
                      -1 };
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
void push_to_ordered_chain(const node n){
    if(ordered_chain_size > 0 && ordered_chain[ordered_chain_size-1].slave_id == -1){
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
bool check_if_slave(const node n){
    return (ordered_chain_size-1 > 0 && ordered_chain[ordered_chain_size-1].slave_id == n.id) ||
            (ordered_chain_size-2 > 0 && ordered_chain[ordered_chain_size-1].slave_id == -1 && ordered_chain[ordered_chain_size-2].slave_id == n.id) ;
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
void recive_new(const node nnode){
    if(nnode.id < 0 || nnode.master_id < 0 || nnode.slave_id < 0){
        printf("ERROR (INVALID FIELD):\nnew->id = %d\nnew->master_id = %d\nnew->slave_id = %d\n\n",
               nnode.id, nnode.master_id, nnode.slave_id);
        return;
    }

    if(nnode.master_id == 0){
        node root = {-1, ROOT_ID, nnode.id};
        push_to_ordered_chain(root);
        push_to_ordered_chain(nnode);
        search_in_buffer();
        try_to_add_leaf();
    }else if(ordered_chain_size > 0 && check_if_slave(nnode)){
        push_to_ordered_chain(nnode);
        print_debug(); // debug
        search_in_buffer();
        try_to_add_leaf();
    } else {
        if(buffer_size < MAX_NODES){
            buffer[buffer_size] = nnode;
            buffer_size++;
            print_debug(); // debug
        }
    }
}

/*
 ROOT has received a hello message from its slave;
 only now can the chain construction begin.
 (obviously root does not send a report to itself,
 so we manually insert ROOT here)
*/
void init_ordered_chain(int root_id, int root_slave){
    node root = { -1, root_id, root_slave };
    ordered_chain[0] = root;
    ordered_chain_size = 1;
    search_in_buffer();
    try_to_add_leaf();
}

