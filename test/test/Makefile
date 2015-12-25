CC=g++
CFLAGS=-std=c++11 -g -Wall
TARGET=$(patsubst %.cpp, %.t, $(wildcard *.cpp)) pg_cpu.t
OBJS=$(patsubst %, %.o, $(TARGET))
LINK=-L../../src -lboost_coroutine -lboost_thread -lboost_system

all : $(TARGET)

pg_cpu.t: pg_cpu.o
	@$(CC) $(CFLAGS) -pg $^ -o $@ ${LINK}  -llibgo -ldl
	@echo "LD $@"

pg_cpu.o: cpu.cpp
	@$(CC) $(CFLAGS) -pg -c $^ -I../../src -I../../src/linux -o $@
	@echo "MK $@"

s_hiredis.t: hiredis.o
	@$(CC) $(CFLAGS) $^ -o $@ ${LINK} -lhiredis -llibgo -static -static-libgcc -static-libstdc++
	@echo "LD $@"

hiredis.t: hiredis.o
	@$(CC) $(CFLAGS) $^ -o $@ ${LINK} -lhiredis -llibgo -ldl
	@echo "LD $@"

%.t: %.o
	@$(CC) $(CFLAGS) $^ -o $@ ${LINK} -L../../src -llibgo -ldl
	@echo "LD $@"

.cpp.o:
	@$(CC) $(CFLAGS) -c $^ -I../../src -I../../src/linux -o $@
	@echo "MK $@"

.PHONY: clean

clean:
	rm *.o *.t -f
