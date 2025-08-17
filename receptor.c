// receptor.c (Versión Final con Sockets Dedicados por Hilo)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocolo.h"

// Estructura para pasar argumentos iniciales al hilo
typedef struct {
    struct sockaddr_in client_addr;
    paquete_t primer_paquete;
} thread_args_t;

// Mutex para proteger la salida estándar (printf) y evitar mensajes mezclados
pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

void *manejar_transferencia(void *args) {
    thread_args_t *thread_args = (thread_args_t *)args;
    struct sockaddr_in client_addr = thread_args->client_addr;
    paquete_t paquete_recibido = thread_args->primer_paquete;
    free(args); // Liberamos la memoria de los argumentos lo antes posible

    // PASO 1: Crear un nuevo socket exclusivo para esta transferencia
    int new_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (new_sock < 0) {
        perror("Error al crear socket en el hilo");
        return NULL;
    }

    // PASO 2: Hacer bind del nuevo socket a un puerto efímero (aleatorio)
    struct sockaddr_in thread_serv_addr;
    memset(&thread_serv_addr, 0, sizeof(thread_serv_addr));
    thread_serv_addr.sin_family = AF_INET;
    thread_serv_addr.sin_addr.s_addr = INADDR_ANY;
    thread_serv_addr.sin_port = htons(0); // Puerto 0 significa "dame cualquiera disponible"

    if (bind(new_sock, (struct sockaddr*)&thread_serv_addr, sizeof(thread_serv_addr)) < 0) {
        perror("Error en bind del hilo");
        close(new_sock);
        return NULL;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

    pthread_mutex_lock(&stdout_mutex);
    printf("Hilo %ld: Iniciando transferencia con %s:%d\n", pthread_self(), client_ip, ntohs(client_addr.sin_port));
    pthread_mutex_unlock(&stdout_mutex);

    char nombre_archivo[256];
    sprintf(nombre_archivo, "recibido_de_%s_%d.dat", client_ip, ntohs(client_addr.sin_port));
    FILE* fp = fopen(nombre_archivo, "wb");
    if (!fp) {
        perror("Error al abrir archivo de salida");
        close(new_sock);
        return NULL;
    }

    uint32_t seq_esperada = 0;
    socklen_t client_len = sizeof(client_addr);
    
    // Procesa el primer paquete que ya recibimos en el hilo principal
    fwrite(paquete_recibido.data, 1, paquete_recibido.data_len, fp);
    
    paquete_t paquete_ack;
    paquete_ack.is_ack = 1;
    paquete_ack.ack_num = seq_esperada;
    
    // PASO 3: Enviar el primer ACK desde el NUEVO socket. Esto le informa al emisor del nuevo puerto.
    sendto(new_sock, &paquete_ack, sizeof(paquete_t), 0, (struct sockaddr*)&client_addr, client_len);
    seq_esperada++;

    // Bucle principal de la transferencia, escuchando en el NUEVO socket
    while (1) {
        ssize_t bytes_recibidos = recvfrom(new_sock, &paquete_recibido, sizeof(paquete_t), 0, NULL, NULL);
        if (bytes_recibidos <= 0) {
            pthread_mutex_lock(&stdout_mutex);
            fprintf(stderr, "Hilo %ld: Error en recvfrom o fin de conexión. Terminando.\n", pthread_self());
            pthread_mutex_unlock(&stdout_mutex);
            break;
        }

        if (paquete_recibido.seq_num == seq_esperada) {
            fwrite(paquete_recibido.data, 1, paquete_recibido.data_len, fp);
            paquete_ack.ack_num = seq_esperada;
            sendto(new_sock, &paquete_ack, sizeof(paquete_t), 0, (struct sockaddr*)&client_addr, client_len);

            if (paquete_recibido.is_fin) {
                pthread_mutex_lock(&stdout_mutex);
                printf("Hilo %ld: Transferencia completada. Archivo guardado como %s\n", pthread_self(), nombre_archivo);
                pthread_mutex_unlock(&stdout_mutex);
                break;
            }
            seq_esperada++;
        } else {
            // Reenviar ACK del último paquete correcto en caso de duplicados
            paquete_ack.ack_num = seq_esperada - 1; 
            sendto(new_sock, &paquete_ack, sizeof(paquete_t), 0, (struct sockaddr*)&client_addr, client_len);
        }
    }
    
    fclose(fp);
    close(new_sock); // Cerramos el socket del hilo
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(1);
    }

    int puerto = atoi(argv[1]);
    int sock = socket(AF_INET, SOCK_DGRAM, 0); // Socket principal de escucha

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(puerto);

    if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error en bind");
        exit(1);
    }
    printf("Receptor concurrente escuchando en el puerto %d...\n", puerto);
    
    // Bucle infinito para aceptar nuevas conexiones
    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        paquete_t primer_paquete;

        // El hilo principal solo escucha por el primer paquete (seq_num 0)
        ssize_t bytes_recibidos = recvfrom(sock, &primer_paquete, sizeof(paquete_t), 0,
                                           (struct sockaddr*)&client_addr, &client_len);

        if (bytes_recibidos > 0 && primer_paquete.seq_num == 0 && !primer_paquete.is_ack) {
            // Creamos los argumentos para el hilo hijo
            thread_args_t *args = malloc(sizeof(thread_args_t));
            args->client_addr = client_addr;
            args->primer_paquete = primer_paquete;

            // Creamos el hilo para que se encargue del resto de la transferencia
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, manejar_transferencia, args) != 0) {
                perror("Error al crear el hilo");
                free(args);
            }
            pthread_detach(thread_id); // No esperamos a que el hilo termine
        }
    }

    close(sock);
    return 0;
}