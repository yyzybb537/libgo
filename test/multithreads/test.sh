#!/bin/bash

build()
{
    wrk=`pwd`
    cd ../..
    mkdir -p build
    cd build
    cmake .. && make release
    cd $wrk
}

test -f ../../build/liblibgo.a || build

isMac=`uname -a | grep Darwin -c || echo -n`
threads=$1
shift 
test -z "$threads" && threads=1
echo "Test Thread Number:" $threads
echo "------------- compiling ---------------"
if [ "$isMac" -eq "1" ]
then
    g++ -g mthreads.cpp -DTEST_MIN_THREAD=$threads -DNDEBUG -std=c++11 -O3 -o mthreads -I../../third_party/gtest/include -L../../build -llibgo -pthread || exit
    #g++ mthreads.cpp -DTEST_MIN_THREAD=$threads -DNDEBUG -std=c++11 -g -o mthreads -I../../third_party/gtest/include -L../../build -llibgo -pthread || exit
else
    #g++ mthreads.cpp -DTEST_MIN_THREAD=$threads -DNDEBUG -std=c++11 -O3 -o mthreads -I../../third_party/gtest/include -L../../build -llibgo -static \
    #    /usr/local/lib/libtcmalloc_minimal.a /usr/local/lib/libunwind.a \
    #    -Wl,--whole-archive -lstatic_hook -lc -lpthread -Wl,--no-whole-archive -ldl || exit

    g++ -g mthreads.cpp -DTEST_MIN_THREAD=$threads -std=c++11 -O3 -o mthreads -I../../third_party/gtest/include -L../../build -llibgo \
        -pthread -ldl || exit
        #/usr/local/lib/libtcmalloc_minimal.a /usr/local/lib/libprofiler.a /usr/local/lib/libunwind.a \
fi
echo "------------- libgo ---------------"
./mthreads $@
echo "-----------------------------------"
#exit
