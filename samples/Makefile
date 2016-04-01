CC=g++
CFLAGS=-std=c++11 -g -Wall
INCLUDE=-I../libgo -I../libgo/linux
LINK=-L.. -llibgo_main -llibgo -ldl -lboost_thread -lboost_system -lpthread
STATIC=-static -static-libgcc -static-libstdc++
TARGET=$(patsubst %.cpp, %.t, $(wildcard *.cpp))
#$(patsubst %.cpp, s_%.t, $(wildcard *.cpp))

all : $(TARGET)

#s_%.t: %.o
#	@echo "LD $@"
#	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK) $(STATIC)

sample9_curl.t: sample9_curl.o
	@echo "LD $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK) -lcurl

sample12_hiredis.t: sample12_hiredis.o
	@echo "LD $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK) -lhiredis

%.t: %.o
	@echo "LD $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(INCLUDE) $(LINK)

.cpp.o:
	@echo "CC $@"
	@$(CC) $(CFLAGS) -c $^ -o $@ $(INCLUDE)

.PHONY: clean

clean:
	rm *.o $(TARGET) -f
