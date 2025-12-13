#include <iostream>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>

// --- Windows/Socket Includes ---
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
// ---------------------------------

#define SERVER_IP "127.0.0.1" // Loopback address for local testing
#define PORT 5000
#define BUFFER_SIZE 100

// Generate a random vehicle number
void generateVehicleNumber(char *buffer)
{
  // Format: <2 alpha><1 digit><2 alpha><3 digit>
  std::srand(std::time(0) + std::rand()); // Re-seed for better randomness

  buffer[0] = 'A' + std::rand() % 26;
  buffer[1] = 'A' + std::rand() % 26;
  buffer[2] = '0' + std::rand() % 10;
  buffer[3] = 'A' + std::rand() % 26;
  buffer[4] = 'A' + std::rand() % 26;
  buffer[5] = '0' + std::rand() % 10;
  buffer[6] = '0' + std::rand() % 10;
  buffer[7] = '0' + std::rand() % 10;
  buffer[8] = '\0';
}

// Generate a random lane
char generateLane()
{
  char lanes[] = {'A', 'B', 'C', 'D'};
  return lanes[std::rand() % 4];
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
  char buffer[BUFFER_SIZE];

  // 1. Create socket
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Socket failed");
    return 1;
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);

  // Convert address
  if (inet_pton(AF_INET, SERVER_IP, &server_address.sin_addr) <= 0)
  {
    perror("Invalid address");
    closesocket(sock);
    return 1;
  }

  // 2. Connect to the server
  if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
  {
    perror("Connection failed");
    closesocket(sock);
    return 1;
  }

  std::cout << "Connected to server (Simulator)..." << std::endl;

  std::srand(std::time(NULL));

  // 3. Send loop
  while (true)
  {
    char vehicle[9];
    generateVehicleNumber(vehicle);
    char lane = generateLane();

    // Use snprintf to format data (C-style but robust)
    snprintf(buffer, BUFFER_SIZE, "%s:%c", vehicle, lane);

    // Send message
    if (send(sock, buffer, std::strlen(buffer), 0) == -1)
    {
      perror("send failed");
      break;
    }
    std::cout << "Sent: " << buffer << std::endl;

    // Wait 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  closesocket(sock);
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}