
all: tester/dev-interface sensor-sims/client-sensors

tester/dev-interface: tester/dev-interface.c
	gcc tester/dev-interface.c -o tester/dev-interface

sensor-sims/client-sensors: sensor-sims/client-sensors.c
	gcc sensor-sims/client-sensors.c -o sensor-sims/client-sensors

.PHONY: all
