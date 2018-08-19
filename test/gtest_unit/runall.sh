#!/bin/sh

make $@
ulimit -c 500000
errinfo=''
for t in `ls *.t`
do
    echo "------------ run $t --------------"
    ./$t || errinfo="$errinfo $t"
    echo "----------------------------------"
done

if [ "$errinfo" = "" ]
then
    echo "All OK"
else
    echo "Error: $errinfo"
    exit 1
fi
