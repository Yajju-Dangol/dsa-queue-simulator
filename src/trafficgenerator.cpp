#include <iostream>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
typedef int SOCKET;
#endif

#define SERVER_IP "127.0.0.1"
#define PORT 5000
#define BUFFER_SIZE 100

// Vehicle structure to store vehicle information
struct Vehicle {
    int lane;          
    int road;           
    int vehicleId;      
    double timestamp;   
};

// Queue data structure for vehicles
class VehicleQueue {
private:
    std::queue<Vehicle> queue;
    std::mutex queueMutex;
    int roadId;         
    int vehicleCount;   

public:
    VehicleQueue(int road) : roadId(road), vehicleCount(0) {}

    // Enqueue a vehicle
    void enqueue(const Vehicle& vehicle) {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push(vehicle);
        vehicleCount++;
    }

    // Dequeue a vehicle (FIFO)
    bool dequeue(Vehicle& vehicle) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (queue.empty()) {
            return false;
        }
        vehicle = queue.front();
        queue.pop();
        return true;
    }

    // Dequeue a vehicle from a specific lane (for priority handling)
    bool dequeueFromLane(int lane, Vehicle& vehicle) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (queue.empty()) {
            return false;
        }
        
        // Try to find a vehicle from the specified lane
        std::queue<Vehicle> tempQueue;
        bool found = false;
        
        while (!queue.empty()) {
            Vehicle v = queue.front();
            queue.pop();
            if (v.lane == lane && !found) {
                vehicle = v;
                found = true;
            } else {
                tempQueue.push(v);
            }
        }
        
        // Restore queue
        queue = tempQueue;
        return found;
    }

    // Count vehicles in a specific lane
    int countLaneVehicles(int lane) {
        std::lock_guard<std::mutex> lock(queueMutex);
        int count = 0;
        std::queue<Vehicle> tempQueue = queue;
        while (!tempQueue.empty()) {
            if (tempQueue.front().lane == lane) {
                count++;
            }
            tempQueue.pop();
        }
        return count;
    }

    // Check if queue is empty
    bool isEmpty() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return queue.empty();
    }

    // Get queue size
    int size() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return queue.size();
    }

    // Get road ID
    int getRoadId() const {
        return roadId;
    }

    // Get total vehicle count for this road
    int getVehicleCount() const {
        return vehicleCount;
    }
};

// Global vehicle queues for each road (A, B, C, D)
VehicleQueue roadAQueue(0);  
VehicleQueue roadBQueue(1);  
VehicleQueue roadCQueue(2);  
VehicleQueue roadDQueue(3);  

// Get the appropriate queue for a given lane
VehicleQueue* getQueueForLane(int lane) {
    if (lane >= 1 && lane <= 3) return &roadAQueue;   
    if (lane >= 4 && lane <= 6) return &roadBQueue;   
    if (lane >= 7 && lane <= 9) return &roadCQueue;   
    if (lane >= 10 && lane <= 12) return &roadDQueue; 
    return nullptr;
}

// Get road identifier from lane
int getRoadFromLane(int lane) {
    if (lane >= 1 && lane <= 3) return 0;   
    if (lane >= 4 && lane <= 6) return 1;   
    if (lane >= 7 && lane <= 9) return 2;   
    if (lane >= 10 && lane <= 12) return 3; 
    return -1;
}

// Generate a random lane number (1-12)
int generateLane() {
    static const int validLanes[] = {2, 3, 4, 5, 8, 9, 10, 11};
    int index = std::rand() % 8;
    return validLanes[index];
}

// Generate a vehicle and add it to the appropriate queue
void generateVehicle() {
    int lane = generateLane();
    int road = getRoadFromLane(lane);
    
    if (road == -1) {
        std::cerr << "Invalid lane generated: " << lane << std::endl;
        return;
    }

    Vehicle vehicle;
    vehicle.lane = lane;
    vehicle.road = road;
    static int globalVehicleId = 1;
    vehicle.vehicleId = globalVehicleId++;
    vehicle.timestamp = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    VehicleQueue* queue = getQueueForLane(lane);
    if (queue) {
        queue->enqueue(vehicle);
        std::cout << "Generated vehicle #" << vehicle.vehicleId 
                  << " for Road " << (char)('A' + road) 
                  << " Lane " << lane 
                  << " (Queue size: " << queue->size() << ")" << std::endl;
    }
}

// Global flag to track if we're in priority mode for AL2
static bool priorityModeActive = false;

// Function to process queues and send vehicles to simulator
void processQueuesAndSend(SOCKET sock) {
    
    int al2Count = roadAQueue.countLaneVehicles(2); 
    
    if (al2Count > 10) {
        priorityModeActive = true;
    } else if (al2Count < 5) {
        priorityModeActive = false;   
    }
    
    if (priorityModeActive && al2Count >= 5) {
        Vehicle vehicle;
        if (roadAQueue.dequeueFromLane(2, vehicle)) {
            char buffer[BUFFER_SIZE];
            std::snprintf(buffer, BUFFER_SIZE, "%d", vehicle.lane);
            
            if (send(sock, buffer, std::strlen(buffer), 0) == -1) {
                perror("send failed");
                return;
            }
            int remaining = roadAQueue.countLaneVehicles(2);
            std::cout << "PRIORITY: Sent vehicle from AL2 (Lane 2) - Remaining: " << remaining 
                      << " (Queue size: " << roadAQueue.size() << ")" << std::endl;
            return;
        }
    }
    
    VehicleQueue* queues[] = {&roadAQueue, &roadBQueue, &roadCQueue, &roadDQueue};
    
    for (int i = 0; i < 4; i++) {
        VehicleQueue* queue = queues[i];
        if (!queue->isEmpty()) {
            Vehicle vehicle;
            if (queue->dequeue(vehicle)) {
                char buffer[BUFFER_SIZE];
                std::snprintf(buffer, BUFFER_SIZE, "%d", vehicle.lane);
                
                if (send(sock, buffer, std::strlen(buffer), 0) == -1) {
                    perror("send failed");
                    return;
                }
                std::cout << "Sent vehicle from Road " << (char)('A' + queue->getRoadId())
                          << " Lane " << vehicle.lane 
                          << " (Queue size: " << queue->size() << ")" << std::endl;
                return; 
            }
        }
    }
}

int main()
{
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
  {
    std::cerr << "WSAStartup failed." << std::endl;
    return 1;
  }
#endif

  SOCKET sock = -1;
  struct sockaddr_in server_address;

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Socket failed");
    return 1;
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);

  if (inet_pton(AF_INET, SERVER_IP, &server_address.sin_addr) <= 0)
  {
    perror("Invalid address");
    closesocket(sock);
    return 1;
  }

  if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
  {
    perror("Connection failed");
    closesocket(sock);
    return 1;
  }

  std::cout << "Connected to server (Simulator)..." << std::endl;
  std::cout << "Queue-based vehicle generation system initialized." << std::endl;
  std::cout << "Road A (lanes 1-3), Road B (lanes 4-6), Road C (lanes 7-9), Road D (lanes 10-12)" << std::endl;

  std::srand((unsigned int)std::time(NULL));

  std::thread generatorThread([&]() {
    while (true) {
      generateVehicle();
      int delay = 1 + std::rand() % 3;
      std::this_thread::sleep_for(std::chrono::seconds(delay));
    }
  });

  while (true) {
    processQueuesAndSend(sock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 + std::rand() % 1000));
  }

  generatorThread.detach();
  closesocket(sock);
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}


