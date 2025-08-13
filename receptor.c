// receptor_simple.c
// Este receptor maneja un cliente a la vez, de principio a fin.
// Es la forma más simple y robusta de probar el protocolo.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocolo.h"

void manejar_transferencia(int sock, struct sockaddr_in client_addr, paquete_t primer_paquete) {
    socklen_t client_len = sizeof(client_addr);
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    printf("Iniciando transferencia con %s:%d\n", client_ip, ntohs(client_addr.sin_port));

    char nombre_archivo[256];
    sprintf(nombre_archivo, "recibido_de_%s_%d.dat", client_ip, ntohs(client_addr.sin_port));
    
    FILE* fp = fopen(nombre_archivo, "wb");
    if (!fp) {
        perror("Error al abrir archivo de salida");
        return; // Volver al bucle principal
    }

    uint32_t seq_esperada = 0;
    paquete_t paquete_recibido = primer_paquete;
    
    while (1) {
        // Procesar el paquete actual (el primero ya lo tenemos, los siguientes los recibimos)
        if (seq_esperada > 0) {
             ssize_t bytes_recibidos = recvfrom(sock, &paquete_recibido, sizeof(paquete_t), 0,
                                               (struct sockaddr*)&client_addr, &client_len);
            if (bytes_recibidos <= 0) {
                printf("Error en recvfrom durante la transferencia.\n");
                break;
            }
        }
        
        printf("Recibido paquete con seq_num: %u. Esperando: %u\n", 
               paquete_recibido.seq_num, seq_esperada);

        paquete_t paquete_ack;
        paquete_ack.is_ack = 1;

        if (paquete_recibido.seq_num == seq_esperada) {
            fwrite(paquete_recibido.data, 1, paquete_recibido.data_len, fp);
            
            paquete_ack.ack_num = seq_esperada;
            sendto(sock, &paquete_ack, sizeof(paquete_t), 0, 
                   (struct sockaddr*)&client_addr, client_len);
            printf("Enviado ACK para seq_num: %u\n", seq_esperada);
            
            if (paquete_recibido.is_fin) {
                printf("Transferencia completada. Archivo guardado como %s\n", nombre_archivo);
                break;
            }
            seq_esperada++;
        } else {
            printf("Paquete descartado (seq %u != %u). Reenviando ACK anterior.\n",
                   paquete_recibido.seq_num, seq_esperada);
            paquete_ack.ack_num = paquete_recibido.seq_num;
            sendto(sock, &paquete_ack, sizeof(paquete_t), 0, 
                   (struct sockaddr*)&client_addr, client_len);
        }
    }
    
    fclose(fp);
    printf("Listo para la siguiente transferencia.\n");
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(1);
    }

    int puerto = atoi(argv[1]);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(puerto);

    if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error en bind");
        exit(1);
    }

    printf("Receptor iterativo escuchando en el puerto %d...\n", puerto);
    
    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        paquete_t primer_paquete;

        ssize_t bytes_recibidos = recvfrom(sock, &primer_paquete, sizeof(paquete_t), 0,
                                           (struct sockaddr*)&client_addr, &client_len);

        if (bytes_recibidos > 0 && primer_paquete.seq_num == 0 && !primer_paquete.is_ack) {
            // Recibimos el inicio de una nueva transferencia, la manejamos por completo.
            manejar_transferencia(sock, client_addr, primer_paquete);
        }
    }

    close(sock);
    return 0;
}