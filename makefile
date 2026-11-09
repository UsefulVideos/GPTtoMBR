.RECIPEPREFIX := >

# Define the Windows cross-compiler
CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -static -static-libgcc -static-libstdc++
LDLIBS = -ladvapi32

TARGET = GptToMbr.exe
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
>@echo "[*] Building fully static Windows executable..."
>$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SRC) -o $(TARGET) $(LDLIBS)
>@echo "[+] Build complete: $(TARGET)"

run:
>@if command -v wine >/dev/null 2>&1; then echo "[INFO] Wine detected. Refusing to run the Windows executable."; else echo "[INFO] Refusing to run Windows executable on Linux."; fi

clean:
>@echo "[*] Cleaning up..."
>rm -f $(TARGET)

.PHONY: all run clean