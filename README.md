# Traffic Queue Simulator

A simple traffic simulation project that demonstrates Queue Data Structures using C++ and SDL3.

## How it Works
This system consists of two separate programs that talk to each other:
1. **Simulator (Server)**: This opens the graphical window, draws the roads, and manages the traffic lights and moving cars. It receives car data from the generator.
2. **Traffic Generator (Client)**: This runs in the console. It generates random vehicles and places them into **Queues** (Road A, B, C, D). It acts as the "producer" and sends vehicles to the Simulator.

## Prerequisites
- A C++ Compiler (like g++)
- Make tool
- SDL3 libraries (located in the `SDL-3` folder)

## How to Run

Follow these simple steps to start the simulation.

### Step 1: Build the Project
Open your terminal in the project folder and run:
```bash
make
```
This will compile the code and create the executables in the `build` folder.

### Step 2: Start the Simulator
Run the simulator first so it can listen for connections. In your terminal, type:
```bash
./build/simulator.exe
```
*You should see a window open with the road layout.*

### Step 3: Start the Traffic Generator
Open a **new, separate terminal window** (keep the first one running!). navigate to the project folder, and run:
```bash
./build/trafficgenerator.exe
```
*You will be asked to enter a traffic speed (1-10). Enter a number and press Enter.*

## Controls
- **Traffic Speed**: When running the Generator, typing `10` creates heavy traffic, while `1` creates light traffic.
- **Traffic Lights**: The simulation automatically adjusts traffic lights based on which road has the most cars waiting (Priority Scheduling).

## Project Structure
- `src/Simulator.cpp`: Handles graphics, animation, and traffic light logic.
- `src/TrafficGenerator.cpp`: Handles vehicle creation and queue management.

## Preview
![traffic-simulator](https://github.com/user-attachments/assets/d95cba5b-e39d-4ad2-956d-c98691bb3cb0)




