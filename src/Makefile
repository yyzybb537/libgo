CC=g++
#CFLAGS=-std=c++11 -g -Wall -fPIC
CFLAGS=-std=c++11 -O3 -Wall -fPIC
STATIC_LIB=libcoroutine.a
DYNAMIC_LIB=libcoroutine.so
INCLUDE=-I. -Ilinux
OBJS=$(patsubst %.cpp, %.o, $(wildcard *.cpp)) $(patsubst %.cpp, %.o, $(wildcard linux/*.cpp))

all : $(STATIC_LIB) $(DYNAMIC_LIB)

$(STATIC_LIB) : $(OBJS)
	@ar cr $@ $^
	@echo "LD $(STATIC_LIB)"

$(DYNAMIC_LIB) : $(OBJS)
	@echo "CC dynamic-link hook module."
	@rm linux_glibc_hook.o -f
	@$(CC) $(CFLAGS) $(INCLUDE) -DCO_DYNAMIC_LINK -c linux_glibc_hook.cpp -o linux_glibc_hook.o
	@$(CC) $(CFLAGS) $(INCLUDE) -shared $^ -o $@
	@echo "LD $(DYNAMIC_LIB)"
	@rm linux_glibc_hook.o -f

.cpp.o:
	@echo "CC $@"
	@$(CC) $(CFLAGS) $(INCLUDE) -c $^ -o $@

.PHONY: clean install

clean:
	rm *.o linux/*.o $(STATIC_LIB) $(DYNAMIC_LIB) -f

install:
	install -m 0664 $(STATIC_LIB) /usr/local/lib
	install -m 0775 $(DYNAMIC_LIB) /usr/local/lib
	mkdir -p /usr/local/include/coroutine
	install -m 0664 *.h /usr/local/include/coroutine
	install -m 0664 linux/*.h /usr/local/include/coroutine
