# Home Invader Repellent
Written by: Amytis Saghafi, Melissa Mosavi, Richard Kim, Simon Schaufele

## PROGRAM OUTLINE
### 1 - Description
Our proposed project is a home security system designed to provide a simple and reliable solution for residential safety. The system will consist of individual door modules, each equipped with a stepper motor, an ultrasonic sensor, and a BeagleY-AI board. These modules will monitor the state of the doors and communicate with a central BeagleY-AI control unit, which interprets the data and forwards updates to the user’s personal device (e.g., phone, tablet, or laptop) via wifi connection. For the scope of this project and given hardware limitations, we will implement three door modules along with a single central control unit.
### 2 - Connectivity
Each door module would be connected to a BeagleY-AI board with networking capabilities, allowing it to communicate with a central BeagleY-AI board that communicates with the user's mobile device via a wifi connection. 
The central BeagleY-AI board will send updates to the user. <br>
**Option 1:** A Discord webhook where alerts are routed to the user through Discord
**Option 2 (if time allows):** A mobile UI that includes a control hub (e.g., sends alerts as well as allowing the user to control the lock on the door)

### Core Components
- 4× BeagleY-AI Boards:# Configure and build
  - 3× Door module controllerscmake ..
  - 1× Central control unitmake -j4
- 3× HC-SR04 Ultrasonic Sensors```
- 3× Stepper Motors with Driver Boards
- 3× Demo Door Units (3D printed or wooden)### Cross-Compiling (if needed)

### Optional Components

- Front door camera module## Running the Application

## BEAGLE-Y-AI PIN CONNECTIONS 

- **STEPPER MOTOR**
    - IN1: GPIO 04
    - IN2: GPIO 22
    - IN3: GPIO 05
    - IN4: GPIO 06

- **ULTRASONIC SENSOR (HC-SR04)**:
    - trig: GPIO27 (gpiochip1, line 33)
    - echo: GPIO17 (gpiochip2, line 8)

## RUNNING CMAKE
- for host:
    ```shell
        ./buildHost.sh
    ```
- for target:
    ```shell
        ./buildTarget.sh
    ```


### Manually Running CMake

To manually run CMake from the command line use:

#### Host (build for the machine you're running on)

    ```shell
    # Regenerate build/ folder and makefiles:
    rm -rf build/         # Wipes temporary build folder
    cmake -S . -B build   # Generate makefiles in build

    # Build (compile & link) the project
    cmake --build build
    ```

#### Target (cross-compile for aarch64)

Use one of these minimal options (do this from the repo root). Remove the old `build/` or `build/CMakeCache.txt` before changing compilers.

    Option A — pass compilers directly to CMake:

        ```shell
        rm -rf build/
        cmake -S . -B build \
        -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
        -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
        cmake --build build
        ```

    Option B — set environment variables for a one-shot configure:

        ```shell
        rm -rf build/
        CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ cmake -S . -B build
        cmake --build build
        ```
## WLAN SETUP

### SETUP GUIDE
source: https://docs.beagleboard.org/boards/beagley/ai/02-quick-start.html <br><br>
Once board is fully booted and you have access to the shell, follow the commands below to connect to any WiFi access point, <br>

- To list the wireless devices attached, (you should see wlan0 listed) <br>
`iwctl device list` <br><br>
- Scan WiFi using,<br>
`iwctl station wlan0 scan`<br><br>
- Get networks using, <br>
`iwctl station wlan0 get-networks` <br><br>
- Connect to your wifi network using,<br>
`iwctl --passphrase "<wifi-pass>" station wlan0 connect "<wifi-name>"` <br><br>
- Check wlan0 status with, <br>
`iwctl station wlan0 show` <br><br>
- To list the networks with connected WiFi marked you can again use, <br>
`iwctl station wlan0 get-networks` <br><br>
- Test connection with ping command, <br>
`ping 8.8.8.8`<br>

### WIFI information
The wifi for this project is called 351network. PW defender351

iwctl --passphrase "defender351" station wlan0 connect "351network"

## NETCAT INFORMATION
Created two ports for UDP function:

 - Used port 12345 for "1.2 Listening to UDP" functions
 - Used port 12346 to complete "1.4 Terminal Output" Secion in Assignment Instructions and print to host.
    - BYAI terminal outputs as per "1.6 Local output" instruction
    - BYAI terminal and Host terminal print information simutaneosly
 - Can use both ports 12345 and 12346 in different terminal windows.

NETCAT COMMANDS
netcat -u 192.168.7.2 12345 
nc -u -l -p 12346

