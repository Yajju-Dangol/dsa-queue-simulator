#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <SDL3/SDL.h>
#include <SDL3/SDL_ttf.h>
#include <cstdio>
#include <cstring>
#include <cmath>
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

#define PORT 5000
#define BUFFER_SIZE 100
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define ROAD_WIDTH 150
#define LANE_WIDTH 50

#define MAIN_FONT "C:/Windows/Fonts/arial.ttf"

std::atomic<int> currentLight = 0;
std::atomic<int> nextLight = 0;
std::mutex vehicleQueueMutex;
std::vector<std::string> vehicleQueue;

struct SharedData
{
  int currentLight;
  int nextLight;
};

struct Vehicle
{
  float x, y;
  float speed;
  // 1–3 A, 4–6 B, 7–9 C, 10–12 D
  int lane;
  SDL_Color bodyColor;
  bool active;
  bool horizontal;
};

std::vector<Vehicle> activeVehicles;

bool initializeSDL(SDL_Window **window, SDL_Renderer **renderer);
void drawRoadsAndLane(SDL_Renderer *renderer, TTF_Font *font);
void displayText(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y);
void refreshLight(SDL_Renderer *renderer);
void drawLightForB(SDL_Renderer *renderer, bool isRed);
void drawArrow(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, int x3, int y3);

void drawLightForA(SDL_Renderer *renderer, bool isRed);
void drawLightForB(SDL_Renderer *renderer, bool isRed);
void drawLightForC(SDL_Renderer *renderer, bool isRed);
void drawLightForD(SDL_Renderer *renderer, bool isRed);

void drawTrafficLight(SDL_Renderer *renderer, float x, float y, bool isRed, bool horizontal);

void drawCar(SDL_Renderer *renderer, Vehicle &v);

void spawnVehicle(int lane);
void updateVehicles();

void socketReceiverThread();
void lightControlThread();

void lightControlThread()
{
  while (true)
  {
    nextLight = 0;
    SDL_Delay(5000);

    nextLight = 2;
    SDL_Delay(5000);
  }
}

void socketReceiverThread()
{
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
  {
    std::cerr << "WSAStartup failed." << std::endl;
    return;
  }
#endif

  SOCKET server_fd, new_socket;
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  char buffer[BUFFER_SIZE] = {0};

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Socket failed");
    return;
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1)
  {
    perror("Bind failed");
    closesocket(server_fd);
    return;
  }

  if (listen(server_fd, 3) < 0)
  {
    perror("Listen failed");
    closesocket(server_fd);
    return;
  }

  std::cout << "Server listening on port " << PORT << "..." << std::endl;

  if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) == -1)
  {
    perror("Accept failed");
    closesocket(server_fd);
    return;
  }

  std::cout << "Client connected (Traffic Generator)..." << std::endl;

  while (true)
  {
    int bytes_read = recv(new_socket, buffer, BUFFER_SIZE - 1, 0);

    if (bytes_read > 0)
    {
      buffer[bytes_read] = '\0';
      std::string receivedData(buffer);

      std::lock_guard<std::mutex> lock(vehicleQueueMutex);
      vehicleQueue.push_back(receivedData);

      std::cout << "Received: " << receivedData << " (Queue size: " << vehicleQueue.size() << ")" << std::endl;

      // Clear buffer
      memset(buffer, 0, BUFFER_SIZE);
    }
    else if (bytes_read == 0)
    {
      std::cout << "Client disconnected." << std::endl;
      break;
    }
    else
    {
      perror("recv failed");
      break;
    }
  }

  closesocket(new_socket);
  closesocket(server_fd);
#ifdef _WIN32
  WSACleanup();
#endif
}

int main(int argc, char *argv[])
{
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;

  if (!initializeSDL(&window, &renderer))
  {
    return -1;
  }

  TTF_Font *font = TTF_OpenFont(MAIN_FONT, 24);
  if (!font)
  {
    SDL_Log("Failed to load font: %s", SDL_GetError());
  }

  std::thread receiver_t(socketReceiverThread);
  std::thread light_t(lightControlThread);

  bool running = true;
  SDL_Event event;

  while (running)
  {
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_EVENT_QUIT)
        running = false;
    }

    vehicleQueueMutex.lock();
    if (!vehicleQueue.empty())
    {
      std::string data = vehicleQueue.front();
      vehicleQueue.erase(vehicleQueue.begin());
      vehicleQueueMutex.unlock();

      try
      {
        if (!data.empty())
          spawnVehicle(std::stoi(data));
      }
      catch (...)
      {
      }
    }
    else
    {
      vehicleQueueMutex.unlock();
    }

    updateVehicles();

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    drawRoadsAndLane(renderer, font);
    refreshLight(renderer);

    SDL_RenderPresent(renderer);
    SDL_Delay(16); // ~60 FPS
  }

  receiver_t.detach();
  light_t.detach();

  if (font)
    TTF_CloseFont(font);
  if (renderer)
    SDL_DestroyRenderer(renderer);
  if (window)
    SDL_DestroyWindow(window);
  TTF_Quit();
  SDL_Quit();

#ifdef _WIN32
  WSACleanup();
#endif

  return 0;
}

bool initializeSDL(SDL_Window **window, SDL_Renderer **renderer)
{
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return false;
  }

  if (TTF_Init() < 0)
  {
    SDL_Log("SDL3_ttf could not initialize! TTF_Error: %s\n", SDL_GetError());
    SDL_Quit();
    return false;
  }

  *window = SDL_CreateWindow("Junction Diagram", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
  if (!*window)
  {
    SDL_Log("Failed to create window: %s", SDL_GetError());
    TTF_Quit();
    SDL_Quit();
    return false;
  }

  *renderer = SDL_CreateRenderer(*window, NULL);
  if (!*renderer)
  {
    SDL_Log("Failed to create renderer: %s", SDL_GetError());
    SDL_DestroyWindow(*window);
    TTF_Quit();
    SDL_Quit();
    return false;
  }
  return true;
}

void displayText(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y)
{
  SDL_Color textColor = {0, 0, 0, 255};
  SDL_Surface *textSurface = TTF_RenderText_Solid(font, text, strlen(text), textColor);

  if (!textSurface)
  {
    SDL_Log("TTF_RenderText_Solid failed: %s", SDL_GetError());
    return;
  }

  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, textSurface);
  SDL_DestroySurface(textSurface);

  if (!texture)
  {
    SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
    return;
  }

  SDL_FRect textRect = {(float)x, (float)y, 0.0f, 0.0f};

  SDL_GetTextureSize(texture, &textRect.w, &textRect.h);

  SDL_RenderTexture(renderer, texture, NULL, &textRect);
  SDL_DestroyTexture(texture);
}

void drawRoadsAndLane(SDL_Renderer *renderer, TTF_Font *font)
{
  float center = (float)WINDOW_WIDTH / 2.0f;
  float road_half = (float)ROAD_WIDTH / 2.0f;

  SDL_SetRenderDrawColor(renderer, 35, 35, 35, 255);
  SDL_FRect vRoad = {center - road_half, 0.0f, (float)ROAD_WIDTH, (float)WINDOW_HEIGHT};
  SDL_RenderFillRect(renderer, &vRoad);
  SDL_FRect hRoad = {0.0f, center - road_half, (float)WINDOW_WIDTH, (float)ROAD_WIDTH};
  SDL_RenderFillRect(renderer, &hRoad);

  SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
  float laneOffset = (float)LANE_WIDTH;

  for (int i = 1; i <= 2; ++i)
  {
    float x = (center - road_half) + laneOffset * i;
    SDL_FRect line = {x - 1.0f, 0.0f, 2.0f, (float)WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &line);
  }

  for (int i = 1; i <= 2; ++i)
  {
    float y = (center - road_half) + laneOffset * i;
    SDL_FRect line = {0.0f, y - 1.0f, (float)WINDOW_WIDTH, 2.0f};
    SDL_RenderFillRect(renderer, &line);
  }

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  float dashW = 4.0f;
  float dashH = 20.0f;
  float gap = 20.0f;
  for (float y = 0; y < WINDOW_HEIGHT; y += (dashH + gap))
  {
    if (y > center - road_half && y < center + road_half)
      continue;
    SDL_FRect dash = {center - (dashW / 2.0f), y, dashW, dashH};
    SDL_RenderFillRect(renderer, &dash);
  }
  for (float x = 0; x < WINDOW_WIDTH; x += (dashH + gap))
  {
    if (x > center - road_half && x < center + road_half)
      continue;
    SDL_FRect dash = {x, center - (dashW / 2.0f), dashH, dashW};
    SDL_RenderFillRect(renderer, &dash);
  }

  displayText(renderer, font, "A", (int)center + 10, 10);
  displayText(renderer, font, "B", (int)center + 10, WINDOW_HEIGHT - 40);
  displayText(renderer, font, "D", 10, (int)center + 10);
  displayText(renderer, font, "C", WINDOW_WIDTH - 40, (int)center + 10);

  int lState = nextLight.load();
  drawLightForA(renderer, lState != 1);
  drawLightForC(renderer, lState != 1);
  drawLightForB(renderer, lState != 2);
  drawLightForD(renderer, lState != 2);

  for (auto &v : activeVehicles)
  {
    drawCar(renderer, v);
  }
}

void refreshLight(SDL_Renderer *renderer)
{
  if (nextLight.load() == currentLight.load())
    return;

  currentLight = nextLight.load();
  std::cout << "Light state updated to " << currentLight.load() << std::endl;
}

void drawArrow(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, int x3, int y3)
{
  SDL_RenderLine(renderer, (float)x1, (float)y1, (float)x2, (float)y2);
  SDL_RenderLine(renderer, (float)x2, (float)y2, (float)x3, (float)y3);
  SDL_RenderLine(renderer, (float)x3, (float)y3, (float)x1, (float)y1);
}

void drawLightForA(SDL_Renderer *renderer, bool isRed)
{

  drawTrafficLight(renderer, 380.0f, 355.0f, isRed, true);
}

void drawLightForB(SDL_Renderer *renderer, bool isRed)
{

  drawTrafficLight(renderer, 375.0f, 450.0f, isRed, true);
}

void drawLightForC(SDL_Renderer *renderer, bool isRed)
{

  drawTrafficLight(renderer, 450.0f, 380.0f, isRed, false);
}

void drawLightForD(SDL_Renderer *renderer, bool isRed)
{

  drawTrafficLight(renderer, 350.0f, 380.0f, isRed, false);
}
void drawTrafficLight(SDL_Renderer *renderer, float x, float y, bool isRed, bool horizontal)
{
  SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
  SDL_FRect housing = {x, y, (horizontal ? 45.0f : 25.0f), (horizontal ? 25.0f : 45.0f)};
  SDL_RenderFillRect(renderer, &housing);

  float size = 15.0f;
  float padding = 5.0f;

  if (isRed)
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
  else
    SDL_SetRenderDrawColor(renderer, 60, 0, 0, 255);
  SDL_FRect redLamp = {x + padding, y + padding, size, size};
  SDL_RenderFillRect(renderer, &redLamp);

  if (!isRed)
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
  else
    SDL_SetRenderDrawColor(renderer, 0, 60, 0, 255);

  SDL_FRect greenLamp = {
      horizontal ? x + padding + 20.0f : x + padding,
      horizontal ? y + padding : y + padding + 20.0f,
      size, size};
  SDL_RenderFillRect(renderer, &greenLamp);
}

void drawCar(SDL_Renderer *renderer, Vehicle &v)
{
  if (!v.active)
    return;

  float w = v.horizontal ? 40.0f : 25.0f;
  float h = v.horizontal ? 25.0f : 40.0f;

  SDL_SetRenderDrawColor(renderer, v.bodyColor.r, v.bodyColor.g, v.bodyColor.b, 255);
  SDL_FRect body = {v.x, v.y, w, h};
  SDL_RenderFillRect(renderer, &body);

  SDL_SetRenderDrawColor(renderer, 150, 200, 255, 255);
  SDL_FRect glass;
  if (v.lane >= 1 && v.lane <= 3)
    glass = {v.x + 3, v.y + 25, 19, 8};
  else if (v.lane >= 4 && v.lane <= 6)
    glass = {v.x + 3, v.y + 7, 19, 8};
  else if (v.lane >= 7 && v.lane <= 9)
    glass = {v.x + 7, v.y + 3, 8, 19};
  else if (v.lane >= 10 && v.lane <= 12)
    glass = {v.x + 25, v.y + 3, 8, 19};
  SDL_RenderFillRect(renderer, &glass);

  SDL_SetRenderDrawColor(renderer, 255, 255, 150, 255);
  if (v.lane >= 1 && v.lane <= 3)
  {
    SDL_FRect h1 = {v.x + 4, v.y + 34, 4, 4}, h2 = {v.x + 17, v.y + 34, 4, 4};
    SDL_RenderFillRect(renderer, &h1);
    SDL_RenderFillRect(renderer, &h2);
  }
  else if (v.lane >= 4 && v.lane <= 6)
  {
    SDL_FRect h1 = {v.x + 4, v.y + 2, 4, 4}, h2 = {v.x + 17, v.y + 2, 4, 4};
    SDL_RenderFillRect(renderer, &h1);
    SDL_RenderFillRect(renderer, &h2);
  }
  else if (v.lane >= 7 && v.lane <= 9)
  {
    SDL_FRect h1 = {v.x + 2, v.y + 4, 4, 4}, h2 = {v.x + 2, v.y + 17, 4, 4};
    SDL_RenderFillRect(renderer, &h1);
    SDL_RenderFillRect(renderer, &h2);
  }
  else if (v.lane >= 10 && v.lane <= 12)
  {
    SDL_FRect h1 = {v.x + 34, v.y + 4, 4, 4}, h2 = {v.x + 34, v.y + 17, 4, 4};
    SDL_RenderFillRect(renderer, &h1);
    SDL_RenderFillRect(renderer, &h2);
  }
}

void spawnVehicle(int lane)
{
  Vehicle v;
  v.active = true;
  v.speed = 2.0f;
  v.bodyColor = {(Uint8)(rand() % 255), (Uint8)(rand() % 255), (Uint8)(rand() % 255), 255};
  float center = WINDOW_WIDTH / 2.0f;
  float road_half = (float)ROAD_WIDTH / 2.0f;

  switch (lane)
  {
  case 1:
  case 2:
  case 3:
  {
    int sub = lane - 1; 
    float carW = 25.0f;
    float laneInnerOffset = ((float)LANE_WIDTH - carW) / 2.0f;
    float startX = center - road_half + laneInnerOffset;
    v.x = startX + sub * (float)LANE_WIDTH;
    v.y = 50.0f;
    v.horizontal = false;
    break;
  }
  case 4:
  case 5:
  case 6:
  {
    int sub = lane - 4;
    float carW = 25.0f;
    float laneInnerOffset = ((float)LANE_WIDTH - carW) / 2.0f;
    float startX = center - road_half + laneInnerOffset;
    v.x = startX + sub * (float)LANE_WIDTH;
    v.y = 700.0f;
    v.horizontal = false;
    break;
  }
  case 7:
  case 8:
  case 9:
  {
    int sub = lane - 7;
    float carH = 25.0f;
    float laneInnerOffset = ((float)LANE_WIDTH - carH) / 2.0f;
    float startY = center - road_half + laneInnerOffset;
    v.y = startY + sub * (float)LANE_WIDTH;
    v.x = 700.0f;
    v.horizontal = true;
    break;
  }
  case 10:
  case 11:
  case 12:
  {
    int sub = lane - 10;
    float carH = 25.0f;
    float laneInnerOffset = ((float)LANE_WIDTH - carH) / 2.0f;
    float startY = center - road_half + laneInnerOffset;
    v.y = startY + sub * (float)LANE_WIDTH;
    v.x = 50.0f;
    v.horizontal = true;
    break;
  }
  default:
    return;
  }
  v.lane = lane;
  activeVehicles.push_back(v);
}

void updateVehicles()
{
  int lState = nextLight.load();

  std::vector<Vehicle *> laneGroups[13]; 
  for (auto &v : activeVehicles)
  {
    if (v.lane >= 1 && v.lane <= 12)
      laneGroups[v.lane].push_back(&v);
  }

  auto sortLane = [&](int laneStart, int laneEnd, bool vertical, bool increasing)
  {
    for (int lane = laneStart; lane <= laneEnd; ++lane)
    {
      auto &vec = laneGroups[lane];
      if (vertical)
      {
        if (increasing)
          std::sort(vec.begin(), vec.end(), [](Vehicle *a, Vehicle *b)
                    { return a->y < b->y; });
        else
          std::sort(vec.begin(), vec.end(), [](Vehicle *a, Vehicle *b)
                    { return a->y > b->y; });
      }
      else
      {
        if (increasing)
          std::sort(vec.begin(), vec.end(), [](Vehicle *a, Vehicle *b)
                    { return a->x < b->x; });
        else
          std::sort(vec.begin(), vec.end(), [](Vehicle *a, Vehicle *b)
                    { return a->x > b->x; });
      }
    }
  };

  sortLane(1, 3, true, false);
  sortLane(4, 6, true, true);
  sortLane(7, 9, false, true);
  
  sortLane(10, 12, false, false);

  float minGap = 45.0f;

  auto canAdvance = [&](Vehicle *v)
  {
    if ((v->lane >= 1 && v->lane <= 3) && v->y >= 280 && v->y <= 290 && lState != 1)
      return false;
    if ((v->lane >= 4 && v->lane <= 6) && v->y <= 480 && v->y >= 470 && lState != 2)
      return false;
    if ((v->lane >= 7 && v->lane <= 9) && v->x <= 480 && v->x >= 470 && lState != 1)
      return false;
    if ((v->lane >= 10 && v->lane <= 12) && v->x >= 280 && v->x <= 290 && lState != 2)
      return false;
    return true;
  };

  auto moveVertical = [&](int laneStart, int laneEnd, bool increasing)
  {
    for (int lane = laneStart; lane <= laneEnd; ++lane)
    {
      auto &vec = laneGroups[lane];
      for (size_t i = 0; i < vec.size(); ++i)
      {
        Vehicle *v = vec[i];
        if (!canAdvance(v))
          continue;

        float proposedY = v->y + (increasing ? v->speed : -v->speed);

        if (i > 0)
        {
          Vehicle *front = vec[i - 1];
          if (increasing)
          {
            if (front->y - proposedY < minGap)
              continue;
          }
          else
          {
            if (proposedY - front->y < minGap)
              continue;
          }
        }

        v->y = proposedY;
      }
    }
  };

  auto moveHorizontal = [&](int laneStart, int laneEnd, bool increasing)
  {
    for (int lane = laneStart; lane <= laneEnd; ++lane)
    {
      auto &vec = laneGroups[lane];
      for (size_t i = 0; i < vec.size(); ++i)
      {
        Vehicle *v = vec[i];
        if (!canAdvance(v))
          continue;

        float proposedX = v->x + (increasing ? v->speed : -v->speed);

        if (i > 0)
        {
          Vehicle *front = vec[i - 1];
          if (increasing)
          {
            if (front->x - proposedX < minGap)
              continue;
          }
          else
          {
            if (proposedX - front->x < minGap)
              continue;
          }
        }

        v->x = proposedX;
      }
    }
  };

  
  moveVertical(1, 3, true);  
  moveVertical(4, 6, false);  
  moveHorizontal(7, 9, false); 
  moveHorizontal(10, 12, true); 

  activeVehicles.erase(std::remove_if(activeVehicles.begin(), activeVehicles.end(),
                                      [](const Vehicle &v)
                                      { return v.x < -100 || v.x > 900 || v.y < -100 || v.y > 900; }),
                       activeVehicles.end());
}