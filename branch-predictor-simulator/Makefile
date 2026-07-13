CXX ?= c++
CPPFLAGS += -Iinclude
CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow

BUILD_DIR := build/make
CORE_SOURCES := src/predictors.cpp src/simulator.cpp src/statistics.cpp src/trace_parser.cpp
CORE_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CORE_SOURCES))
MAIN_OBJECT := $(BUILD_DIR)/src/main.o
TEST_NAMES := predictors trace_parser simulator cli
TEST_OBJECTS := $(addprefix $(BUILD_DIR)/tests/test_,$(addsuffix .o,$(TEST_NAMES)))
TEST_BINARIES := $(addprefix $(BUILD_DIR)/test_,$(TEST_NAMES))
DEPENDENCIES := $(CORE_OBJECTS:.o=.d) $(MAIN_OBJECT:.o=.d) $(TEST_OBJECTS:.o=.d)

.PHONY: all test clean

all: branchsim

branchsim: $(CORE_OBJECTS) $(MAIN_OBJECT)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/test_%: $(BUILD_DIR)/tests/test_%.o $(CORE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: branchsim $(TEST_BINARIES)
	$(BUILD_DIR)/test_predictors
	$(BUILD_DIR)/test_trace_parser
	$(BUILD_DIR)/test_simulator
	$(BUILD_DIR)/test_cli ./branchsim .

clean:
	rm -rf $(BUILD_DIR) branchsim

-include $(DEPENDENCIES)
