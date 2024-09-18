# Deadlock Tools

Various tools for running a Deadlock server and maybe other useful things.

## Tools

-   Hero dumper for various hero informations
    -   Currently only hero name, hero id, hero image, and availability
-   [Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay) instruction to protect the server from DDOS attacks and route all traffic through Valve relays
-   Match setup with automatic team and hero assignment, including dumping stats at match end
    -   Stats are written to disk in a raw protobuf format, we'll work on making them readable in the future
    -   I am still trying to figure out how lane assignments work
    -   I haven't quite figured out how to enable death replays
