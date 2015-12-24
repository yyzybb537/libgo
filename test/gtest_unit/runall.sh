#!/bin/sh

make $@
ulimit -c 500000
echo '' > result
for t in `ls *.t`
do
    echo ''
    echo ------------ run $t -------------- | tee -a result
    ./$t | tee -a result
    echo ---------------------------------- | tee -a result
done

echo ------------ result --------------
cat result | egrep \(PASSED\|FAIL\|run\)
echo ----------------------------------
