Instructions on how to setup a match similar to Valve matchmaking. It will spawn you in a lane automatically with a preset hero after all players have connected and the pregame countdown has concluded. Statistics will be dumped at the end of the match.

1. Download the latest artifact from [Github Actions](https://github.com/BeepIsla/deadlock-tools/actions)
1. Put `server-match.exe` into `Project8Staging/game/bin`, next to `project8.exe`
1. Create the following file `Project8Staging/match/match.json` and adjust it according to the example [`match.jsonc`](./match.jsonc) file found in this repository
1. Run the server using `server-match.exe -dedicated +sv_matchmaking_enabled 1 +map street_test`

To debug protobufs launch the server with `-protobufdebug`, it will dump all protobufs we handle to the server logs.

The game will automatically try to upload SourceTV replay and metadata by executing a file called `upload_replay.py`, this file only exists on Valve servers as such you will encounter an error when the game tries to execute this file. You can suppress this by using `+citadel_upload_replay_enabled 0` and `+citadel_upload_metadata_enabled 0` in your launch options.

The game also sends performance statistics to Steam automatically, you can disable this by adding `+sv_matchperfstats_send_to_steam 0` to your launch options.

Stats will be dumped next to the `match.json` file in `result.json`. We are not able to add our own custom stat tracking to this, it is just what the server already tracks.

The server will automatically cancel the match after ~3 minutes! Make sure all players are ready to join when you boot the server.

_Linux compatibility will come once there are binaries for it_
