#include <iostream>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>

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

int generateLane()
{
  return 1 + std::rand() % 12;
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

  std::srand((unsigned int)std::time(NULL));

  while (true)
  {
    int lane = generateLane();
    std::snprintf(buffer, BUFFER_SIZE, "%d", lane);

    if (send(sock, buffer, std::strlen(buffer), 0) == -1)
    {
      perror("send failed");
      break;
    }
    std::cout << "Sent lane: " << buffer << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  closesocket(sock);
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}


