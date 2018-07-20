#!/bin/sh

echo "------------- libgo ---------------"
g++ libgo_test.cpp -std=c++11 -O3 -o libgo_test -L../../build -llibgo -static -pthread -Wl,--whole-archive -lpthread -Wl,--no-whole-archive && ./libgo_test
echo "-----------------------------------"

echo "------------- golang --------------"
go version
go test golang_test.go -test.bench=".*"
echo "-----------------------------------"
