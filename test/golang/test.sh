#!/bin/sh

echo "------------- libgo ---------------"
g++ libgo_test.cpp -std=c++11 -O3 -llibgo -o libgo_test && ./libgo_test
echo "-----------------------------------"

echo "------------- golang --------------"
go test -test.bench=".*"
echo "-----------------------------------"
