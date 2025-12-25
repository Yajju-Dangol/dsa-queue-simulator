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

// Global atomic variables for thread-safe light state
std::atomic<int> currentLight = 0;
std::atomic<int> nextLight = 0;
std::mutex vehicleQueueMutex;
std::vector<std::string> vehicleQueue;

struct SharedData
{
  int currentLight;
  int nextLight;
};

// Main vehicle structure with physics and state
struct Vehicle
{
  float x, y;
  float speed;
  int lane;
  int pathOption; 
  SDL_Color bodyColor;
  bool active;
  bool horizontal;
  
  bool turning;
  float t;
  float t_speed;
  float p0x, p0y;
  float p1x, p1y;
  float p2x, p2y;
  int targetLane;
  bool targetHorizontal;
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




// Background thread that listens for incoming vehicle data from generator
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
  // Initialize SDL window and renderer
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

  bool running = true;
  SDL_Event event;

  Uint32 lastLightSwitchTime = SDL_GetTicks();
  int lightPhase = 1;
  int targetPhase = 1;
  bool isTransitioning = false;
  int priorityLane = -1;

  auto countVehiclesOnRoad = [](int roadIndex) -> int {
      int count = 0;
      for (const auto& v : activeVehicles) {
          if (!v.active) continue;
          if (v.turning) continue;

          if (roadIndex == 0 && v.lane >= 1 && v.lane <= 3) {
             if (v.y <= 295) count++; 
          }
          
          else if (roadIndex == 1 && v.lane >= 4 && v.lane <= 6) {
             if (v.y >= 465) count++; 
          }
          
          else if (roadIndex == 2 && v.lane >= 7 && v.lane <= 9) {
             if (v.x >= 465) count++; 
          }
          
          else if (roadIndex == 3 && v.lane >= 10 && v.lane <= 12) {
             if (v.x <= 295) count++; 
          }
      }
      return count;
  };

  // Main game loop: handles input, updates, and rendering
  while (running)
  {
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_EVENT_QUIT)
        running = false;
    }

    // Process incoming vehicle queue from network thread
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

    Uint32 currentTime = SDL_GetTicks();
    
    // Adaptive Traffic Light Logic: checks density to assign priority
    if (priorityLane == -1) {
        for (int i = 0; i < 4; i++) {
            if (countVehiclesOnRoad(i) >= 6) {
                priorityLane = i;
                std::cout << "Priority mode activated for Road " << (char)('A' + i) << std::endl;
                break;
            }
        }
    } else {
        if (countVehiclesOnRoad(priorityLane) <= 3) {
            std::cout << "Priority mode deactivated for Road " << (char)('A' + priorityLane) << std::endl;
            priorityLane = -1;
        }
    }

    if (!isTransitioning) {
        targetPhase = lightPhase; 
        
        if (priorityLane != -1) {
            if (lightPhase != priorityLane + 1) {
                targetPhase = priorityLane + 1;
            }
        } else {
            if (currentTime - lastLightSwitchTime > 3000) { 
                 bool found = false;
                 for (int i = 1; i <= 4; i++) {
                     int checkIndex = (lightPhase - 1 + i) % 4;
                     if (countVehiclesOnRoad(checkIndex) > 0) {
                         targetPhase = checkIndex + 1;
                         found = true;
                         break;
                     }
                 }
                 
                 if (!found) {
                     targetPhase = (lightPhase % 4) + 1;
                 }
            }
        }
    }

    if (lightPhase != targetPhase) {
        if (!isTransitioning) {
            isTransitioning = true;
            lastLightSwitchTime = currentTime;
            nextLight = 0; 
        } 
        else {
            if (currentTime - lastLightSwitchTime > 1000) {
                lightPhase = targetPhase;
                nextLight = lightPhase;
                isTransitioning = false;
                lastLightSwitchTime = currentTime;
            }
        }
    } else {
        if (!isTransitioning) {
             nextLight = lightPhase;
        }
    }

    // Update physics for all cars
    updateVehicles();

    // Render everything
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    drawRoadsAndLane(renderer, font);
    refreshLight(renderer);

    SDL_RenderPresent(renderer);
    SDL_Delay(16); 
  }

  receiver_t.detach();

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
  // Initialize the graphics engine (SDL3)
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

  *window = SDL_CreateWindow("Traffic Simulator", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
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
  SDL_Color textColor = {255, 255, 255, 255};
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

// Draws static road geometry and markings
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
  float laneCenterOffset = laneOffset / 2.0f;

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

  float topY = 10.0f;
  float bottomY = WINDOW_HEIGHT - 40.0f;
  float leftX = 10.0f;
  float rightX = WINDOW_WIDTH - 60.0f;

  for (int i = 0; i < 3; ++i)
  {
    float xTopBottom = (center - road_half) + laneOffset * i + laneCenterOffset - 10.0f;
    displayText(renderer, font, (std::string("A") + std::to_string(i + 1)).c_str(), (int)xTopBottom, (int)topY);
    displayText(renderer, font, (std::string("B") + std::to_string(i + 1)).c_str(), (int)xTopBottom, (int)bottomY);

    float yLeftRight = (center - road_half) + laneOffset * i + laneCenterOffset - 10.0f;
    displayText(renderer, font, (std::string("D") + std::to_string(i + 1)).c_str(), (int)leftX, (int)yLeftRight);
    displayText(renderer, font, (std::string("C") + std::to_string(i + 1)).c_str(), (int)rightX, (int)yLeftRight);
  }

  int lState = nextLight.load();
  drawLightForA(renderer, lState != 1);
  drawLightForB(renderer, lState != 2);
  drawLightForC(renderer, lState != 3);
  drawLightForD(renderer, lState != 4);

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

  drawTrafficLight(renderer, 380.0f, 345.0f, isRed, true);
}

void drawLightForB(SDL_Renderer *renderer, bool isRed)
{

  drawTrafficLight(renderer, 375.0f, 430.0f, isRed, true);
}

void drawLightForC(SDL_Renderer *renderer, bool isRed)
{

  drawTrafficLight(renderer, 430.0f, 380.0f, isRed, false);
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

void fillRotatedBox(SDL_Renderer* renderer, float cx, float cy, float w, float h, float angleDeg, SDL_Color color)
{
    float rad = angleDeg * 3.14159f / 180.0f;
    float c = std::cos(rad);
    float s = std::sin(rad);

    float hw = w / 2.0f;
    float hh = h / 2.0f;

    struct Point { float x, y; };
    Point corners[4] = {
        {-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}
    };

    SDL_Vertex verts[4];
    for (int i = 0; i < 4; i++) {
        float rx = corners[i].x * c - corners[i].y * s;
        float ry = corners[i].x * s + corners[i].y * c;
        verts[i].position.x = cx + rx;
        verts[i].position.y = cy + ry;
        verts[i].color = { (float)color.r/255.0f, (float)color.g/255.0f, (float)color.b/255.0f, (float)color.a/255.0f };
        verts[i].tex_coord = { 0.0f, 0.0f };
    }

    int indices[6] = { 0, 1, 2, 2, 3, 0 };
    SDL_RenderGeometry(renderer, NULL, verts, 4, indices, 6);
}


float getLaneAngle(int lane) {
    if (lane >= 1 && lane <= 3) return 90.0f;
    if (lane >= 4 && lane <= 6) return 270.0f;
    if (lane >= 7 && lane <= 9) return 180.0f;
    return 0.0f;
}

// Renders individual vehicle with rotation and lighting
void drawCar(SDL_Renderer *renderer, Vehicle &v)
{
  if (!v.active) return;
  
  float angle = getLaneAngle(v.lane);
  
  if (v.turning) {
      float target = getLaneAngle(v.targetLane);
      if (std::abs(target - angle) > 180.0f) {
          if (target < angle) target += 360.0f;
          else angle += 360.0f;
      }
      angle = angle + (target - angle) * v.t;
  }

  float cx = v.x + (v.horizontal ? 20.0f : 12.5f);
  float cy = v.y + (v.horizontal ? 12.5f : 20.0f);

  fillRotatedBox(renderer, cx, cy, 40.0f, 25.0f, angle, v.bodyColor);

  fillRotatedBox(renderer, 
      cx + 10.0f * std::cos(angle * 3.14159f/180.0f), 
      cy + 10.0f * std::sin(angle * 3.14159f/180.0f), 
      8.0f, 19.0f, angle, {150, 200, 255, 255});

  float rad = angle * 3.14159f / 180.0f;
  float c = std::cos(rad);
  float s = std::sin(rad);
  
  auto drawHeadlight = [&](float lx, float ly) {
      float rx = lx * c - ly * s;
      float ry = lx * s + ly * c;
      fillRotatedBox(renderer, cx + rx, cy + ry, 4.0f, 4.0f, angle, {255, 255, 150, 255});
  };
  drawHeadlight(18.0f, -8.0f);
  drawHeadlight(18.0f, 8.0f);
}





// Creates a new vehicle object based on lane data
void spawnVehicle(int lane)
{
  if (lane == 1 || lane == 6 || lane == 7 || lane == 12)
    return;

  Vehicle v;
  v.active = true;
  v.speed = 2.0f;
  v.pathOption = std::rand() % 2;
  v.bodyColor = {(Uint8)(rand() % 255), (Uint8)(rand() % 255), (Uint8)(rand() % 255), 255};
  v.turning = false;
  v.t = 0.0f;

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

// Core update loop: physics, sorting, and logic
void updateVehicles()
{
  int lState = nextLight.load();

  std::vector<Vehicle *> laneGroups[13]; 
  for (auto &v : activeVehicles)
  {
    if (v.lane >= 1 && v.lane <= 12)
      laneGroups[v.lane].push_back(&v);
  }

  // Sort vehicles to handle rendering depth (painter's algorithm)
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

  // Check for collisions and red lights
  auto canAdvance = [&](Vehicle *v)
  {
    if ((v->lane >= 1 && v->lane <= 3) && v->y >= 280 && v->y <= 290 && lState != 1)
      return false;
    if ((v->lane >= 4 && v->lane <= 6) && v->y <= 480 && v->y >= 470 && lState != 2)
      return false;
    if ((v->lane >= 7 && v->lane <= 9) && v->x <= 480 && v->x >= 470 && lState != 3)
      return false;
    if ((v->lane >= 10 && v->lane <= 12) && v->x >= 280 && v->x <= 290 && lState != 4)
      return false;
    return true;
  };

  // Handle Bezier curve interpolation for turning
  auto updateTurn = [&](Vehicle *v)
  {
      v->t += v->t_speed;

      

      if (v->t >= 1.0f)
      {
          v->t = 1.0f;
          v->turning = false;
          v->lane = v->targetLane;
          v->horizontal = v->targetHorizontal; 
          v->x = v->p2x;
          v->y = v->p2y;
      }
      else
      {
          float u = 1.0f - v->t;
          float tt = v->t * v->t;
          float uu = u * u;
          v->x = uu * v->p0x + 2 * u * v->t * v->p1x + tt * v->p2x;
          v->y = uu * v->p0y + 2 * u * v->t * v->p1y + tt * v->p2y;
      }
  };

  auto startTurn = [&](Vehicle *v, int tLane, bool tHorz, float p1x, float p1y, float p2x, float p2y)
  {
      v->turning = true;
      v->t = 0.0f;
      v->targetLane = tLane;
      v->targetHorizontal = tHorz;
      v->p0x = v->x;
      v->p0y = v->y;
      v->p1x = p1x;
      v->p1y = p1y;
      v->p2x = p2x;
      v->p2y = p2y;
      
      float dx = v->p0x - v->p2x;
      float dy = v->p0y - v->p2y;
      float dist = std::sqrt(dx*dx + dy*dy);
      
      float len = dist * 1.11f;
      if (len < 1.0f) len = 1.0f;
      v->t_speed = (v->speed * 3.0f) / len; 
  };

  auto moveVertical = [&](int laneStart, int laneEnd, bool increasing)
  {
    for (int lane = laneStart; lane <= laneEnd; ++lane)
    {
      auto &vec = laneGroups[lane];
      for (size_t i = 0; i < vec.size(); ++i)
      {
        Vehicle *v = vec[i];
        
        if (v->turning) {
            updateTurn(v);
            continue;
        }

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

       
       
        if (v->lane == 3 && v->y >= 307.5f && v->y < 380.0f) 
        {
           startTurn(v, 10, true, 437.5f, 337.5f, 487.5f, 337.5f);
        }
       
        else if (v->lane == 4 && v->y <= 467.5f && v->y > 400.0f)
        {
           startTurn(v, 9, true, 337.5f, 437.5f, 287.5f, 437.5f);
        }
        
       
        else if (v->lane == 2)
        {
            if (v->pathOption == 1 && v->y >= 407.5f && v->y <= 445.0f) 
            {
               startTurn(v, 9, true, 387.5f, 437.5f, 300.0f, 437.5f);
            }
            else if (v->pathOption == 0 && v->y >= 380.0f && v->y <= 400.0f) 
            {
                
                startTurn(v, 3, false, 412.5f, v->y + 50.0f, 437.5f, v->y + 100.0f);
            }
        }
       
        else if (v->lane == 5)
        {
            if (v->pathOption == 1 && v->y <= 367.5f && v->y >= 330.0f)
            {
                startTurn(v, 10, true, 387.5f, 337.5f, 450.0f, 337.5f);
            }
            else if (v->pathOption == 0 && v->y <= 420.0f && v->y >= 400.0f) 
            {
                startTurn(v, 4, false, 362.5f, v->y - 50.0f, 337.5f, v->y - 100.0f);
            }
        }
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

        if (v->turning) {
            updateTurn(v);
            continue;
        }

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

        if (v->lane == 9 && v->x <= 467.5f && v->x > 420.0f)
        {
           startTurn(v, 3, false, 437.5f, 437.5f, 437.5f, 517.5f);
        }
        else if (v->lane == 10 && v->x >= 307.5f && v->x < 380.0f)
        {
           startTurn(v, 4, false, 337.5f, 337.5f, 337.5f, 257.5f);
        }
        
        else if (v->lane == 8)
        {
             if (v->pathOption == 1 && v->x <= 367.5f && v->x >= 330.0f)
             {
                 startTurn(v, 4, false, 337.5f, 387.5f, 337.5f, 270.0f); 
             }
             else if (v->pathOption == 0 && v->x <= 420.0f && v->x >= 400.0f) 
             {
                 
                 startTurn(v, 9, false, v->x - 50.0f, 412.5f, v->x - 100.0f, 437.5f);
             }
        }
        
        else if (v->lane == 11)
        {
            if (v->pathOption == 1 && v->x >= 407.5f && v->x <= 445.0f)
            {
                
                startTurn(v, 3, false, 437.5f, 387.5f, 437.5f, 530.0f);
            }
            else if (v->pathOption == 0 && v->x >= 380.0f && v->x <= 400.0f) 
            {
                
                startTurn(v, 10, false, v->x + 50.0f, 362.5f, v->x + 100.0f, 337.5f);
            }
        }
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