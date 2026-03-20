#include <stdio.h>
#include <string.h>

//definiti da me:
#include "msg_structs.h"
#include "utils_uart_comms.h"


//*GLOBALS
SemaphoreHandle_t master_buffer_mutex;
SemaphoreHandle_t slave_buffer_mutex;
SemaphoreHandle_t block_print_mutex;


bool already_done = 0;
//* _______________________________________ ON START INIT UART


void init_uart_mutexes(){
    master_buffer_mutex = xSemaphoreCreateMutex();
    slave_buffer_mutex = xSemaphoreCreateMutex();
    block_print_mutex = xSemaphoreCreateMutex();
    printf("blockprint_mutex: %p\n", (void*)block_print_mutex);
}



void init_uart(uart_port_t uart_num, int rx_pin, int tx_pin) {
    const uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122, 
        .source_clk = UART_SCLK_APB,
        .flags = 0,
    };
    
    uart_driver_install(uart_num, U_BUF_SIZE * 2, 0, 0, nullptr, 0);
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_set_pull_mode((gpio_num_t)rx_pin, GPIO_PULLUP_ONLY); 

    if(SELF_ID == 1){ 
      printf("\nUART: %d, tx_pin: %d, rx_pin: %d\n", uart_num, tx_pin, rx_pin);
    }
}



//* _______________________________________UART RECEIVE
void sort_new_msg(Msg *msg){
    if(msg->type == type_command_01){
        xQueueSend(h_queue_command_01, &msg, portMAX_DELAY);
    }else if (msg->type == type_command_02){
        xQueueSend(h_queue_command_02, &msg, portMAX_DELAY);
    }else if (msg->type == type_handshake){
        xQueueSend(h_queue_handshake, &msg, portMAX_DELAY);
    }else if(msg->type == type_report){
        xQueueSend(h_queue_report, &msg, portMAX_DELAY);
    }else if(msg->type == type_servo){
        xQueueSend(h_queue_servo, &msg, portMAX_DELAY);
    }else if(msg->type == type_servo_ack){
        printf("else if(msg->type == type_servo_ack){ !!! !!! !!!\n");
        // xQueueSend(h_queue_servo, &msg, portMAX_DELAY); //!DOESNT DELETE THE MSG !!!! 
    }else{
        printf("ERRORE: [sort_new_msg] type: %i , non esiste\n", msg->type);
    }
}


void task_receive_uart(void *arg) {
    uart_port_t selected_uart = (uart_port_t)(int32_t)arg;
    int flow_counter = 0; // Per distinguere i vari tentativi di ricezione

    while (1) {
        Msg *msg = new Msg();
        memset(msg, 0, sizeof(Msg));
        bool message_ok = false;
        uint8_t *buf = (uint8_t*)msg;
        const size_t frame_len = sizeof(Msg);

        if (PRINT_RECEIVED_BYTES) printf("\n>>> [START FLOW #%d - UART %d] <<<\nRAW:", flow_counter++, (int)selected_uart);

        while (!message_ok) {
            size_t buffered_len = 0;
            if (uart_get_buffered_data_len(selected_uart, &buffered_len) == ESP_OK) {
                if (buffered_len > U_BUF_SIZE) {
                    printf("\n[ALERT: Buffer overflow: %u byte]\n", (unsigned)buffered_len);
                }
            }

            //In loop finchè non trova l'header
            int r = 0;
            do {
                r = uart_read_bytes(selected_uart, buf, 1, pdMS_TO_TICKS(1000));
                if (r > 0) {
                    if (PRINT_RECEIVED_BYTES) {
                        printf(" %02X", buf[0]); // Stampa il byte ricevuto in esadecimale
                        fflush(stdout);
                    }
                }
            } while (buf[0] != HEADER_BYTE);
            
            if (PRINT_RECEIVED_BYTES) printf("|HEADER OK|"); 

            //Leggi il resto del messaggio
            bool do_start_over = false;
            size_t have = 1;
            while (have < frame_len) {
                int rr = uart_read_bytes(selected_uart, buf + have, frame_len - have, pdMS_TO_TICKS(1000));
                if (rr > 0) {
                    // Stampa i byte del corpo del messaggio
                    if (PRINT_RECEIVED_BYTES) {
                        for (int i = 0; i < rr; i++) {
                            printf(" %02X", buf[have + i]);
                        }
                        fflush(stdout);
                    }
                    have += (size_t)rr;
                } else {
                    if (PRINT_RECEIVED_BYTES) printf(" [THE THE MESSAGE IS INCOMPLETE]");
                    do_start_over = true;
                    break;
                }
            }

            
            if (do_start_over) {
                if (PRINT_RECEIVED_BYTES) printf("\n[I START OVER]\nRAW:");
                continue;
            }

            //Check footer
            if (msg->footer == FOOTER_4_BYTES) {
                if (PRINT_RECEIVED_BYTES) printf(" |OK|");
                message_ok = true;
            } else {
                if (PRINT_RECEIVED_BYTES) printf(" |BAD-FOOTER: %08X|, [I START OVER]", (unsigned int)msg->footer);
            }
        } // fine while(!message_ok)

        if (PRINT_RECEIVED_BYTES) printf("\n[FOOTER OK, MESSAGE OK]\n");


        //sort the message
        if(BLINK_ON_RECEIVE_MSG && msg->header == HEADER_BYTE && msg->footer == FOOTER_4_BYTES){
            wake_task_blink_led_once();
        }

        // printf("blockprint_mutex: %p\n", (void*)block_print_mutex);
        // xSemaphoreTake(block_print_mutex, portMAX_DELAY);
        printf("\n================[RECEIVE UART]================\n");
        const char* role = get_role_name(selected_uart);
        if(msg->target_id == SELF_ID || msg->target_id == -1){ //! in ogni caso se -1 lo prendo io
            printf("ID: %d | RICEVUTO DA: %s | DESTINAZIONE: ME\n", SELF_ID, role);
            print_msg_struct(msg);
            sort_new_msg(msg);
        } else { 
            int uart_opposta = (int)!(bool)selected_uart;
            const char* tpr = get_role_name(uart_opposta);
            bool esiste = !(((int)uart_opposta == (int)UART_NUM_1 && SLAVE_ID == -1) || ((int)uart_opposta == (int)UART_NUM_0 && MASTER_ID == -1));
            
            printf("ID: %d | RICEVUTO DA: %s | FORWARD TO: %s (PRESENTE: %d)\n", SELF_ID, role, tpr, esiste);
            print_msg_struct(msg);
            if(selected_uart == U_WITH_MASTER){
              send_msg_to_slave(msg);
            }else if(selected_uart == U_WITH_SLAVE){
              send_msg_to_master(msg);
            }
        }

        printf("================[__RECEIVE UART]================\n\n");
        fflush(stdout);

        // printf("blockprint_mutex: %p\n", (void*)block_print_mutex);
        // xSemaphoreGive(block_print_mutex);
    }
}


//* _______________________________________UART SEND
void task_send_uart(void *arg){
  uart_port_t selected_uart = (uart_port_t)(int32_t)arg;

  while (1) {
    Msg *msg = nullptr; 
    QueueHandle_t selected_queue = nullptr;
    if(selected_uart == U_WITH_MASTER){
      selected_queue = h_queue_send_to_master;
    }else if(selected_uart == U_WITH_SLAVE){
      selected_queue = h_queue_send_to_slave;
    }

    xQueueReceive(selected_queue, &msg, portMAX_DELAY);

    // uart_write_bytes(selected_uart, (const void*)msg, sizeof(Msg));
    int bytes_sent = uart_write_bytes(selected_uart, (const void*)msg, sizeof(Msg));
    
    // printf("blockprint_mutex: %p\n", (void*)block_print_mutex);
    // xSemaphoreTake(block_print_mutex, portMAX_DELAY);
    printf("\n================[SEND UART]================\n");
    const char* role = get_role_name(selected_uart);
    printf("SONO: %d, HO INVIATO INVIO A: %s, IL SEGUENTE MESSAGGIO:\n", SELF_ID, role);
    print_msg_struct(msg);
    if (bytes_sent != sizeof(Msg)) {
        printf("ERRORE: inviati %d byte su %d\n", bytes_sent, sizeof(Msg));
    } else {
        printf("Tutti i byte inviati correttamente\n");
    }

    if(BLINK_ON_SEND_MSG){
      wake_task_blink_led_once();
    }
    printf("================[__SEND UART]================\n\n");
    fflush(stdout);

    // printf("blockprint_mutex: %p\n", (void*)block_print_mutex);
    // xSemaphoreGive(block_print_mutex);


    delete msg; 
  } 
}



Msg* create_msg(int sender_id, int target_id, MsgType type, Payload payload){
  Msg* msg = new Msg();
  memset(msg, 0, sizeof(Msg));
  msg->header = HEADER_BYTE;
  msg->footer = FOOTER_4_BYTES;

  msg->sender_id = sender_id;
  msg->target_id = target_id;
  msg->type = type;

  msg->payload = payload; //shallow copy
  return msg;
}


// accumula tutti i messaggi che non puo inviare al suo master xche non lo conosce
queue<Msg*> master_pre_init_buffer; 
void send_buffered_messages_to_master(){
    while(!master_pre_init_buffer.empty() && MASTER_ID != UNKNOWN_ID){
        Msg* m = master_pre_init_buffer.front();
        master_pre_init_buffer.pop();
        xQueueSend(h_queue_send_to_master, &m, portMAX_DELAY);
    }
}


void send_msg_to_master(Msg* msg){
    xSemaphoreTake(master_buffer_mutex, portMAX_DELAY);

    if(MASTER_ID == UNKNOWN_ID && msg->type != type_handshake){ //!type_handshake passa in ogni caso
        master_pre_init_buffer.push(msg);
    } else {
        xQueueSend(h_queue_send_to_master, &msg, portMAX_DELAY);
    }
    xSemaphoreGive(master_buffer_mutex);
}



// accumula tutti i messaggi che non puo inviare al suo slave xche non lo conosce
queue<Msg*> slave_pre_init_buffer; 
void send_buffered_messages_to_slave(){
    while(!slave_pre_init_buffer.empty() && SLAVE_ID != UNKNOWN_ID){
            Msg* m = slave_pre_init_buffer.front();
            slave_pre_init_buffer.pop();
            xQueueSend(h_queue_send_to_slave, &m, portMAX_DELAY);
        }
}


void send_msg_to_slave(Msg* msg){
    xSemaphoreTake(slave_buffer_mutex, portMAX_DELAY);
    if(SLAVE_ID == UNKNOWN_ID && msg->type != type_handshake){ //!type_handshake passa in ogni caso
        slave_pre_init_buffer.push(msg);
    } else {
        xQueueSend(h_queue_send_to_slave, &msg, portMAX_DELAY);
    }
    xSemaphoreGive(slave_buffer_mutex);
}
