#!/bin/sh

mkdir vs2017/x64 vs2015/x86 vs2015/x64 -p

PATH=$PATH:/c/Program\ Files/CMake/bin/

rm vs2017/x64/* -rf
cd vs2017/x64
cmake.exe ../../.. -G"Visual Studio 15 2017 Win64" $@
cd ../..

#rm vs2015/x86/* -rf
#cd vs2015/x86
#cmake ../../.. -G"Visual Studio 14 2015" $@
#cd ../..
#
#rm vs2015/x64/* -rf
#cd vs2015/x64
#cmake ../../.. -G"Visual Studio 14 2015 Win64" $@
#cd ../..
