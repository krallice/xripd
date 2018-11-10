# xripd
xripd is a *passive only* RIPv2 daemon implemented for Linux in C. It will respond internally to RIPv2 messages received from the network, but will not actively participate in updating its neighbours. 

xripd will honor RIPv2 path selection rules to ensure best paths are installed into the kernel's routing table. 

White and blacklisting of routes is supported.

## Usage
```
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

## Why?
I wanted to implement something relatively simple from a protocol perspective to muck around with the Linux API:

+ Socket and Network programming (Async I/O, AF_INET sockets, the NETLINK API)
+ IPC (anon pipes)
+ Process spawning (forking)

From this perspective, the code is over engineered and could be easily executed within a single process, but where's the fun in that?

I also wanted to build something moderately useful where I could muck around and play with different datastructures. In this case, the datastructures backing the main Routing Information Base (RIB) are backed by function pointers interfacing to an implementation (C's answer to OOP interfaces/abstraction): 

```
	// Function pointers for underlying datastore implementations:
	int (*add_to_rib)(int*, const rib_entry_t*, rib_entry_t*, rib_entry_t*);
	int (*remove_expired_entries)();
	int (*invalidate_expired_local_routes)(); 
	int (*dump_rib)();
	void (*destroy_rib)();
```
## Structure:

```
                               rip_msg_entry_t
                                recv_from()
RIPv2 UPDATE MSG +---------------+   |    +--------------+
network +--------> AF_INET SOCKET+--------> xripd daemon |
                 +---------------+   |    +---+--+-------+
                                     |        |  |
                                     |        |  |
                     +-----+      write()     |  | rib_entry_t
                     |     <---------+-----------+
                     | a p |         |        |
                     | n i |         |        | fork()
                     | o p |         |        |
                     | n e |      read()  +---v----------+
                     |     +---------+---->  xripd rib   |
                     +-----+         |    +---+----------+
                                     |        |
                                     |        |
  ROUTING        +---------------+  sendmsg() |  NLM_F_REQUEST
   TABLE <-------+ NETLINK SOCKET<---+--------+
                 +---------------+   |
                                     +
                     kernel space        user space
```



## Improvements?
+ Support more than one network interface
+ Encapsulate logic into a state table design. Clean it up
+ Replace the child/fork model with pthreading
+ Replace the use of pipes with mutexed shared memory
