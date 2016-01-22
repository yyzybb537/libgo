#!/bin/bash

# Note that this only works if the tests were built using --coverage for
# compile and link flags!
if [ "$CXX" == "g++" ];
then
	sudo pip install cpp-coveralls
	cd build
	coveralls -r ../ -e CMakeFiles -e test -e example --gcov-options '\-lp'
fi
