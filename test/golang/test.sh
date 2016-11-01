#!/bin/sh

echo "------------- libgo ---------------"
g++ libgo_test.cpp -std=c++11 -O3 -o libgo_test -llibgo -lboost_context -lboost_thread -pthread -static && ./libgo_test
echo "-----------------------------------"

echo "------------- golang --------------"
go test golang_test.go -test.bench=".*"
echo "-----------------------------------"
