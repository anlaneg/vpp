#! /bin/bash
cat << EOF > ./startup.conf

unix {
  nodaemon
  log /var/log/vpp/vpp.log
  full-coredump
  cli-listen localhost:5002
}

api-trace {
  on
}

EOF

sudo gdb --args ./build-root/build-vpp_debug-native/vpp/bin/vpp -c ./startup.conf
