SHELL=/bin/sh
.SUFFIXES:
.SUFFIXES: .cpp .o
CC=g++
CFLAGS=-c -g -Wall -std=c++11
LDFLAGS=-lz -lpthread
SOURCES=main.cpp radiomanager.cpp crc32.cpp
HEADERS=radiomanager.hpp crc32.h radiodata.hpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=emulator

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) -Wall -std=c++11 $(OBJECTS) $(LDFLAGS) -o $@ 

main.o: main.cpp radiomanager.hpp radiodata.hpp
	$(CC) $(CFLAGS) main.cpp

radiomanager.o: radiomanager.cpp radiomanager.hpp
	$(CC) $(CFLAGS) radiomanager.cpp

crc32.o: crc32.cpp crc32.h
	$(CC) $(CFLAGS) crc32.cpp


#.cpp.o:
#	$(CC) $(CFLAGS) $< -o $@

clean:
	rm *o emulator
