#!/bin/bash

wget ftp://gsapubftp-anonymous@ftp.broadinstitute.org/travis/gcc_4.9.1-1_amd64.deb
sudo apt-get remove cpp libffi-dev
sudo dpkg --install gcc_4.9.1-1_amd64.deb

sudo rm /usr/lib/x86_64-linux-gnu/libstdc++.so.6

export LD_LIBRARY_PATH=/usr/lib64
sudo ln -s /usr/lib64/libstd* /usr/lib/x86_64-linux-gnu/
