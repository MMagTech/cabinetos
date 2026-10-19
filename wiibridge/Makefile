# cabinetos-wii-bridge (#200). The one build command, used by CI
# (.github/workflows/build-frontend.yml) and by hand, in the builder container.
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++20 -Wall -Wextra

cabinetos-wii-bridge: bridge.cpp mii.cpp bridge.h encryption.cpp encryption.h
	$(CXX) $(CXXFLAGS) -o $@ bridge.cpp mii.cpp encryption.cpp

# The pacing test (#480): two Remotes on socket pairs, no hardware.
pacing-test: pacing-test.cpp bridge.cpp mii.cpp bridge.h encryption.cpp encryption.h
	$(CXX) $(CXXFLAGS) -pthread -o $@ pacing-test.cpp mii.cpp encryption.cpp

test: pacing-test
	./pacing-test

clean:
	rm -f cabinetos-wii-bridge pacing-test

.PHONY: clean test
