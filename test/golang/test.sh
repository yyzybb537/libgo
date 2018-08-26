#!/bin/sh

echo "------------- libgo ---------------"
isMac=`uname -a | grep Darwin -c || echo -n`
if [ "$isMac" -eq "1" ]
then
    g++ libgo_test.cpp -DTEST_MIN_THREAD=$@ -DNDEBUG -std=c++11 -O3 -o libgo_test -I../../third_party/gtest/include -L../../build -llibgo -pthread && ./libgo_test
    #g++ libgo_test.cpp -DTEST_MIN_THREAD=$@ -DNDEBUG -std=c++11 -g -o libgo_test -I../../third_party/gtest/include -L../../build -llibgo -pthread && ./libgo_test
else
    g++ libgo_test.cpp -DTEST_MIN_THREAD=$@ -DNDEBUG -std=c++11 -O3 -o libgo_test -I../../third_party/gtest/include -L../../build -llibgo -static \
        -Wl,--whole-archive -lstatic_hook -lc -lpthread -Wl,--no-whole-archive -ldl 2>/dev/null && ./libgo_test
fi
echo "-----------------------------------"

echo "------------- golang --------------"
go version
go test golang_test.go -test.bench=".*"
echo "-----------------------------------"
