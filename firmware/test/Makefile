# Pruebas en PC de la lógica que no depende del hardware.
#   make -C firmware/test
CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter -O1 -I../main -D_DEFAULT_SOURCE
LDLIBS = -lm

all: test_estado test_spwm
	./test_estado
	./test_spwm

test_estado: test_estado.c ../main/estado.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_spwm: test_spwm.c ../main/spwm.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f test_estado test_spwm

.PHONY: all clean
