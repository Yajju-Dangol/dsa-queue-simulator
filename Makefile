CC = g++
CFLAGS = -I SDL-3/include -std=c++17
LDFLAGS = -L SDL-3/lib -lSDL3 -lSDL3_ttf
WINLIBS = -lws2_32 -pthread

# Output directories and source files
BUILD_DIR = build
SRC_DIR = src

# Targets
SIMULATOR = $(BUILD_DIR)/Simulator.exe
GENERATOR = $(BUILD_DIR)/TrafficGenerator.exe

# SDL3 DLL copy definitions
DLL_SRC = SDL-3\bin\SDL3.dll
TTF_DLL_SRC = SDL-3\bin\SDL3_ttf.dll
DLL_DEST = $(BUILD_DIR)

all: $(SIMULATOR) $(GENERATOR) copy_dlls

$(SIMULATOR): $(SRC_DIR)/Simulator.cpp
	$(CC) $(SRC_DIR)/Simulator.cpp -o $@ $(CFLAGS) $(LDFLAGS) $(WINLIBS)

$(GENERATOR): $(SRC_DIR)/TrafficGenerator.cpp
	$(CC) $(SRC_DIR)/TrafficGenerator.cpp -o $@ $(CFLAGS) $(WINLIBS)

copy_dlls:
	copy $(DLL_SRC) $(DLL_DEST)
	copy $(TTF_DLL_SRC) $(DLL_DEST)

clean:
	del $(SIMULATOR) $(GENERATOR)
	del $(DLL_DEST)\SDL3.dll
	del $(DLL_DEST)\SDL3_ttf.dll
	del vehicles.data
	@echo "--- Clean complete! ---"