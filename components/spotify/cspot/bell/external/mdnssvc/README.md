# Introduction
This is a fork from https://bitbucket.org/geekman/tinysvcmdns. It works under
Windows, MacOS, and Linux (x86, x86_64, arm and aarch64), FreeBSD and Solaris.

I've added a function to stop a registered service, send bye-bye packet and 
properly release all allocated memory.

It also uses unicast when cient is asking for it which is essential as more and more routers do IGMP spoofing

I've also added a small real responder

Please see [here](https://github.com/philippe44/cross-compiling/blob/master/README.md#organizing-submodules--packages) to know how to rebuild my apps in general 
