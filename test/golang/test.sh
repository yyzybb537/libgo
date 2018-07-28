#!/bin/sh

echo "------------- libgo ---------------"
isMac=`uname -a | grep Darwin -c || echo -n`
if [ "$isMac" -eq "1" ]
then
    g++ libgo_test.cpp -std=c++11 -O3 -o libgo_test -I../../third_party/gtest/include -L../../build -llibgo -pthread && ./libgo_test
else
    g++ libgo_test.cpp -std=c++11 -O3 -o libgo_test -I../../third_party/gtest/include -L../../build -llibgo -static -pthread -Wl,--whole-archive -lpthread -Wl,--no-whole-archive && ./libgo_test
fi
echo "-----------------------------------"

echo "------------- golang --------------"
go version
go test golang_test.go -test.bench=".*"
echo "-----------------------------------"
