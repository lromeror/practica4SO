#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocolo.h"

// estructura para pasar los argumentos iniciales al hilo
typedef struct {
    struct sockaddr_in client_addr;
    paquete_t primer_paquete;
} thread_args_t;

// mutex para proteger printf y evitar que los logs de los hilos se mezclen
pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

// esta es la funcion que ejecuta cada hilo trabajador
void *manejar_transferencia(void *args) {
    thread_args_t *thread_args = (thread_args_t *)args;
    struct sockaddr_in client_addr = thread_args->client_addr;
    paquete_t paquete_recibido = thread_args->primer_paquete;
    free(args); // liberamos la memoria de los argumentos lo antes posible

    //  cada hilo crea su propio socket para evitar conflictos
    int new_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (new_sock < 0) {
        perror("error al crear socket en el hilo");
        return NULL;
    }

    struct sockaddr_in thread_serv_addr;
    memset(&thread_serv_addr, 0, sizeof(thread_serv_addr));
    thread_serv_addr.sin_family = AF_INET;
    thread_serv_addr.sin_addr.s_addr = INADDR_ANY;
    thread_serv_addr.sin_port = htons(0); // puerto 0 le pide al so uno disponible

    if (bind(new_sock, (struct sockaddr*)&thread_serv_addr, sizeof(thread_serv_addr)) < 0) {
        perror("error en bind del hilo");
        close(new_sock);
        return NULL;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

    // seccion critica para imprimir logs de forma ordenada
    pthread_mutex_lock(&stdout_mutex);
    printf("hilo %ld: iniciando transferencia con %s:%d\n", pthread_self(), client_ip, ntohs(client_addr.sin_port));
    pthread_mutex_unlock(&stdout_mutex);

    // creamos un nombre de archivo unico para cada transferencia
    char nombre_archivo[256];
    sprintf(nombre_archivo, "recibido_de_%s_%d.dat", client_ip, ntohs(client_addr.sin_port));
    FILE* fp = fopen(nombre_archivo, "wb");
    if (!fp) {
        perror("error al abrir archivo de salida");
        close(new_sock);
        return NULL;
    }

    uint32_t seq_esperada = 0;
    socklen_t client_len = sizeof(client_addr);
    
    // procesamos el primer paquete que nos paso el hilo principal
    fwrite(paquete_recibido.data, 1, paquete_recibido.data_len, fp);
    
    paquete_t paquete_ack;
    paquete_ack.is_ack = 1;
    paquete_ack.ack_num = seq_esperada;
    
    // Enviamos el primer ack desde el nuevo socket
    // esto le informa al emisor del nuevo puerto de comunicacion
    sendto(new_sock, &paquete_ack, sizeof(paquete_t), 0, (struct sockaddr*)&client_addr, client_len);
    seq_esperada++;

    // bucle principal de la transferencia, escuchando en el socket privado
    while (1) {
        ssize_t bytes_recibidos = recvfrom(new_sock, &paquete_recibido, sizeof(paquete_t), 0, NULL, NULL);
        if (bytes_recibidos <= 0) {
            // error o el cliente cerro la conexion
            break;
        }

        // si el paquete es el que esperabamos lo procesamos
        if (paquete_recibido.seq_num == seq_esperada) {
            fwrite(paquete_recibido.data, 1, paquete_recibido.data_len, fp);
            paquete_ack.ack_num = seq_esperada;
            sendto(new_sock, &paquete_ack, sizeof(paquete_t), 0, (struct sockaddr*)&client_addr, client_len);

            if (paquete_recibido.is_fin) {
                pthread_mutex_lock(&stdout_mutex);
                printf("hilo %ld: transferencia completada. archivo guardado como %s\n", pthread_self(), nombre_archivo);
                pthread_mutex_unlock(&stdout_mutex);
                break;
            }
            seq_esperada++;
        } else {
            paquete_ack.ack_num = seq_esperada - 1; 
            sendto(new_sock, &paquete_ack, sizeof(paquete_t), 0, (struct sockaddr*)&client_addr, client_len);
        }
    }
    
    fclose(fp);
    close(new_sock); // cerramos el socket del hilo
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "uso: %s <puerto>\n", argv[0]);
        exit(1);
    }

    int puerto = atoi(argv[1]);
    int sock = socket(AF_INET, SOCK_DGRAM, 0); // socket principal de escucha

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; // escuchar en todas las interfaces
    serv_addr.sin_port = htons(puerto);     // convertir puerto a formato de red

    // asociamos el socket al puerto
    if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("error en bind");
        exit(1);
    }
    printf("receptor concurrente escuchando en el puerto %d...\n", puerto);
    
    // bucle infinito para aceptar nuevas conexiones
    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        paquete_t primer_paquete;

        // el hilo principal solo escucha por el primer paquete 
        ssize_t bytes_recibidos = recvfrom(sock, &primer_paquete, sizeof(paquete_t), 0,
                                           (struct sockaddr*)&client_addr, &client_len);

        // si es un nuevo cliente, creamos un hilo para atenderlo
        if (bytes_recibidos > 0 && primer_paquete.seq_num == 0 && !primer_paquete.is_ack) {
            // reservamos memoria para los argumentos del hilo
            thread_args_t *args = malloc(sizeof(thread_args_t));
            args->client_addr = client_addr;
            args->primer_paquete = primer_paquete;

            // creamos el hilo para que se encargue del resto de la transferencia
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, manejar_transferencia, args) != 0) {
                perror("error al crear el hilo");
                free(args);
            }
            // nos desentendemos del hilo para poder atender a otros clientes
            pthread_detach(thread_id);
        }
    }

    close(sock);
    return 0;
}