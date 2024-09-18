Instructions on how to setup a match similar to Valve matchmaking. It will spawn you in a lane automatically with a preset hero. Statistics will be dumped at the end of the match.

1. Put `server-match.exe` into `Project8Staging/game/bin`, next to `project8.exe`
2. Create the following file `Project8Staging/match/match.json` and adjust it according to the example [`match.jsonc`](./match.jsonc) file found in this repository
3. Run the server using `server-match.exe -dedicated +sv_matchmaking_enabled 1 +map street_test`

To debug protobufs launch the server with `-protobufdebug`, it will dump all protobufs we handle to the server logs.

If you enable SourceTV the game will automatically try to upload a replay of the match by executing a file called `upload_replay.py`, this file does not exist and will show an error instead. You can suppress this by using `+citadel_upload_replay_enabled 0` in your launch options.

Stats will be dumped next to the `match.json` file in `result.json`. We are not able to add our own custom stat tracking to this, it is just what the server already tracks.
