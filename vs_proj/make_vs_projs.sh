#!/bin/sh

mkdir vs2013/x86 vs2013/x64 vs2015/x86 vs2015/x64 -p

rm vs2013/x86/* -rf
cd vs2013/x86
cmake ../../.. -G"Visual Studio 12 2013"
sed -i 's/E:\\github\\libgo/..\\..\\../g' *vcxproj*
sed -i 's/E:\/github\/libgo/..\\..\\../g' *vcxproj*
cd ../..

rm vs2013/x64/* -rf
cd vs2013/x64
cmake ../../.. -G"Visual Studio 12 2013 Win64"
sed -i 's/E:\\github\\libgo/..\\..\\../g' *vcxproj*
sed -i 's/E:\/github\/libgo/..\\..\\../g' *vcxproj*
cd ../..

rm vs2015/x86/* -rf
cd vs2015/x86
cmake ../../.. -G"Visual Studio 14 2015"
sed -i 's/E:\\github\\libgo/..\\..\\../g' *vcxproj*
sed -i 's/E:\/github\/libgo/..\\..\\../g' *vcxproj*
cd ../..

rm vs2015/x64/* -rf
cd vs2015/x64
cmake ../../.. -G"Visual Studio 14 2015 Win64"
sed -i 's/E:\\github\\libgo/..\\..\\../g' *vcxproj*
sed -i 's/E:\/github\/libgo/..\\..\\../g' *vcxproj*
cd ../..
