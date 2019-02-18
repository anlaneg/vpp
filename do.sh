#! /bin/bash

make install-dep
(./build-root/vagrant/build.sh ; cd build-root/ && ./bootstrap.sh && make V=0 PLATFORM=vpp TAG=vpp_debug )
