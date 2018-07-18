#!/bin/sh

echo "------------- libgo ---------------"
#g++ libgo_test.cpp -std=c++11 -O3 -o libgo_test -L../../build -llibgo -lboost_context -lboost_thread -pthread -static -static-libgcc -static-libstdc++ && ./libgo_test
g++ libgo_test.cpp -std=c++11 -O3 -o libgo_test -L../../build -Wl,-rpath ../../build -llibgo -lboost_context -lboost_thread -pthread && ./libgo_test
echo "-----------------------------------"

echo "------------- golang --------------"
go version
go test golang_test.go -test.bench=".*"
echo "-----------------------------------"
