CC=g++
CFLAGS=-std=c++11 -O3 -Wall
INCLUDE=-I../../../src -I../../../src/linux -Igtest/include
LINK=-L../../../build -llibgo -ldl -lpthread -L../.. -lboost_thread -lboost_system -lpthread
STATIC=-static -static-libgcc -static-libstdc++
TARGET=$(patsubst %.cpp, s_%.t, $(wildcard *.cpp))
#$(patsubst %.cpp, %.t, $(wildcard *.cpp))

all : $(TARGET)

s_%.t: %.o
	@echo "LD $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK) $(STATIC)

%.t: %.o
	@echo "LD $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK)

.cpp.o:
	@echo "CC $@"
	@$(CC) $(CFLAGS) -c $^ -o $@ $(INCLUDE)

.PHONY: clean

clean:
	rm *.o $(TARGET) -f
