#!/bin/sh

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
test -z "$threads" && threads=1
echo "Test Thread Number:" $threads
echo "------------- compiling ---------------"
if [ "$isMac" -eq "1" ]
then
    g++ libgo_test.cpp -DTEST_MIN_THREAD=$threads -DNDEBUG -std=c++11 -O3 -o libgo_test -I../../third_party/gtest/include -L../../build -llibgo -pthread || exit
    #g++ libgo_test.cpp -DTEST_MIN_THREAD=$threads -DNDEBUG -std=c++11 -g -o libgo_test -I../../third_party/gtest/include -L../../build -llibgo -pthread || exit
else
    g++ libgo_test.cpp -DTEST_MIN_THREAD=$threads -DNDEBUG -std=c++11 -O3 -o libgo_test -I../../third_party/gtest/include -L../../build -llibgo -static \
        -Wl,--whole-archive -lstatic_hook -lc -lpthread -Wl,--no-whole-archive -ldl 2>/dev/null || exit
fi
echo "------------- libgo ---------------"
./libgo_test
echo "-----------------------------------"

echo "------------- golang --------------"
go version
go run golang.go $threads
echo "-----------------------------------"
