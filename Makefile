CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic
INCLUDES = -Iinclude

SOURCES = src/main.cpp src/predictor.cpp src/trace.cpp
TEST_SOURCES = tests/tests.cpp src/predictor.cpp src/trace.cpp
HEADERS = include/predictor.h include/trace.h

.PHONY: all test clean

all: branchsim

branchsim: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o branchsim

tests_runner: $(TEST_SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TEST_SOURCES) -o tests_runner

test: tests_runner
	./tests_runner

clean:
	rm -f branchsim tests_runner
