#!/bin/bash

if [ "$#" -eq 1 ] && [ "$1" = "build" ]; then
    make -f project-build.mk
fi

./sensor-sims/client-sensors 127.0.0.1 9000 sensor-sims/sensors-data.txt 4 30000 > /dev/null 2>&1 &
BG_PID=$!

cleanup() {
    kill "$BG_PID" 2>/dev/null
    wait "$BG_PID" 2>/dev/null
}

trap cleanup EXIT INT TERM

./tester/dev-interface 127.0.0.1 9196 500
