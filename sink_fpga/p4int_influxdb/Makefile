##
# @author Mario Kuka <kuka@cesnet.cz>
#         Pavlina Patova <xpatov00@stud.fit.vutbr.cz>
# @brief Header file of INT sink node
# 
# Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
##

BIN=p4int
CXX=g++
CXXFLAGS=-Wall -pedantic -std=c++17
INT_FILES=device.cc device.h p4int.cc p4int.h p4_influxdb.cc p4_influxdb.h

DEBUG ?= 0
ifeq ($(DEBUG), 1)
CXXFLAGS +=-O0 -g 
else
CXXFLAGS +=-O2 -O3
endif

LIBS=-lm -lnfb -lp4dev -lInfluxDB

p4int: $(INT_FILES)
	@echo "Using CXXFLAGS = $(CXXFLAGS)"
	$(CXX) -o $(BIN) $(CXXFLAGS) $(INT_FILES) $(LIBS)

clean:
	rm -f *.a *.o $(BIN)

mrproper: clean
	rm $(BIN) 
