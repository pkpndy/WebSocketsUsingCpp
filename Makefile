CC = g++
CFLAGS = -std=c++11 -Wall -c -g -fsanitize=address
LDFLAGS = -lstdc++ -fsanitize=address
SOURCES = main.cpp websocket.cpp base64.cpp
OBJECTS = $(SOURCES:.cpp=.o)
EXECUTABLE = wsServer

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS) -lssl -lcrypto

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf *o $(EXECUTABLE)
