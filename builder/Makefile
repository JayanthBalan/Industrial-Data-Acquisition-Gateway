
CC = $(CROSS_COMPILE)gcc

CFLAGS = -g -Wall -Wextra -Werror
CPPFLAGS = -I../include

all: acquiringd loggingd processingd telemetryd

sense_utils.o: ../src/sense_utils.c ../include/sense_utils.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

libsenseutils.a: sense_utils.o
	ar rcs $@ $^

acquiringd: ../src/acquiring_d.c ../include/...
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L. -lsenseutils -o $@

loggingd: ../src/logging_d.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L. -lsenseutils -o $@

processingd: ../src/processing_d.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L. -lsenseutils -o $@

telemetryd: ../src/telemetry_d.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L. -lsenseutils -o $@

clean:
	rm -f *.o *.a acquiringd loggingd processingd telemetryd

.PHONY: all clean
