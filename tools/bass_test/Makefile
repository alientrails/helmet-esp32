# Host test for the bass detector. Builds the firmware's own src/bass_dsp.cpp.
#
#   make && ./bass_test

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

bass_test: bass_test.cpp $(ROOT)/src/bass_dsp.cpp $(ROOT)/include/bass_dsp.h
	$(CXX) -O2 -Wall -Wextra -std=c++17 -I$(ROOT)/include -o $@ bass_test.cpp $(ROOT)/src/bass_dsp.cpp -lm

clean:
	rm -f bass_test

.PHONY: clean
