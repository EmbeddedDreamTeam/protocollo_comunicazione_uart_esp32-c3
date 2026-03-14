#pragma once

#include <stdint.h>      // Per uint8_t, uint32_t
#include <stdbool.h>     // Per il tipo bool
#include "driver/uart.h" // Per uart_read_bytes e le funzioni ESP-IDF


//*PAYLOADS DEFINITIONS

//todo just for mockup
typedef struct{
    char str1[100];
    char str2[50];
}PayloadCommand01;

typedef struct{
    int num1;
    float num2;
}PayloadCommand02;
//todo _just for mockup

typedef enum{
    type_StM,
    type_StM_ack,
    type_MtS,
    type_MtS_ack,
}HandshakeType;
typedef struct{
    HandshakeType handshake_type;
} PayloadHandshake;


typedef struct{
    int my_slave_id;
    int my_id;
    int my_master_id;
}PayloadReport;


typedef struct{
    float radians; //? is it true?
}PayloadServo;


//con le union alloca sempre i byte x il messaggio + lungo
typedef union{ 
    PayloadCommand01 payload_command_01; //todo just for mockup
    PayloadCommand02 payload_command_02; //todo just for mockup
    PayloadHandshake payload_handshake;
    PayloadReport payload_report;
    PayloadServo payload_servo;
}Payload;


//*MSG DEFINIFION
typedef enum{
    type_command_01, //todo just for mockup
    type_command_02, //todo just for mockup
    type_handshake,
    type_report,
    type_servo,
    type_servo_ack,
}MsgType;
typedef struct __attribute__((packed)){
    uint8_t header;
    int sender_id;
    int target_id;
    MsgType type;

    Payload payload;

    uint32_t footer;
}Msg;
