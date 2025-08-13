// emisor.c (Versión final con paquete FIN explícito y robusto)

// Macro para activar las funciones POSIX modernas
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>       // Para getaddrinfo
#include <sys/time.h>    // Para struct timeval
#include "protocolo.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <host_destino> <puerto_destino> <nombre_archivo>\n", argv[0]);
        exit(1);
    }

    char* host_destino = argv[1];
    char* puerto_str = argv[2];
    char* nombre_archivo = argv[3];

    FILE* fp = fopen(nombre_archivo, "rb");
    if (!fp) {
        perror("Error al abrir el archivo");
        exit(1);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Error al crear socket");
        exit(1);
    }

    struct addrinfo hints, *servinfo, *p;
    int rv;
    struct sockaddr_in serv_addr;
    socklen_t serv_len;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(host_destino, puerto_str, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    p = servinfo;
    if (p == NULL) {
        fprintf(stderr, "emisor: no se pudo encontrar una dirección para el host\n");
        exit(1);
    }
    
    memcpy(&serv_addr, p->ai_addr, sizeof(struct sockaddr_in));
    serv_len = p->ai_addrlen;
    freeaddrinfo(servinfo);

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    paquete_t paquete_envio;
    paquete_t paquete_ack;
    uint32_t seq_num = 0;
    size_t bytes_leidos;

    // Bucle para enviar los datos del archivo
    while ((bytes_leidos = fread(paquete_envio.data, 1, PAYLOAD_SIZE, fp)) > 0) {
        paquete_envio.seq_num = seq_num;
        paquete_envio.data_len = bytes_leidos;
        paquete_envio.is_ack = 0;
        paquete_envio.is_fin = 0; // La bandera FIN no se usa para paquetes de datos

        int intentos = 0;
        while (1) {
            printf("Enviando paquete de datos seq_num: %u (tamaño: %u)\n", seq_num, (unsigned int)bytes_leidos);
            sendto(sock, &paquete_envio, sizeof(paquete_t), 0,
                   (struct sockaddr*)&serv_addr, serv_len);

            ssize_t ack_bytes = recvfrom(sock, &paquete_ack, sizeof(paquete_t), 0, NULL, NULL);

            if (ack_bytes > 0 && paquete_ack.is_ack && paquete_ack.ack_num == seq_num) {
                printf("ACK para seq_num: %u recibido.\n", seq_num);
                break;
            } else if (ack_bytes < 0) {
                printf("Timeout! Retransmitiendo paquete %u (intento %d)\n", seq_num, ++intentos);
            } else {
                printf("ACK incorrecto recibido (esperaba %u, recibí %u). Ignorando...\n", seq_num, paquete_ack.ack_num);
            }
            
            if (intentos >= 10) {
                fprintf(stderr, "Error: No se pudo contactar al receptor después de %d intentos. Abortando.\n", intentos);
                fclose(fp);
                close(sock);
                exit(1);
            }
        }
        seq_num++;
    }

    // --- SECCIÓN CORREGIDA Y MEJORADA ---
    // El bucle de datos ha terminado. Ahora enviamos un paquete de control FIN.
    printf("Fin del archivo. Enviando paquete FIN con seq_num: %u\n", seq_num);
    paquete_envio.seq_num = seq_num;
    paquete_envio.data_len = 0; // Cero datos
    paquete_envio.is_ack = 0;
    paquete_envio.is_fin = 1;   // Bandera FIN activada

    int intentos_fin = 0;
    while(1) {
        sendto(sock, &paquete_envio, sizeof(paquete_t), 0, (struct sockaddr*)&serv_addr, serv_len);
        
        ssize_t ack_bytes = recvfrom(sock, &paquete_ack, sizeof(paquete_t), 0, NULL, NULL);

        if (ack_bytes > 0 && paquete_ack.is_ack && paquete_ack.ack_num == seq_num) {
            printf("ACK para el paquete FIN recibido. Transferencia completada.\n");
            break; // ¡Éxito!
        }
        
        if (++intentos_fin > 10) {
            fprintf(stderr, "No se recibió ACK para el paquete FIN. Asumiendo éxito, pero podría haber un problema.\n");
            break;
        }
        printf("Timeout en paquete FIN, retransmitiendo...\n");
    }

    fclose(fp);
    close(sock);
    return 0;
}