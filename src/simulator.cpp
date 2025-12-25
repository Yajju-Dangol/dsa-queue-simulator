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
  int pathOption; // 0 = Straight/Shift, 1 = Turn Right
  SDL_Color bodyColor;
  bool active;
  bool horizontal;
  
  // Turning state
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
  // std::thread light_t(lightControlThread);

  bool running = true;
  SDL_Event event;

  // Traffic Light Logic State
  Uint32 lastLightSwitchTime = SDL_GetTicks();
  int lightPhase = 1; // 1=A, 2=B, 3=C, 4=D
  int targetPhase = 1;
  bool isTransitioning = false; // If true, all red
  int priorityLane = -1; // -1 none, 0=A, 1=B, 2=C, 3=D

  auto countVehiclesOnRoad = [](int roadIndex) -> int {
      int count = 0;
      for (const auto& v : activeVehicles) {
          if (!v.active) continue;
          if (v.turning) continue; // Don't count cars already in intersection

          // Road A (Lanes 1-3) (Moves Down, Stop ~280)
          if (roadIndex == 0 && v.lane >= 1 && v.lane <= 3) {
             if (v.y <= 295) count++; // Count including stop line
          }
          // Road B (Lanes 4-6) (Moves Up, Stop ~480)
          else if (roadIndex == 1 && v.lane >= 4 && v.lane <= 6) {
             if (v.y >= 465) count++; 
          }
          // Road C (Lanes 7-9) (Moves Left, Stop ~480)
          else if (roadIndex == 2 && v.lane >= 7 && v.lane <= 9) {
             if (v.x >= 465) count++; 
          }
          // Road D (Lanes 10-12) (Moves Right, Stop ~280)
          else if (roadIndex == 3 && v.lane >= 10 && v.lane <= 12) {
             if (v.x <= 295) count++; 
          }
      }
      return count;
  };

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

    // --- Traffic Light Logic ---
    Uint32 currentTime = SDL_GetTicks();
    
    // Check for Priority
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

    // Determine Target Phase (if not transitioning)
    if (!isTransitioning) {
        targetPhase = lightPhase; // Default to stay
        
        if (priorityLane != -1) {
            if (lightPhase != priorityLane + 1) {
                targetPhase = priorityLane + 1;
            }
        } else {
            // Normal Cycle
            if (currentTime - lastLightSwitchTime > 3000) { 
                 bool found = false;
                 // Look for next road with cars
                 for (int i = 1; i <= 4; i++) {
                     int checkIndex = (lightPhase - 1 + i) % 4; // Check A, B, C, D order relative to current
                     if (countVehiclesOnRoad(checkIndex) > 0) {
                         targetPhase = checkIndex + 1;
                         found = true;
                         break;
                     }
                 }
                 // If all empty, cycle to next anyway to keep alive
                 if (!found) {
                     targetPhase = (lightPhase % 4) + 1;
                 }
            }
        }
    }

    // State Machine
    if (lightPhase != targetPhase) {
        if (!isTransitioning) {
            // Start Transition
            isTransitioning = true;
            lastLightSwitchTime = currentTime;
            nextLight = 0; // All Red
        } 
        else {
            // Wait for clearance (1s)
            if (currentTime - lastLightSwitchTime > 1000) {
                lightPhase = targetPhase;
                nextLight = lightPhase;
                isTransitioning = false;
                lastLightSwitchTime = currentTime;
            }
        }
    } else {
        // Enforce current light
        if (!isTransitioning) {
             nextLight = lightPhase;
        }
    }
    // ---------------------------

    updateVehicles();

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    drawRoadsAndLane(renderer, font);
    refreshLight(renderer);

    SDL_RenderPresent(renderer);
    SDL_Delay(16); // ~60 FPS
  }

  receiver_t.detach();
  // light_t.detach();

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

  // Lane labels
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
  // Phase 1: A Green
  drawLightForA(renderer, lState != 1);
  // Phase 2: B Green
  drawLightForB(renderer, lState != 2);
  // Phase 3: C Green
  drawLightForC(renderer, lState != 3);
  // Phase 4: D Green
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

    // Local corners (relative to center 0,0)
    // TL: -hw, -hh
    // TR: hw, -hh
    // BR: hw, hh
    // BL: -hw, hh

    struct Point { float x, y; };
    Point corners[4] = {
        {-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}
    };

    SDL_Vertex verts[4];
    for (int i = 0; i < 4; i++) {
        // Rotate
        float rx = corners[i].x * c - corners[i].y * s;
        float ry = corners[i].x * s + corners[i].y * c;
        // Translate
        verts[i].position.x = cx + rx;
        verts[i].position.y = cy + ry;
        verts[i].color = { (float)color.r/255.0f, (float)color.g/255.0f, (float)color.b/255.0f, (float)color.a/255.0f };
        verts[i].tex_coord = { 0.0f, 0.0f };
    }

    int indices[6] = { 0, 1, 2, 2, 3, 0 };
    SDL_RenderGeometry(renderer, NULL, verts, 4, indices, 6);
}

// Map lane to angle: D(Right)=0, A(Down)=90, C(Left)=180, B(Up)=270
float getLaneAngle(int lane) {
    if (lane >= 1 && lane <= 3) return 90.0f;
    if (lane >= 4 && lane <= 6) return 270.0f;
    if (lane >= 7 && lane <= 9) return 180.0f;
    return 0.0f;
}

void drawCar(SDL_Renderer *renderer, Vehicle &v)
{
  if (!v.active) return;
  
  float angle = getLaneAngle(v.lane);
  
  if (v.turning) {
      float target = getLaneAngle(v.targetLane);
      // Handle wrap-around for smooth rotation
      // 270 -> 0  => 270 -> 360
      // 0 -> 270  => 360 -> 270
      if (std::abs(target - angle) > 180.0f) {
          if (target < angle) target += 360.0f;
          else angle += 360.0f;
      }
      angle = angle + (target - angle) * v.t;
  }

  // Determine Center. v.x, v.y is Top-Left of the align bounding box approx.
  // We use fixed offsets based on 40x25 or 25x40 base depending on 'horizontal'
  // But actually, v.horizontal tells us the rect shape.
  // if v.horizontal, center is x+20, y+12.5. If not, x+12.5, y+20.
  // We strictly use v.horizontal (Start State) to determine Center offset, 
  // because v.x, v.y follows the path of that start state until end of turn.
  float cx = v.x + (v.horizontal ? 20.0f : 12.5f);
  float cy = v.y + (v.horizontal ? 12.5f : 20.0f);

  // Draw Body (40x25 base, rotated)
  fillRotatedBox(renderer, cx, cy, 40.0f, 25.0f, angle, v.bodyColor);

  // Draw Windshield (offset +10 in X local from center? No, Right side is front for 0 deg)
  // Windshield is ~8 wide, 19 high (relative to car body 40x25).
  // Position: Car Front is +X (Right). Windshield is near front.
  // Rel center: x=+10, y=0.
  fillRotatedBox(renderer, 
      cx + 10.0f * std::cos(angle * 3.14159f/180.0f), 
      cy + 10.0f * std::sin(angle * 3.14159f/180.0f), 
      8.0f, 19.0f, angle, {150, 200, 255, 255});

  // Headlights
  // Top-Right and Bot-Right (relative)
  // x=+18, y=-10 and y=+10
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
    // A Needs 1
    if ((v->lane >= 1 && v->lane <= 3) && v->y >= 280 && v->y <= 290 && lState != 1)
      return false;
    // B Needs 2
    if ((v->lane >= 4 && v->lane <= 6) && v->y <= 480 && v->y >= 470 && lState != 2)
      return false;
    // C Needs 3
    if ((v->lane >= 7 && v->lane <= 9) && v->x <= 480 && v->x >= 470 && lState != 3)
      return false;
    // D Needs 4
    if ((v->lane >= 10 && v->lane <= 12) && v->x >= 280 && v->x <= 290 && lState != 4)
      return false;
    return true;
  };

  auto updateTurn = [&](Vehicle *v)
  {
      v->t += v->t_speed;

      // DO NOT switch visuals halfway. Wait until turn is complete.
      // if (v->t >= 0.5f && v->lane != v->targetLane) ... REMOVED

      if (v->t >= 1.0f)
      {
          v->t = 1.0f;
          v->turning = false;
          // Ensure final state
          v->lane = v->targetLane;
          v->horizontal = v->targetHorizontal; // Switch orientation only here
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
      // Boost turn speed by 1.6x for faster completion
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

       
       
        if (v->lane == 3 && v->y >= 307.5f && v->y < 380.0f) // Trigger earlier
        {
           // A3 (Down) -> D10 (Right). Shortened P2 X (487.5)
           startTurn(v, 10, true, 437.5f, 337.5f, 487.5f, 337.5f);
        }
       
        else if (v->lane == 4 && v->y <= 467.5f && v->y > 400.0f)
        {
           // B4 (Up) -> C9 (Left). Shortened P2 X (287.5)
           startTurn(v, 9, true, 337.5f, 437.5f, 287.5f, 437.5f);
        }
        
       
        else if (v->lane == 2)
        {
            if (v->pathOption == 1 && v->y >= 407.5f && v->y <= 445.0f) 
            {
               // A2 (Down) -> C9 (Left). Shortened P2 X (300.0)
               startTurn(v, 9, true, 387.5f, 437.5f, 300.0f, 437.5f);
            }
            else if (v->pathOption == 0 && v->y >= 380.0f && v->y <= 400.0f) // Shift
            {
                // Smooth Shift A2 -> A3 (P0=387.5, P2=437.5)
                startTurn(v, 3, false, 412.5f, v->y + 50.0f, 437.5f, v->y + 100.0f);
            }
        }
       
        else if (v->lane == 5)
        {
            if (v->pathOption == 1 && v->y <= 367.5f && v->y >= 330.0f)
            {
                // B5 (Up) -> D10 (Right). Shortened P2 X (450.0)
                startTurn(v, 10, true, 387.5f, 337.5f, 450.0f, 337.5f);
            }
            else if (v->pathOption == 0 && v->y <= 420.0f && v->y >= 400.0f) // Shift
            {
                // Smooth Shift B5 -> B4 (P0=387.5, P2=337.5)
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

        // Lane 9 -> 3 (Left) (Moves Left)
        if (v->lane == 9 && v->x <= 467.5f && v->x > 420.0f)
        {
           // C9 (Left) -> A3 (Down). Shortened P2 Y (517.5)
           startTurn(v, 3, false, 437.5f, 437.5f, 437.5f, 517.5f);
        }
        // Lane 10 -> 4 (Left) (Moves Right)
        else if (v->lane == 10 && v->x >= 307.5f && v->x < 380.0f)
        {
           // D10 (Right) -> B4 (Up). Shortened P2 Y (257.5)
           startTurn(v, 4, false, 337.5f, 337.5f, 337.5f, 257.5f);
        }
        // Lane 8 -> 4 (Right) (Moves Left)
        else if (v->lane == 8)
        {
             if (v->pathOption == 1 && v->x <= 367.5f && v->x >= 330.0f)
             {
                 // C8 (Left) -> B4 (Up). Shortened P2 Y (270.0)
                 startTurn(v, 4, false, 337.5f, 387.5f, 337.5f, 270.0f); 
             }
             else if (v->pathOption == 0 && v->x <= 420.0f && v->x >= 400.0f) // Shift
             {
                 // Smooth Shift C8 -> C9 (P0=387.5, P2=437.5)
                 startTurn(v, 9, false, v->x - 50.0f, 412.5f, v->x - 100.0f, 437.5f);
             }
        }
        // Lane 11 -> 3 (Right) (Moves Right)
        else if (v->lane == 11)
        {
            if (v->pathOption == 1 && v->x >= 407.5f && v->x <= 445.0f)
            {
                // D11 (Right) -> A3 (Down). Shortened P2 Y (530.0)
                startTurn(v, 3, false, 437.5f, 387.5f, 437.5f, 530.0f);
            }
            else if (v->pathOption == 0 && v->x >= 380.0f && v->x <= 400.0f) // Shift
            {
                // Smooth Shift D11 -> D10 (P0=387.5, P2=337.5)
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