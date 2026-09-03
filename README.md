
# Industrial Data Acquisition Gateway

An Embedded Linux-based industrial sensor data acquisition and monitoring gateway built using C, POSIX APIs, TCP sockets, POSIX message queues, pthreads, Buildroot, and QEMU ARM64 emulation.

The project simulates multiple industrial sensors sending data to an embedded Linux device. The device processes the sensor frames through multiple daemon processes and provides a TCP-based interface for monitoring sensor data, latency, throughput, and sensor status.

## Architecture

The system consists of two main sides.

### Host side

**client-sensors**
- Simulates multiple sensor clients.
- Sends sensor packets to the device.
- One client thread is used for latency measurement.
- The remaining threads continuously send simulated sensor data from `sensors-data.txt`.

**dev-interface**
- Connects to the telemetry interface running inside the device.
- Allows querying:
  - All sensors
  - Sensor type
  - Individual sensor ID
  - Online sensors
  - Offline sensors
  - Latency
  - Throughput

### Embedded device side

The Buildroot-based ARM64 device runs multiple daemon processes:

```
Sensor Clients
      |
      | TCP Port 9000
      v
 acquiringd
      |
      | POSIX Message Queue
      v
 processingd -----------------
      |                      |
      | POSIX Message Queues |
      |                      |
      v                      v
 loggingd                telemetryd
                             |
                             | TCP Port 9196
                             v
                         Device Interface
```

All four daemons start automatically via init scripts as soon as the device boots, so there's nothing to launch manually on the device side once QEMU is up.

The device is emulated using QEMU.

## Packet and Frame Protocol

### Sensor packet protocol

The simulated sensor clients communicate with the device using the following packet format:

| Sync | Type | Sensor ID | Data Length | Sensor Data |
| --- | --- | --- | --- | --- |
| 1 byte | 1 byte | 2 bytes | 2 bytes | variable |

The sync field is:

```
0xAA
```

The sensor ID and data length are transmitted in little-endian byte order.

The received sensor packet is parsed and converted into the internal Sensor_t frame used by the device daemons.

### Internal sensor frame

The device internally processes sensor information using the `Sensor_t` structure. It contains information such as:

- Sensor data
- Sensor timestamp
- Last seen timestamp
- Sensor ID
- Sensor name
- Sensor type
- Sensor state
- Frame type

Sensor frames can be:

- `SENSOR_HEARTBEAT`
- `SENSOR_DATA`

The `last_seen_time` is used by the watchdog thread to determine whether a sensor is online or offline (a sensor is marked offline after 30 seconds without a new frame).

### Latency measurement

A dedicated simulated sensor uses:

- Sensor ID: `65000` (`0xFDE8`)
- Sensor Type: `0x2B`

The latency client embeds a `CLOCK_REALTIME` timestamp directly inside its sensor data:

```
8 bytes  -> seconds
4 bytes  -> nanoseconds
```

This timestamp is carried through the acquisition and processing pipeline.

When the device interface requests the latency sensor, `telemetryd` extracts the embedded timestamp from the sensor data and returns:

```
LATENCY: <seconds> <nanoseconds>
```

The host-side `dev-interface` compares this timestamp with its current `CLOCK_REALTIME` value and calculates the end-to-end latency.

## Build and Run

### 1. Clone the repository

Clone the project including its required Buildroot setup.

```bash
git clone --recurse-submodules <repository-url>
```

If the repository was already cloned:

```bash
git submodule init
git submodule sync
git submodule update
```

### 2. Commit and push your changes

Before building the embedded device, commit and push your latest changes.

```bash
git add .
git commit -m "Your commit message"
git push
```

Get the latest commit ID:

```bash
git rev-parse HEAD
```

Copy the resulting commit hash.

### 3. Update the package .mk file

Open:

```
base_external/package/industrial-data-acquisition-gateway/industrial-data-acquisition-gateway.mk
```

Update:

```
INDUSTRIAL_DATA_ACQUISITION_GATEWAY_VERSION =
```

with your latest commit ID. For example:

```
INDUSTRIAL_DATA_ACQUISITION_GATEWAY_VERSION = 4396ed3fc999dac539a821eddb09581e9748e37b
```

This is important because Buildroot downloads and builds the exact Git commit specified in this file.

### 4. Use the saved Buildroot configuration

The saved device configuration is located in:

```
base_external/configs/
```

The defconfig enables the required Buildroot packages and the Industrial Data Acquisition Gateway package. Use the saved defconfig when creating or rebuilding the Buildroot configuration.

> ```bash
> make -C buildroot defconfig BR2_EXTERNAL=$(pwd)/base_external BR2_DEFCONFIG=$(pwd)/base_external/configs/_qemu_defconfig
> ```

### 5. Build the device

Run:

```bash
./builder/build-device.sh
```

The script initializes the required submodules and builds the Buildroot ARM64 image.

The build output is generated inside:

```
buildroot/output/images/
```

Depending on the system and whether Buildroot needs to rebuild packages, this can take some time.

### 6. Run the device

Start the ARM64 device using:

```bash
./runqemu.sh
```

QEMU forwards the following host ports:

```
Host Port 9000  -> Sensor acquisition interface
Host Port 9196  -> Telemetry interface
Host Port 10022 -> SSH
```

Keep the QEMU terminal running while testing the project.

## Running the Host-Side Programs

There are two ways to run the sensor simulation and device interface.

### Option 1: Run manually in separate terminals

**Terminal 1 — Sensor simulation**

```bash
./sensor-sims/client-sensors 127.0.0.1 9000 sensor-sims/sensors-data.txt <number_of_clients> <delay_microseconds>
```

Example:

```bash
./sensor-sims/client-sensors 127.0.0.1 9000 sensor-sims/sensors-data.txt 4 30000
```

Parameters:

```
127.0.0.1              QEMU host address
9000                    Sensor acquisition port
sensors-data.txt        Simulated sensor data
4                       Number of client threads
30000                   Delay between packets in microseconds
```

One of the client threads is always reserved for latency measurement, so with 4 threads you get 1 latency client and 3 regular sensor clients.

**Terminal 2 — Device interface**

```bash
./tester/dev-interface 127.0.0.1 9196 <latency_poll_delay>
```

Example:

```bash
./tester/dev-interface 127.0.0.1 9196 500
```

The interface supports:

```
GET ALL
GET TYPE POWCURRVOLT
GET TYPE TOR
GET TYPE TEMPRESS
GET TYPE PROX
GET ONLINE
GET OFFLINE
GET ID <Sensor_ID_hex>
GET LATENCY
GET THROUGHPUT
```

`GET ID` takes the sensor ID in hex — for example, `GET ID FDE8` queries the latency sensor.

Example:

```
> GET THROUGHPUT
THROUGHPUT: 60.22 Kbps

> GET LATENCY
Average latency from the last 5 samples: 17576 microseconds
```

### Option 2: Use the Default Run Script

The project also provides:

```bash
./runproject-default.sh
```

This starts `client-sensors` in the background and `dev-interface` in the foreground.

The default parameters are:

```bash
./sensor-sims/client-sensors 127.0.0.1 9000 sensor-sims/sensors-data.txt 4 30000
```

and:

```bash
./tester/dev-interface 127.0.0.1 9196 500
```

The sensor simulator output is suppressed, so the terminal is controlled only by `dev-interface`. When `dev-interface` exits, the script automatically terminates the background `client-sensors` process.

### Rebuilding the host-side programs

If changes were made to:

```
tester/dev-interface.c
sensor-sims/client-sensors.c
```

the programs need to be rebuilt. You can directly use:

```bash
make -f project-build.mk
```

Alternatively, the default run script can rebuild the programs before starting:

```bash
./runproject-default.sh build
```

## Performance Notes

The sensor simulation parameters involve tradeoffs.

**Increasing the number of sensor client threads** — generally:
- Higher throughput.
- More concurrent sensor traffic.
- Higher processing and scheduling overhead.
- Can increase packet loss, missed/corrupted data, dropped frames or simulation-side contention under heavier load.

**Increasing the delay between sensor packets** — generally:
- Lower traffic load.
- More stable operation.
- Better tolerance against the configured latency/processing thresholds.
- Lower overall throughput.

**Reducing the delay** — generally:
- Higher packet generation rate.
- Higher throughput.
- Greater system load.
- Can increase latency or cause missed packets depending on the workload.

The exact values depend on the chosen client count, packet delay, host machine load, QEMU performance, and scheduling behaviour.

## Project Structure

```
Industrial-Data-Acquisition-Gateway/
│
├── base_external/
│   ├── configs/
│   │   └── _qemu_defconfig   Saved Buildroot defconfig
│   │
│   ├── package/
│   │   └── industrial-data-acquisition-gateway/
│   │       ├── industrial-data-acquisition-gateway.mk
│   │       └── Config.in
│   ├── Config.in
│   ├── external.mk
│   └── external.desc
│
│
├── builder/
│   ├── build-device.sh
│   ├── save-config.sh
│   └── Makefile                     Builds the 4 device daemons
│
├── include/
│   ├── sense.h
│   ├── sense_utils.h
│   └── process_init.h
│
├── src/
│   ├── acquiring_d.c
│   ├── processing_d.c
│   ├── logging_d.c
│   ├── telemetry_d.c
│   ├── sense_utils.c
│   └── process_init.c
│
├── sensor-sims/
│   ├── client-sensors.c
│   ├── client-sensors
│   └── sensors-data.txt
│
├── tester/
│   ├── dev-interface
│   └── dev-interface.c
│
├── project-build.mk
├── runproject-default.sh
├── latency-log.csv
├── README.md
├── shared.sh
├── daemons4d-start-stop
├── mount1q-start-stop
├── network-start-stop
└── runqemu.sh
```

## Typical Workflow

For a fresh build:

```bash
git add .
git commit -m "Update project"
git push
```

Get the commit ID:

```bash
git rev-parse HEAD
```

Update the commit ID in:

```
base_external/package/industrial-data-acquisition-gateway/industrial-data-acquisition-gateway.mk
```

Then:

```bash
./builder/build-device.sh
```

Start the device:

```bash
./runqemu.sh
```

Finally, either run the host programs manually in two terminals, or use:

```bash
./runproject-default.sh
```

If the host-side C files were changed:

```bash
make -f project-build.mk
```

Then run the project normally.
