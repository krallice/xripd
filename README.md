# xripd
xripd is a RIPv2 daemon implemented in Linux in C. Originally built as a *passive only* implementation, it has now been upgraded to *active* capability - meaning it can now participate in RIPv2 topologies. Interoperability has been tested with Cisco's IOS 'router rip' and the classical linux zebra/ripd daemon.

As a bonus feature, xripd supports the black and whitelisting of inbound/outbound routes.

## Usage:
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

## Why:

I wanted to build something moderately useful that I can run within my home network, that would also allow me to explore the Linux ABI/API, specifically regarding:

+ Socket and Network programming (Async I/O, AF_INET Sockets, the NETLINK API)
+ Inter Process Communication (Anon Pipes, Abstract Unix Domain Sockets)
+ Multiprocessing (fork()ing, POSIX threads, mutexes)
+ Abstractions in C

The code itself is *stupidly over-engineered* as I wanted to pile in as many reasons to play with different things - A readable and performant RIP implementation is not really the point. I'm happy it works at all haha.

I also wanted to build something where I could play with abstractions in C, and have a real-life requirement to implement several datastructures. A RIB is a good chance to play with good, and not so good, datastructures.

## Structure:

The original v1 *passive only* structure of xripd looked like:

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

The current v2 **active** structure of xripd looks like:
```
                                            sendto()
    RIP^2 UPDATE MSG    +---------------------------------------+
       <------------+   |      rip_msg_entry_t                  |    rib_ctl_hdr_t
                    |   |       recv_from()                     |           +  '\0xripd-daemon'
RIPv2 UPDATE MSG +--+---v--------+   +    +--------------+------+-----+     |  +--------+
network +--------> AF_INET SOCKET+--------> xripd daemon | daemon pthread<-----> AF_UNIX|   ^
                 +---------------+   |    +---+--+-------+------------+     |  +-+----^-+   |
                                     |        |  | <-mutex_request_flag->   |    |    |     |
                                     +        |  |                          |    |    |     +
                     +-----+      write()     |  | rib_entry_t              |    |    |  rib_ctl
                     |     <---------+-----------+                          |    |    |  messaging
                     | a p |         |        |                             |    |    |     +
                     | n i |         |        | fork()                      |    |    |     |
                     | o p |         +        |                      rib_ctl_hdr_t    |     |
                     | n e |      read()  +---v----------+------------+     |  +-v----+-+   |
                     |     +---------+---->  xripd rib   | rib pthread<--------> AF_UNIX|   v
                     +-----+         |    +---+----------+------------+     |  +--------+
                                     |        | <- mutex_rib_lock ->        | '\0xripd-rib'
                                     +        |                             |
  ROUTING        +---------------+  sendmsg() |  NLM_F_REQUEST              | Abstract Unix
   TABLE <-------+ NETLINK SOCKET<---+--------+                             | Domain Sockets
                 +---------------+   |                                      |  (Datagrams)
                                     +                                      +
                     kernel space                   user space                kernel space
```
The application is split into 2 seperate processes that are responsible for barely holding the whole thing together:

+ **xripd-daemon** - Responsible for inbound/outbound communication on the network between other RIP routers.
+ **xripd-rib** - Resonsible for maintaining our RIB in memory, and manipulating the kernel's route table.

There are two IPC channels between the two processes used for transferring data internally:

#### Anonymous Pipe
As a RIPv2 RESPONSE message is recieved by the daemon by another router, it unpacks the one-or-many rip_msg_entry_t's (aka routes) contained in the UDP datagram, converts these into our internal datastructure rib_entry_t, and sends them to the rib via an anonymouse pipe.

A pipe provides a method of transferring a stream of bytes through the kernel between two processes. As the rib_entry_t struct is fixed in size, both processes read/write from the pipe in units of rib_entry_t's fixed byte size. This is a rudimentary way of message passing through a stream interface. Cool.

#### AF_UNIX DGRAMs (Control Plane)
Ok so this one's a little more fun. I decided to play with Abstract Unix Domain Sockets to:
+ Extract routes back from out of our rib to the daemon, AND
+ Function as the **control plane** between both processes.

AF_UNIX DGRAMs allow multiple processes to send datagram's through the kernel to each other. Inside the datagram, I defined a very rudimentary messaging protocol called rib_ctl used to exchange control and data messages between the frontend and backend:

For instance, if the daemon recieves a RIPv2 REQUEST message, it will send a rib_ctl RIB_CTL_HDR_REQUEST message to the daemon. In response to this type of message, the rib will: 
+ Reply with zero or more datagrams packed with a RIB_CTL_HDR_REPLY header followed by a rib_entry_t route,
+ Finish the *stream* with a RIB_CTL_HDR_ENDREPLY.

This will inform the daemon it can begin processing the data it has recieved. The basic *(and it really is, it's only 2 bytes ..)* rib_ctl header provides a sort of *stream* capability out of a datagram format. Essentially the opposite of the Pipe example. Pretty cool, never done that before.

#### Mutexes and POSIX Threading
I decided to spawn seperate threads in both the rib and daemon processes to handle the rib_ctl messaging. Muxtex locking therefore becomes required to ensure *consistency of data* as this throws order of execution prediction out the window. Manipulations of the RIB are protected by a blocking mutex to ensure inbound/outbound RIP messaging is consistent and nothing catches fire. I've never played with these before but they were fun, if not completely unrequired haha.

## RIB datastore:

One of the other fun parts of this was abstracting away the implementation of the RIB to a series of function pointers:
```
    7         // Function pointers for underlying datastore implementations:
    8         int (*add_to_rib)(int*, const rib_entry_t*, rib_entry_t*, rib_entry_t*, int*);
    9         int (*invalidate_expired_local_routes)(); // Metric = 16 for old local routes that are no longer in the kernel table
   10         int (*remove_expired_entries)(const rip_timers_t*, int*);
   11         int (*dump_rib)();
   12         int (*serialise_rib)(char *buf, const uint32_t *count);
   13         void (*destroy_rib)();
   14 
```
This *interface* is then alligned with an underlying implementation at compile time:
```
         } else if ( rib_datastore == XRIPD_RIB_DATASTORE_LINKEDLIST ) {
 12 
 11                 xripd_rib->add_to_rib = &rib_ll_add_to_rib;
 10                 xripd_rib->dump_rib = &rib_ll_dump_rib;
  9                 xripd_rib->remove_expired_entries = &rib_ll_remove_expired_entries;
  8                 xripd_rib->invalidate_expired_local_routes = &rib_ll_invalidate_expired_local_routes;
  7                 xripd_rib->serialise_rib = &rib_ll_serialise_rib;
  6                 xripd_rib->destroy_rib = &rib_ll_destroy_rib;
```
This has provided some room to play with different RIB implementations. To date, only one has been implemented:

+ An Unsorted Singularly Linked List (horribly inefficient but functioning..)

Future planned datastructures include:

+ Some sort of B-Tree implementation (for overkill of complexity)
+ Some sort of hash of linked lists (probably the sweet spot of performance and complexity)

## Future:

Things that might be good to play with in the future:

+ Support more than one network interface
+ Support more of the RIPv2 Spec (Not all optional features outlined in the RFC are implemented. REQUEST message handling is not completely RFC compliant, but good enough to work in my labs)
+ Rebuild with a sane design to actually solve the domain of rip and not just muck aroud wasting CPU cycles haha.

## License:

Licensed under the GPLv2 as per included LICENSE file.
