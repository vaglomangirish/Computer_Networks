CC = gcc
FLAGS = -pthread

all: Initialize_deco clean

Initialize_deco: Initialize_deco.o
	$(CC) -o $@ $^ $(FLAGS)

clean:
	rm *.o
