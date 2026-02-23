#pragma once

#include <stdint.h>      // Per uint8_t, uint32_t
#include <stdbool.h>     // Per il tipo bool
#include "driver/uart.h" // Per uart_read_bytes e le funzioni ESP-IDF


//*PAYLOADS DEFINITIONS
typedef struct{
    char str1[100];
    char str2[50];
}PayloadCommand01;


typedef struct{
    int num1;
    float num2;
}PayloadCommand02;


typedef enum{
    type_hello,
    type_ACK_hello,
}HandshakeType;
typedef struct{
    HandshakeType handshake_type;
} PayloadHandshake;


typedef struct{
    int my_slave_id;
    int my_id;
    int my_master_id;
}PayloadReport;


//con le union alloca sempre i byte x il messaggio + lungo
typedef union{ 
    PayloadCommand01 payload_command_01;
    PayloadCommand02 payload_command_02;
    PayloadHandshake payload_handshake;
    PayloadReport payload_report;
}Payload;


//*MSG DEFINIFION
typedef enum{
    type_command_01,
    type_command_02,
    type_handshake,
    type_report,
}MsgType;
typedef struct __attribute__((packed)){
    uint8_t header;
    int sender_id;
    int target_id;
    MsgType type;

    Payload payload;

    uint32_t footer;
}Msg;
