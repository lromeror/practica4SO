#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>      
#include <sys/time.h>    
#include "protocolo.h"

int main(int argc, char *argv[]) {
    // validamos que se pasen los argumentos correctos
    if (argc != 4) {
        fprintf(stderr, "uso: %s <host_destino> <puerto_destino> <nombre_archivo>\n", argv[0]);
        exit(1);
    }

    char* host_destino = argv[1];
    char* puerto_str = argv[2];
    char* nombre_archivo = argv[3];

    // abrimos el archivo en modo lectura binaria 
    FILE* fp = fopen(nombre_archivo, "rb");
    if (!fp) {
        perror("error al abrir el archivo");
        exit(1);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("error al crear socket");
        exit(1);
    }

    struct addrinfo hints, *servinfo, *p;
    int rv;
    // usamos sockaddr_storage porque es lo suficientemente grande para ipv4 e ipv6
    struct sockaddr_storage serv_addr_storage;
    socklen_t serv_len;

    // preparamos los 'hints' para getaddrinfo
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // acepta ipv4 o ipv6
    hints.ai_socktype = SOCK_DGRAM;

    // resolvemos la direccion del host a una direccion de red
    if ((rv = getaddrinfo(host_destino, puerto_str, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    // tomamos la primera direccion valida de la lista
    p = servinfo;
    if (p == NULL) {
        fprintf(stderr, "emisor: no se pudo encontrar una direccion para el host\n");
        exit(1);
    }
    
    memcpy(&serv_addr_storage, p->ai_addr, p->ai_addrlen);
    serv_len = p->ai_addrlen;
    freeaddrinfo(servinfo); // liberamos la memoria de la lista

    // configurar el timeout de recepcion en el socket
    struct timeval tv;
    tv.tv_sec = 2; // timeout de 2 segundos
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    paquete_t paquete_envio;
    paquete_t paquete_ack;
    uint32_t seq_num = 0;
    size_t bytes_leidos;

    // bucle principal aqui lee el archivo en trozos y los envia
    while ((bytes_leidos = fread(paquete_envio.data, 1, PAYLOAD_SIZE, fp)) > 0) {
        paquete_envio.seq_num = seq_num;
        paquete_envio.data_len = bytes_leidos;
        paquete_envio.is_ack = 0;
        paquete_envio.is_fin = 0;

        int intentos = 0;
        // bucle de retransmision no avanza hasta recibir el ack correcto
        while (1) {
            printf("enviando paquete de datos seq_num: %u (tamano: %u)\n", seq_num, (unsigned int)bytes_leidos);
            sendto(sock, &paquete_envio, sizeof(paquete_t), 0,
                   (struct sockaddr*)&serv_addr_storage, serv_len);

            // preparamos para recibir la respuesta y la direccion del remitente
            struct sockaddr_storage from_addr;
            socklen_t from_len = sizeof(from_addr);
            ssize_t ack_bytes = recvfrom(sock, &paquete_ack, sizeof(paquete_t), 0, 
                                         (struct sockaddr*)&from_addr, &from_len);

            // si recibimos un ack valido continuamos
            if (ack_bytes > 0 && paquete_ack.is_ack && paquete_ack.ack_num == seq_num) {
                printf("ack para seq_num: %u recibido.\n", seq_num);
                
             
                // si es el primer ack actualizamos la direccion de destino
                // el hilo del receptor nos habra respondido desde un puerto nuevo
                if (seq_num == 0) {
                    printf("conectado al hilo del receptor. actualizando puerto de destino.\n");
                    memcpy(&serv_addr_storage, &from_addr, from_len);
                    serv_len = from_len;
                }
                break; // salimos del bucle de retransmision
            } else if (ack_bytes < 0) {
                printf("Timeout! Retransmitiendo paquete %u (intento %d)\n", seq_num, ++intentos);
            } else {
                printf("ACK incorrecto recibido (esperaba %u, recibí %u). Ignorando...\n", seq_num, paquete_ack.ack_num);
            }
            
            // medida de seguridad para evitar un bucle infinito
            if (intentos >= 10) {
                fprintf(stderr, "error: no se pudo contactar al receptor. abortando.\n");
                exit(1);
            }
        }
        seq_num++;
    }

    // hemos enviado todo el archivo ahora enviamos el paquete de finalizacion
    printf("fin del archivo. enviando paquete fin con seq_num: %u\n", seq_num);
    paquete_envio.seq_num = seq_num;
    paquete_envio.data_len = 0;
    paquete_envio.is_ack = 0;
    paquete_envio.is_fin = 1;

    int intentos_fin = 0;
    // usamos la misma logica de retransmision para el paquete fin
    while(1) {
        sendto(sock, &paquete_envio, sizeof(paquete_t), 0, (struct sockaddr*)&serv_addr_storage, serv_len);
        
        ssize_t ack_bytes = recvfrom(sock, &paquete_ack, sizeof(paquete_t), 0, NULL, NULL);

        if (ack_bytes > 0 && paquete_ack.is_ack && paquete_ack.ack_num == seq_num) {
            printf("ACK para el paquete FIN recibido. Transferencia completada.\n");
            break;
        }
        
        if (++intentos_fin > 10) {
            fprintf(stderr, "No se recibió ACK para el paquete FIN. Asumiendo éxito.\n");
            break;
        }
        printf("timeout en paquete fin, retransmitiendo...\n");
    }

    fclose(fp);
    close(sock);
    return 0;
}