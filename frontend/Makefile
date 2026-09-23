# The CabinetOS frontend.
#
# Built inside the container in Containerfile, never on the host: the OS image
# carries no compiler by design, and the development machine is a Mac.
# See README.md for the loop.

CXX      ?= g++
# -MMD -MP writes a .d file per object listing the headers it included, so
# editing a header rebuilds everything that uses it. Without this, changing a
# signature in ui.h left stale objects linking against the old one, which
# surfaces as an undefined reference rather than as a compile error.
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS  ?=

PKGS     := sdl3 egl glesv2 freetype2 libjpeg libpng libcurl json-c libarchive libsystemd
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS     := $(shell pkg-config --libs $(PKGS)) -lpthread -ldl

SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:.cpp=.o)
DEP      := $(OBJ:.o=.d)
BIN      := build/cabinetos-frontend

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p build
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)
	@echo "built $@"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP)
	rm -rf build

-include $(DEP)

.PHONY: all clean
