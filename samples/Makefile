CC=g++
CFLAGS=-std=c++11 -g -Wall
INCLUDE=-I../src -I../src/linux
LINK=-L.. -llibgo_main -llibgo -ldl -lboost_thread -lboost_system -lpthread
STATIC=-static -static-libgcc -static-libstdc++
TARGET=$(patsubst %.cpp, %.t, $(wildcard *.cpp))
#$(patsubst %.cpp, s_%.t, $(wildcard *.cpp))

all : $(TARGET)

#s_%.t: %.o
#	@echo "LD $@"
#	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK) $(STATIC)

sample9.t: sample9.o
	@echo "LD $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK) -lcurl

%.t: %.o
	@echo "LD $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK)

.cpp.o:
	@echo "CC $@"
	@$(CC) $(CFLAGS) -c $^ -o $@ $(INCLUDE)

.PHONY: clean

clean:
	rm *.o $(TARGET) -f
