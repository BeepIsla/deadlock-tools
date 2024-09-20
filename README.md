# Deadlock Tools

Various tools for running a Deadlock server and maybe other useful things.

## Tools

-   Hero dumper for various hero informations
    -   Currently only hero name, hero id, hero image, and availability
-   [Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay) instruction to protect the server from DDOS attacks and route all traffic through Valve relays
-   Match setup with automatic team, lane, and hero assignment, including dumping stats at match end
    -   Stats are written to disk in a JSON format, see [server-match README](./server-match/README.md) for more information
    -   Death replays do not work yet (No idea how to enable these)
