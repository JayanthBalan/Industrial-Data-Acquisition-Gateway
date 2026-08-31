
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Wextra -Werror

all: acquiringd loggingd processingd telemeryd

acquiringd: acquiring_d.c
	$(CC) $(CFLAGS) acquiring_d.c -o acquiringd

loggingd: logging_d.c
	$(CC) $(CFLAGS) logging_d.c -o loggingd

processingd: processing_d.c
	$(CC) $(CFLAGS) processing_d.c -o processingd

telemeryd: telemetry_d.c
	$(CC) $(CFLAGS) telemetry_d.c -o telemetryd

clean:
	rm -f acquiringd loggingd processingd telemetryd

.PHONY: all clean
