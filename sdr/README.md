# Steam Datagram Relay

SDR is Valve's virtual private gaming network, it is used to protect servers from DDOS attacks by hiding IPs and routing all traffic through Valves relays. To use it simply follow these steps:

1. Open the following file in your favourite editor: `Project8Staging/game/citadel/gameinfo.gi`
2. Go to the `NetworkSystem` section and add `CreateListenSocketP2P    2` to it
3. Go to the `ConVars` section and add `"net_p2p_listen_dedicated"    "1"` to it
4. On the server run the `status` command, it will show your servers SteamID (Example `steamid  : [A:1:1864462358:30237] (90201861338001430)`)
5. On the client connect using the server SteamID (Example `connect [A:1:1864462358:30237]`)

```diff
NetworkSystem
{
+	CreateListenSocketP2P	2
	BetaUniverse
	{
		...
	}
}
...

ConVars
{
+	"net_p2p_listen_dedicated"	"1"
	"rate"
	{
		"min"		"98304"
		"default"	"786432"
		"max"		"1000000"
	}
	...
}
```

If you validate the game files or Valve modifies the `gameinfo.gi` in an update your changes will reset and you must go through the above steps again!
