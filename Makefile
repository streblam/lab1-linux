CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
SOURCES = src/main.cpp src/config.cpp src/worker.cpp src/supervisor.cpp

.PHONY: all clean test debug
all: oslab
oslab: $(SOURCES) src/common.hpp
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@
test: oslab
	python3 tests/integration.py
debug:
	$(CXX) -std=c++17 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined -fno-omit-frame-pointer $(SOURCES) -o oslab-ubsan
clean:
	rm -f oslab oslab-ubsan
