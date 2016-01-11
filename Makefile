SHELL=/bin/sh
.SUFFIXES:
.SUFFIXES: .cpp .o
CC=g++
CFLAGS=-c -Wall -std=c++11
LDFLAGS=-lz
SOURCES=main.cpp radiomanager.cpp crc32.cpp
HEADERS=radiomanager.h crc32.h
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=emulator

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) -Wall -std=c++11 $(OBJECTS) $(LDFLAGS) -o $@ 

main.o: main.cpp
	$(CC) $(CFLAGS) main.cpp

radiomanager.o: radiomanager.cpp radiomanager.h
	$(CC) $(CFLAGS) radiomanager.cpp

crc32.o: crc32.cpp crc32.h
	$(CC) $(CFLAGS) crc32.cpp


#.cpp.o:
#	$(CC) $(CFLAGS) $< -o $@

clean:
	rm *o emulator
