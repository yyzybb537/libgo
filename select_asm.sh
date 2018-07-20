#!/bin/sh

cd $1
work=`pwd`

cd $work/third_party/boost.context
./bootstrap.sh > /dev/null
objs=`./b2 -n --with-context link=static | grep "compile.asm" | awk '{print $2}'`
for obj in $objs
do
    obj_name=`basename $obj`
    source=`echo $obj_name | cut -d\. -f1`.S
    source=$work/third_party/boost.context/libs/context/src/asm/$source
    cp $source $work/libgo/context
done
