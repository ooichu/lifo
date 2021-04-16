F = -std=c89 -Wall -Wextra -pedantic -DLF_STANDALONE -lm -O3
CC = gcc

build:
	$(CC) -olifo src/lifo.c $(F)

run:
	./lifo

clean:
	rm lifo
