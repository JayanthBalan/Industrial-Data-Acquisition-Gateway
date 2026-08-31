
CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar

CFLAGS = -g -Wall -Wextra -Werror -I../include

SRC_DIR = ../src

LIB = libsense_utils.a
LIB_OBJ = sense_utils.o

all: $(LIB) acquiringd loggingd processingd telemetryd

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(LIB_OBJ): $(SRC_DIR)/sense_utils.c
	$(CC) $(CFLAGS) -c $< -o $@

acquiringd: $(SRC_DIR)/acquiring_d.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lsense_utils -o $@

loggingd: $(SRC_DIR)/logging_d.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lsense_utils -o $@

processingd: $(SRC_DIR)/processing_d.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lsense_utils -o $@

telemetryd: $(SRC_DIR)/telemetry_d.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lsense_utils -o $@

clean:
	rm -f $(LIB_OBJ) $(LIB)
	rm -f acquiringd processingd loggingd telemetryd

.PHONY: all clean
