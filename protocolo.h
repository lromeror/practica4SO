#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stdint.h> 

// definimos el tamano maximo de datos por paquete
#define PAYLOAD_SIZE 1024 
// definimos la estructura del paquete de datos
// esto es crucial para que la comunicacion en red sea consistente.
#pragma pack(1)
typedef struct {
    uint32_t seq_num;    // numero de secuencia del paquete de datos
    uint32_t ack_num;    // numero de secuencia que se esta confirmando (ack)
    uint8_t  is_ack;     // si es un paquete de ack 0 si es de datos
    uint8_t  is_fin;     // si es el ultimo paquete de la transferencia
    uint16_t data_len;   // cuantos bytes de datos reales hay en el payload
    char data[PAYLOAD_SIZE]; // los datos del archivo
} paquete_t;
#pragma pack(0) 

#endif 