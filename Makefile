# Makefile para el proyecto de protocolo confiable

# Compilador a usar
CC = gcc

# Banderas de compilación:
# -Wall: Activa todas las advertencias (warnings), es una buena práctica.
# -g: Incluye información de depuración (para usar con gdb si fuera necesario).
CFLAGS = -Wall -g

# Banderas del enlazador (Linker):
# -lpthread: Le dice al enlazador que debe incluir la librería de Pthreads (hilos).
# Esto es necesario para el receptor.
LDFLAGS = -lpthread

# Objetivos finales: los programas que queremos crear
all: emisor receptor

# Regla para construir el 'emisor'
# Le dice a 'make' que 'emisor' depende de 'emisor.c' y 'protocolo.h'.
# Si alguno de esos archivos cambia, se ejecutará el comando.
emisor: emisor.c protocolo.h
	$(CC) $(CFLAGS) -o emisor emisor.c

# Regla para construir el 'receptor'
# Depende de 'receptor.c' y 'protocolo.h'.
# Nota el uso de $(LDFLAGS) para enlazar la librería de hilos.
receptor: receptor.c protocolo.h
	$(CC) $(CFLAGS) -o receptor receptor.c $(LDFLAGS)

# Regla 'clean': para limpiar los archivos generados
# Permite borrar los ejecutables y los archivos de salida para empezar de nuevo.
clean:
	rm -f emisor receptor *.o *.dat

.PHONY: all clean