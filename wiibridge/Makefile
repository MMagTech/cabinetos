# cabinetos-wii-bridge (#200). The one build command, used by CI
# (.github/workflows/build-frontend.yml) and by hand, in the builder container.
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++20 -Wall -Wextra

cabinetos-wii-bridge: bridge.cpp encryption.cpp encryption.h
	$(CXX) $(CXXFLAGS) -o $@ bridge.cpp encryption.cpp

clean:
	rm -f cabinetos-wii-bridge

.PHONY: clean
