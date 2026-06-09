# Host unit tests for the pure-logic parts of the quiet_cool component.
# Usage: make -C tests run

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1 -g
BUILD := build

SRCS := \
	../components/quiet_cool/fan/protocol.cpp \
	test_protocol.cpp \
	test_mapping.cpp \
	test_cc1101_math.cpp

OBJS := $(BUILD)/protocol.o $(BUILD)/test_protocol.o $(BUILD)/test_mapping.o \
	$(BUILD)/test_cc1101_math.o $(BUILD)/catch_amalgamated.o

all: $(BUILD)/unit_tests

run: $(BUILD)/unit_tests
	$(BUILD)/unit_tests

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/protocol.o: ../components/quiet_cool/fan/protocol.cpp ../components/quiet_cool/fan/protocol.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/catch_amalgamated.o: catch_amalgamated.cpp catch_amalgamated.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

$(BUILD)/test_%.o: test_%.cpp ../components/quiet_cool/fan/protocol.h catch_amalgamated.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/unit_tests: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
