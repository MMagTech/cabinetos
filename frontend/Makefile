# The CabinetOS frontend.
#
# Built inside the container in Containerfile, never on the host: the OS image
# carries no compiler by design, and the development machine is a Mac.
# See README.md for the loop.

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS  ?=

PKGS     := sdl3 egl glesv2
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS     := $(shell pkg-config --libs $(PKGS))

SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:.cpp=.o)
BIN      := build/cabinetos-frontend

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p build
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)
	@echo "built $@"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ)
	rm -rf build

.PHONY: all clean
