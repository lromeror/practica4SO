#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stdint.h> // Para usar tipos de enteros específicos como uint32_t

// Tamaño del payload (los datos del archivo en cada paquete)
#define PAYLOAD_SIZE 1024 

// #pragma pack(1) asegura que el compilador no añada "relleno" entre los campos.
// Es crucial para que emisor y receptor interpreten la estructura igual.
#pragma pack(1)
typedef struct {
    uint32_t seq_num;    // Número de secuencia del paquete de datos
    uint32_t ack_num;    // Número de secuencia que se está confirmando (ACK)
    uint8_t  is_ack;     // 1 si es un paquete de ACK, 0 si es de datos
    uint8_t  is_fin;     // 1 si es el último paquete (FIN)
    uint16_t data_len;   // Cuántos bytes de datos reales hay en el payload
    char data[PAYLOAD_SIZE]; // Los datos del archivo
} paquete_t;
#pragma pack(0)

#endif // PROTOCOLO_H