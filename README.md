# sksonic

`sksonic` is a simple TUI music player for subsonic compatible servers.
It only requires `ffmpeg` (and a subsonic server).
It follows the same design as `ncmpcpp` but features fewer options.

At this moment, it consists of only two panels:
1. Music browser,
2. Playlist viewer/editor.

To configure it, please edit the `config.h` file directly and re-compile.
The relevant fields are:
`URL`, `PORT`, `USER`, `PWD`, `VERSION` AND `APP`, where `VERSION` is the `subsonic` API version, typically 1.16, and `APP` is the name with which `sksonic` will identify in `subsonic`.
Customisation includes:
- Selecting colours
- Defining an indicator for the currently played song in the playlist `playing_indicator`
- Defining a character to display the played percentage `slider_played`
- Defining a character to display the unplayed percentage `slider_unplayed`


## Compiling
`sksonic` depends on `ncurses` and `curl`.
It can be compiled using: `gcc sksonic.c -lncursesw -lcurl -o sksonic`
It also depends on cJSON, which is included in the repository.

## Usage
Keybindings can be modified by editing `config.h`
The default keybindings are:
### In the Music browser:
- `UP`, `DOWN`, `LEFT` and `RIGHT` arrows provide basic movement.
- `gg` moves to top.
- `G` moves to bottom.
- `SPACE` adds the highlighted Song the playlist.
- `ENTER` adds the highlighted Artist, Album or Song to the playlist and starts playing it.
- `p` toggles pause/play.
- `r` toggles repeat mode.
- `x` toggles shuffle mode.
- `s` stops.
- `<` plays the previous song in the playlist.
- `>` plays the next song in the playlist.
- `2` moves to the playlist panel.
- `q` exits.

### In the Playlist panel:
- `UP` and `DOWN` provide basic movement.
- `gg` moves to top.
- `G` moves to bottom.
- `ENTER` plays the selected song.
- `d` removes the selected song from the playlist.
- `c` clears the entire playlist (upon confirmation) and stop playback.
- `1` moves to the music browser.
- `r` toggles repeat mode.
- `x` toggles shuffle mode.
- `q` exits.

#### Note
I have tested `sksonic` only with navidrome, although it should work with any subsonic compatible server.
