#!/bin/bash

cd $1
work=`pwd`

cd $work/third_party/boost.context
test -f b2 || ./bootstrap.sh > /dev/null
obj=`./b2 -an --with-context link=static | grep "compile.asm" | awk '{print $2}' | grep $2`
obj_name=`basename $obj`
source=`echo $obj_name | cut -d\. -f1`.S
file=$work/third_party/boost.context/libs/context/src/asm/$source
cp $file $work/libgo/context
sed -i "s/${2}_fcontext/libgo_${2}_fcontext/g" $work/libgo/context/$source
echo -n $work/libgo/context/$source
