CC=g++
CFLAGS=-std=c++11 -g -Wall -fPIC
STATIC_LIB=libwin_patch.a
INCLUDE=-IG:/usr/boost_1_59_0
OBJS=$(patsubst %.cpp, %.o, $(wildcard *.cpp))

all : $(STATIC_LIB)

$(STATIC_LIB) : $(OBJS)
	@ar cr $@ $^
	@echo "LD $(STATIC_LIB)"

.cpp.o:
	@echo "CC $@"
	@$(CC) $(CFLAGS) $(INCLUDE) -c $^ -o $@

.PHONY: clean

clean:
	rm *.o $(STATIC_LIB) -f
