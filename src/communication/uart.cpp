#include <stdio.h>
#include <string.h>

//definiti da me:
#include "msg_structs.h"
#include "utils_communication.h"

//*GLOBALS
SemaphoreHandle_t master_buffer_mutex;
SemaphoreHandle_t slave_buffer_mutex;


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
  }else{
    printf("ERRORE: [sort_new_msg] type: %i , non esiste\n", msg->type);
  }
}


void task_receive_uart(void *arg){
  uart_port_t selected_uart = (uart_port_t)(int32_t)arg;

  while (1){
    Msg *msg = new Msg();

    bool message_ok = false;
    while (!message_ok){
      message_ok = false;

      // controllo lunghezza buffer RX e segnalo errore se supera la soglia
      size_t buffered_len = 0;
      esp_err_t err = uart_get_buffered_data_len(selected_uart, &buffered_len);
      if (err == ESP_OK) {
        if (buffered_len > U_BUF_SIZE) {
          printf("ERRORE: buffer RX UART %d pieno (%u byte). Potenziale perdita dati.\n",
                 (int)selected_uart, (unsigned)buffered_len);
        }
      } else {
        printf("WARN: uart_get_buffered_data_len() err %d\n", err);
      }

      uart_read_bytes(selected_uart, (uint8_t*)msg, 1, portMAX_DELAY);

      if(msg->header != HEADER_BYTE){
        continue;
      }
      uart_read_bytes(selected_uart, (uint8_t*)msg+1, sizeof(Msg)-1, portMAX_DELAY);
      if(msg->footer == FOOTER_4_BYTES){
        message_ok = true;
      }
    }

    if(BLINK_ON_RECEIVE_MSG && msg->header == HEADER_BYTE && msg->footer == FOOTER_4_BYTES){
      wake_task_blink_led_once();
    }

    printf("\n====================\n");

    const char* role = get_role_name(selected_uart);
    if(msg->target_id == SELF_ID || msg->target_id == -1){ //messaggio x me
      printf("SONO: %d, HO RICEVUTO DA: %s, il messggio E' PER ME (non verra' ritrasmesso):\n", SELF_ID, role);
      print_msg_struct(msg);
      sort_new_msg(msg);

    }else{ //messaggio non x me
      int uart_opposta = (int)!(bool)selected_uart;
      const char* tpr = get_role_name(uart_opposta);

      printf("SONO: %d, HO RICEVUTO DA: %s, il messaggio NON E' PER ME\n", SELF_ID, role);
      bool esiste = !(((int)uart_opposta == (int)UART_NUM_1 && SLAVE_ID == -1) || ((int)uart_opposta == (int)UART_NUM_0 && MASTER_ID == -1));
      printf("VERRA' RITRASMESSO A: %s (PRESENTE= %d)\n", tpr, esiste);
      print_msg_struct(msg);
      if(selected_uart == U_WITH_MASTER){ //ricevo da master, accodo a slave
        send_msg_to_slave(msg);
      }else if(selected_uart == U_WITH_SLAVE){
        send_msg_to_master(msg);
      }

      printf("====================\n");
      fflush(stdout);
    }
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
    

    printf("\n====================\n");
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
    printf("====================\n");

    delete msg; 
  } 
}


bool merda = 0;
//* _______________________________________ ON START INIT UART
void init_uart(uart_port_t uart_num, int rx_pin, int tx_pin) {
    const uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122, 
        .source_clk = UART_SCLK_APB,
    };
    
    uart_driver_install(uart_num, U_BUF_SIZE * 2, 0, 0, nullptr, 0);
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_set_pull_mode((gpio_num_t)rx_pin, GPIO_PULLUP_ONLY); 

    if(SELF_ID == 1){ 
      printf("\nUART: %d, tx_pin: %d, rx_pin: %d\n", uart_num, tx_pin, rx_pin);
    }

    if(!merda){
      merda = 1;
      master_buffer_mutex = xSemaphoreCreateMutex();
      slave_buffer_mutex = xSemaphoreCreateMutex();
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




queue<Msg*> master_pre_init_buffer; 
void send_msg_to_master(Msg* msg){
    xSemaphoreTake(master_buffer_mutex, portMAX_DELAY);

    if(MASTER_ID == UNKNOWN_ID && msg->type != type_handshake){
        master_pre_init_buffer.push(msg);
    } else {
        while(!master_pre_init_buffer.empty()){
            Msg* m = master_pre_init_buffer.front();
            master_pre_init_buffer.pop();
            xQueueSend(h_queue_send_to_master, &m, portMAX_DELAY);
        }
        xQueueSend(h_queue_send_to_master, &msg, portMAX_DELAY);
    }
    xSemaphoreGive(master_buffer_mutex);
}



queue<Msg*> slave_pre_init_buffer; 
void send_msg_to_slave(Msg* msg){
    xSemaphoreTake(slave_buffer_mutex, portMAX_DELAY);

    if(SLAVE_ID == UNKNOWN_ID && msg->type != type_handshake){
        slave_pre_init_buffer.push(msg);
    } else {
        while(!slave_pre_init_buffer.empty()){
            Msg* m = slave_pre_init_buffer.front();
            slave_pre_init_buffer.pop();
            xQueueSend(h_queue_send_to_slave, &m, portMAX_DELAY);
        }
        xQueueSend(h_queue_send_to_slave, &msg, portMAX_DELAY);
    }
    xSemaphoreGive(slave_buffer_mutex);
}
