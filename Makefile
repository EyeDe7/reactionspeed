SERVER_URL   ?= https://reaction.mineralgaming.com
INVITE_CODE  ?= NIGGERS-2026

DEFINES      := -DDEFAULT_SERVER_URL="\"$(SERVER_URL)\"" -DDEFAULT_INVITE_CODE="\"$(INVITE_CODE)\""

CXX          := g++
CXXFLAGS     := -O3 -std=c++20 -Wall -Wextra -pedantic -static-libstdc++ -static-libgcc $(DEFINES)
LDFLAGS      := -lX11 -lcrypto -pthread

MINGW_CXX    := $(shell command -v x86_64-w64-mingw32-g++-win32 || command -v x86_64-w64-mingw32-g++)
MINGW_STRIP  := $(shell command -v x86_64-w64-mingw32-strip || echo strip)
WIN_CXXFLAGS := -O3 -std=c++20 -Wall -Wextra -pedantic -static -static-libgcc -static-libstdc++ -mwindows $(DEFINES)
WIN_LDFLAGS  := -lgdi32 -luser32 -lwinhttp -lwinmm -mwindows

TARGET_LINUX := reaction_benchmark
TARGET_WIN   := reaction_benchmark.exe
SRC          := reaction_benchmark.cpp
HEADERS      := embedded_font.hpp crypto.hpp net_client.hpp canvas.hpp history.hpp ui_renderer.hpp platform_window.hpp app.hpp

.PHONY: all linux windows hashes clean run history

all: linux

linux: $(TARGET_LINUX)

$(TARGET_LINUX): $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET_LINUX) $(LDFLAGS)
	strip $(TARGET_LINUX)
	cp -f $(TARGET_LINUX) reaction_benchmark_linux_x86_64
	mkdir -p server/binaries
	cp -f reaction_benchmark_linux_x86_64 server/binaries/reaction_benchmark_linux_x86_64

windows: $(SRC) $(HEADERS)
	@if [ -z "$(MINGW_CXX)" ]; then \
		echo "Install mingw-w64 (sudo apt install g++-mingw-w64-x86-64) to rebuild $(TARGET_WIN)"; \
		exit 1; \
	fi
	$(MINGW_CXX) $(WIN_CXXFLAGS) $(SRC) -o $(TARGET_WIN) $(WIN_LDFLAGS)
	$(MINGW_STRIP) $(TARGET_WIN)
	mkdir -p server/binaries
	cp -f $(TARGET_WIN) server/binaries/$(TARGET_WIN)

hashes:
	@python3 -c 'import hashlib, json, os; \
	d = {}; \
	os.path.exists("reaction_benchmark_linux_x86_64") and d.update({"linux_x86_64": hashlib.sha256(open("reaction_benchmark_linux_x86_64","rb").read()).hexdigest()}); \
	os.path.exists("reaction_benchmark.exe") and d.update({"windows_x86_64_exe": hashlib.sha256(open("reaction_benchmark.exe","rb").read()).hexdigest()}); \
	open("server/allowed_binaries.json","w").write(json.dumps(d, indent=2) + "\n"); \
	print("Updated server/allowed_binaries.json:\n" + json.dumps(d, indent=2))'

run: $(TARGET_LINUX)
	./$(TARGET_LINUX)

history: $(TARGET_LINUX)
	./$(TARGET_LINUX) --history

clean:
	rm -f $(TARGET_LINUX) reaction_benchmark_linux_x86_64 $(TARGET_WIN)
