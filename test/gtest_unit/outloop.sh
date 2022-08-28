#!/bin/bash

> loop.log

for ((i=0;i<100000;i+=1))
do
    $@ >> loop.log || exit 1;
done

date >> loop.log
