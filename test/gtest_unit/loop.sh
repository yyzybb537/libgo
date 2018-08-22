#!/bin/bash

for ((i=0;i<100000;i+=1))
do
    $@ || exit 1;
done
exit 0
