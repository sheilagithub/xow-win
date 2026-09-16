# xow-win: Windows (MinGW-w64) build of xow with a ViGEm backend
# Build with: mingw32-make            (release -> build/xow-win.exe)
#             mingw32-make debug      (verbose -> build/xow-win-debug.exe)

VERSION := 0.1.0

CXX := g++
INCLUDES := -I. -Ithird_party/libusb/include -Ithird_party/ViGEmClient/include
DEFINES := -DVERSION=\"$(VERSION)\" -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX
FLAGS := -Wall -std=c++17 -MMD -MP $(INCLUDES) $(DEFINES)

# The radio/GIP structs are packed bit-fields written for GCC's Linux layout.
# MinGW defaults to the MS layout, which pads a 16-bit group in a uint32_t to
# 4 bytes and shifts every 802.11 header field. Use the GCC layout for our code;
# ViGEmClient.cpp keeps the MS layout because it talks to Win32.
BITFIELDS := -mno-ms-bitfields
build/rel/third_party/%.o: BITFIELDS :=
build/dbg/third_party/%.o: BITFIELDS :=
RELEASE_FLAGS := -O2
DEBUG_FLAGS := -Og -g -DDEBUG

# Fully static so the exe runs without any MinGW DLLs next to it.
# -mwindows: no console window of its own (main.cpp attaches/allocates one
# when run interactively; --background logs to a file).
LDFLAGS := -static -static-libgcc -static-libstdc++ -mwindows
LDLIBS := third_party/libusb/libusb-1.0.a -lsetupapi -lcfgmgr32 -lole32 -lwinpthread

SOURCES := main.cpp \
	dongle/usb.cpp dongle/mt76.cpp dongle/dongle.cpp \
	controller/gip.cpp controller/input.cpp controller/controller.cpp controller/mapping.cpp \
	utils/log.cpp utils/recover.cpp \
	third_party/ViGEmClient/src/ViGEmClient.cpp

REL_OBJECTS := $(patsubst %.cpp,build/rel/%.o,$(SOURCES))
DBG_OBJECTS := $(patsubst %.cpp,build/dbg/%.o,$(SOURCES))

.PHONY: all debug clean
all: build/xow-win.exe
debug: build/xow-win-debug.exe

build/xow-win.exe: $(REL_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/xow-win-debug.exe: $(DBG_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/rel/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(BITFIELDS) $(RELEASE_FLAGS) -c -o $@ $<

build/dbg/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(BITFIELDS) $(DEBUG_FLAGS) -c -o $@ $<

clean:
	rm -rf build/rel build/dbg build/*.exe

-include $(REL_OBJECTS:.o=.d) $(DBG_OBJECTS:.o=.d)
