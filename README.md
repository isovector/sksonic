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

## Configuration Options

### Customizing the Colors
It is possible to modify the colors used in `sksonic` by editing `colors[NUM_COLORS][2]` in `config.h`

### Keybindings
The default keybindings can be changed by editing `keys[][2]` in `config.h`
Available actions are listed in `enum { play_pause, stop, next, previous, repeat, shuffle, quit, add, add_and_play, remove_one, remove_all, main_view, playlist_view, up, down, left, right, resize, bottom, top, chord };`
To modify a keybinding, replace the desired key in the array, for instance, to switch from using `p` to toggle play-pause to using `t`,
you should replace `{'p',               play_pause},` with `{'t',               play_pause},`
To create new bindings add new entries to the array.
Keybinding chors are separately defined in `enum { chord_top };` and `chords[][2]`

### Appearance
The aspect of some elements in the UI can be modified by editing `*appearance[5]`

### `notify_cmd`
The `notify_cmd` variable in `config.h` defines the program that `sksonic` should use to send notifications.
If `notify_cmd` is set to NULL, no notification will be displayed.

You can customize this variable to use any program that supports receiving notifications.
For example, you could set `notify_cmd` to `notify-send`.

To enable notifications, edit the `notify_cmd` field in `config.h` to specify the program you want to use.
The code in `sksonic.c` assumes `notify-send` style.

### `state_dump`
The `state_dump` variable in config.h specifies the file path where sksonic should save the current playback status.
If `state_dump` is set to NULL, no dump will be performed.

This variable allows you to save the app state about playback status to a file, which can be useful for processing the data with a custom script.
However, note that writing a custom script is required to process the output data.

To enable dumping of playback status, edit the `state_dump` field in `config.h` to specify the file path where you want to save the status.

#### Note
I have tested `sksonic` only with navidrome, although it should work with any subsonic compatible server.
