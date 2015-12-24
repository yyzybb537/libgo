CC=g++
CFLAGS=-std=c++11 -O3 -Wall
INCLUDE=-I../../third_party/gtest/include -I../../src -I../../src/linux
LINK=-L../../third_party/gtest/build -L../../build -llibgo -ldl -lgtest_main -lgtest -lpthread \
	 -lboost_coroutine -lboost_thread -lboost_system -lpthread -fuse-ld=gold 
STATIC=-static -static-libgcc -static-libstdc++
TARGET=$(patsubst %.cpp, %.t, $(wildcard *.cpp)) $(patsubst %.cpp, s_%.t, $(wildcard *.cpp))

all : ../../third_party/gtest/build/libgtest.a $(TARGET)

../../third_party/gtest/build/libgtest.a:
	cd ../../third_party/gtest/ && mkdir -p build && cd build && cmake .. && make -j4

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
