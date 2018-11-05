# xripd
xripd is a *passive only* RIPv2 daemon implemented for Linux in C. It will respond internally to RIPv2 messages received from the network, but will not actively participate in updating its neighbours. 

xripd will honor RIPv2 path selection rules to ensure best paths are installed into the kernel's routing table. 

White and blacklisting of routes is supported.

## Usage
```
root@r1:~/xripd# make clean && make
Cleanup Complete!
Compiled src/rib-ll.c successfully!
Compiled src/rib.c successfully!
Compiled src/rib-null.c successfully!
Compiled src/filter-ll.c successfully!
Compiled src/xripd.c successfully!
Compiled src/route.c successfully!
Linking complete!

root@r1:~/xripd# bin/xripd -h
usage: xripd [-h] [-bw <filename>] -i <interface>
params:
        -i <interface>   Bind RIP daemon to network interface
        -b               Read Blacklist from <filename>
        -w               Read Whielist from <filename>
        -h               Display this help message
filter:
         - filter file may contain zero or more routes to be white/blacklisted from the RIB
         - 1 route per line in file, in the format of x.x.x.x y.y.y.y
```
