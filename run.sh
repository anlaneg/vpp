#! /bin/bash
cat << EOF > ./startup.conf

unix {
  nodaemon
  log /var/log/vpp/vpp.log
  full-coredump
  #interactive cli-listen localhost:5002
  cli-listen localhost:5002
}

dpdk {
    uio-driver igb_uio #这里只是说vpp使用igb_uio作为网卡的驱动
    dev 0000:00:09.0 
    dev 0000:00:0a.0
    #vdev eth_bond0,mode=2,slave=0000:02:06.0,slave=0000:02:07.0,xmit_policy=l34 #这个设置是将2个物理网卡绑定成一个使用
}

api-trace {
  on
}

EOF
modprobe uio
insmod ../anlaneg_dpdk/x86_64-native-linuxapp-gcc/build/kernel/linux/igb_uio/igb_uio.ko
ifconfig eth1 down
ifconfig eth2 down
sudo gdb --args ./build-root/build-vpp_debug-native/vpp/bin/vpp -c ./startup.conf
