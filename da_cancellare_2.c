#include <stdio.h>
#include <stdbool.h>

#define MAX_NODES 10


typedef struct {
    int master_id;
    int id;
    int slave_id;
} node;

// Stato dell'array globale
node ordered_chain[MAX_NODES];
int ordered_chain_size = 0;

node buffer[MAX_NODES];
int buffer_size = 0;

int expected_next_id = 0;


void get_node_info(node* n, char* str_info, int str_info_size){
    snprintf(str_info, str_info_size, " {%d, %d, %d},", n->master_id, n->id, n->slave_id);
}

void print_debug(){
    printf("PRINTING: ORDERED_CHAIN\n");
    for(int i=0; i<ordered_chain_size; i++){
        char str_info[20];
        get_node_info(&ordered_chain[i], str_info, 20);
        printf("%s ", str_info);
    }

    printf("\nPRINTING: BUFFER\n");
    for(int i=0; i<buffer_size; i++){
        char str_info[20];
        get_node_info(&buffer[i], str_info, 20);
        printf("%s ", str_info);
    }
    if(buffer_size == 0){
        printf("EMPTY");
    }
    printf("\n\n");
}

void remove_ix(int ix, node arr[], int* arr_len){
    for(int i = ix; i < (*arr_len)-1; i++){
        arr[i] = arr[i+1];
    }
    (*arr_len)--;
}

/*
aggiunge leaf solo se BUFFER è vuoto, altrimenti l'ordinamento non è finito e non l'aggiunge
*/
int try_to_add_leaf(){
    if(buffer_size == 0){
        node leaf = {ordered_chain[ordered_chain_size-1].id, ordered_chain[ordered_chain_size-1].slave_id, -1};
        ordered_chain[ordered_chain_size] = leaf;
        ordered_chain_size++;
        return 1;
    }
    return 0;
}

/*
gestisce la possibilità di dover rimuovere LEAF perchè si è aggiunto un altro nodo
*/
int push_to_ordered_chain(node* n){
    if(ordered_chain[ordered_chain_size-1].slave_id == -1){
        ordered_chain_size--;
    }
    
    ordered_chain[ordered_chain_size] = *n;
    ordered_chain_size++;
}

/*
controlla se n deve essere aggiunto gestendo la possibilità che l'ultimo nodo della chain sia LEAF, da rimuovere perchè è arrivato un nuovo nodo
*/
bool check_if_slave(node* n){
    if(ordered_chain[ordered_chain_size-1].slave_id == -1){
        ordered_chain_size--;
    }
    return n->id == ordered_chain[ordered_chain_size-1].slave_id;
}

/*
controlla se nel buffer c'è un nodo che è slave di quello in testa alla ordered_chain;
*/
void search_in_buffer(){ 
    for(int ix = 0; ix < buffer_size; ix++){
        if(check_if_slave(&buffer[ix])){
            push_to_ordered_chain(&buffer[ix]);
            remove_ix(ix, buffer, &buffer_size);
            print_debug(); //todo DEBUG
            search_in_buffer(); //ricontrolla da capo e //non continuare la ricerca se hai gia trovato qualcosa in quel ciclo
        }
    }
}

void recive_new(node* new){
    if(new->id < 0 || new->master_id < 0 || new->slave_id <0){
        printf("ERROR (CAMPO NN VALIDO):\nnew->id = %d\nnew->master_id = %d\nnew->slave_id = %d\n\n", new->id, new->master_id, new->slave_id);
        return;
    }

    if(ordered_chain_size > 0 && check_if_slave(new)){
        push_to_ordered_chain(new);
        print_debug(); //todo DEBUG
        search_in_buffer();

        try_to_add_leaf();

    }else{
        buffer[buffer_size] = *new;
        buffer_size++;
        print_debug(); //todo DEBUG
    }
}

/*
ROOT has recived an hello msg from his slave;
only now we can start constructing the chain.
(obviously root doesnt send a report to himself, we insert ROOT manually here)
*/
void init_ordered_chain(int ROOT_ID, int ROOT_SLAVE){
    node root = {-1, ROOT_ID, ROOT_SLAVE};
    ordered_chain[0] = root;
    ordered_chain_size = 1;
    search_in_buffer(); 

    try_to_add_leaf();
}



int main() {

    init_ordered_chain(0,1);
    node n0 = {0, 1, 2};
    node n1 = {1, 2, 3};
    node n2 = {2, 3, 4};

    recive_new(&n2);
    recive_new(&n1);
    recive_new(&n0);

    print_debug();

    return 0;
}