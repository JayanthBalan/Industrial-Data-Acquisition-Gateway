
CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar

CFLAGS = -g -Wall -Wextra -Werror -I../include

LIBS = -L. -lsenseutils

DAEMONS = acquiringd loggingd processingd telemetryd

all: $(DAEMONS)

sense_utils.o: ../src/sense_utils.c ../include/sense_utils.h
	$(CC) $(CFLAGS) -c $< -o $@

libsenseutils.a: sense_utils.o
	$(AR) rcs $@ $^

process_init.o: ../src/process_init.c ../include/process_init.h
	$(CC) $(CFLAGS) -c $< -o $@

acquiringd: ../src/acquiring_d.c process_init.o
	$(CC) $(CFLAGS) $< process_init.o -o $@

loggingd: ../src/logging_d.c process_init.o libsenseutils.a
	$(CC) $(CFLAGS) $< process_init.o $(LIBS) -o $@

processingd: ../src/processing_d.c process_init.o libsenseutils.a
	$(CC) $(CFLAGS) $< process_init.o $(LIBS) -o $@

telemetryd: ../src/telemetry_d.c process_init.o libsenseutils.a
	$(CC) $(CFLAGS) $< process_init.o $(LIBS) -o $@

clean:
	rm -f $(DAEMONS) *.o *.a

.PHONY: all clean
