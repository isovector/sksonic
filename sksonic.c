#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <locale.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include <curl/curl.h>
#include "cJSON.c"

#include <ncurses.h>
#include "config.h.eric"
#define HASH_TABLE_SIZE 1024

typedef enum {
    PANEL_ARTISTS,
    PANEL_ALBUMS,
    PANEL_SONGS,
    NUM_PANELS
} PanelType;

typedef enum {
    WINDOW_INFO,
    WINDOW_PLAYLIST,
    WINDOW_PLAYBACK,
    NUM_WINDOWS
} WindowType;

typedef enum {
    MOVE_TOP,
    MOVE_BOTTOM,
    NUM_MOVEMENTS
} SpecialMovement;

typedef enum {
    STOPPED,
    PLAYING,
    PAUSED,
    NUM_STATUS
} StatusType;

typedef enum {
    NONE,
    SHUFFLE,
    REPEAT,
} ShuffleRepeatStatus;

typedef enum {
    VIEW_INFO,
    VIEW_PLAYLIST,
    NUM_VIEWS
} ViewType;

typedef struct {
    char *url;
    char *command;
    int playlist_pid;
} PlaybackThreadArgs;

typedef struct Connection {
    char *url;
    int port;
    char *user;
    char *password;
    char *version;
    char *app;
} Connection;

enum Operation {
    PING,
    ARTISTS,
    ALBUMS,
    SONGS,
    PLAY
};

typedef struct Song {
    char *id;
    char *name;
    int duration;
} Song;

typedef struct Album {
    char *id;
    char *name;
    int number_songs;
    Song *songs;
} Album;

typedef struct Artist {
    char *name;
    char *id;
    int number_albums;
    Album *albums;
} Artist;

typedef struct Database {
    Artist *artists;
    int number_artists;
} Database;

typedef struct Playlist {
    Song **songs;
    int size;
    int capacity;
    int current_playing;
    time_t start_time;
    int play_time;
    int status;
    int repeat_shuffle;
    int selected_song_idx;
    pid_t pid;
    ShuffleRepeatStatus shuffle_repeat_status;
} Playlist;

typedef struct Playback_Program {
    char *executable;
    char *flags;
} Playback_Program;

typedef struct AppState {
    Playlist *playlist;
    Database *db;
    Artist *artist;
    Album *album;
    ViewType current_view;
    PanelType current_panel;
    const Connection *const connection;
    const Playback_Program program;
    int selected_artist_idx;
    int selected_album_idx;
    int selected_song_idx;
    WINDOW **windows[NUM_WINDOWS];
} AppState;

struct url_data {
    int size;
    char *data;
};

/* Functions */
int write_url_data(void *ptr, int size, int nmemb, struct url_data *data);
int test_subsonic_connection(const Connection *const conn);
void pause_resume(const AppState *const app_state);
void stop_playback(const AppState *const app_state);
void generate_subsonic_url(const Connection *conn, enum Operation operation, const char *data, char **url);
char *fetch_url_data(const char *const url);
Database init_db(void);
static AppState init_appstate(void);
Playlist init_playlist(void);
void add_song(const Song *song, Playlist *playlist);
void delete_song(const AppState *const app_state);
void get_artists(const Connection *connection, Database *db);
void get_albums(const Connection *connection, Database *db, const char *artist_id);
void get_songs(const Connection *connection, Database *db, const char *artist_id, const char *album_id);
void request_albums(const Connection *const conn, Artist *const artist);
void request_songs(const Connection *conn, Album *album);
static WINDOW **create_windows(const int n, const int bottom_space, const WindowType window_type);
void print_window_data(const AppState *const app_state, PanelType panel, WINDOW *const *const window);
int add_to_playlist(AppState *app_state);
static inline void change_playback_status(const pid_t pid, const int signal);

static const Connection connection = {
    .url = URL,
    .port = PORT,
    .user = USER,
    .password = PWD,
    .version = VERSION,
    .app = APP
};

/**
 * Function to initialize the app state.
 *
 * @return The initialized app state.
 */
static AppState init_appstate(void) 
{
    static const Playback_Program program = {executable, flags};

    AppState state = {
        .selected_artist_idx = 0,
        .selected_album_idx = 0,
        .selected_song_idx = 0,
        .current_view = VIEW_INFO,
        .current_panel = PANEL_ARTISTS,
        .playlist = NULL,
        .connection = &connection,
        .program = program,
        .db = NULL,
        .artist = NULL,
        .album = NULL,
        .windows = { NULL },
    };
    return state;
}

/**
 * Function to delete an array of ncurses windows.
 *
 * @param windows Pointer to an array of ncurses windows.
 * @param num_windows Number of windows in the array.
 */
static inline void delete_windows(WINDOW * windows[], const int num_windows)
{
    for (int i = 0; i < num_windows; i++)
    {
        delwin(windows[i]);
        windows[i] = NULL;
    }
}

/**
 * Function that runs in a separate thread to play the song using ffplay.
 *
 * @param arg Pointer to a PlaybackThreadArgs struct containing data needed to run the playback.
 */
void *playback_thread(void *arg)
{
    // Get arguments and free memory associated with them.
    PlaybackThreadArgs *const  args = (PlaybackThreadArgs *) arg;
    char *const  url = args->url;
    char *const  command = args->command;
    free(args);

    // Open a pipe for reading from the ffplay process.
    FILE *const  fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open stream.\n");
        free(url);
        return NULL;
    }

    // Clean up allocated memory and close the file pointer.
    pclose(fp);
    free(url);

    return NULL;
}

/**
 * Function to set up ncurses for the program's user interface.
 * Initializes the ncurses library and sets various options, as well as defining color pairs.
 */
void setup_ncurses(void)
{
    initscr();
    clear();
    noecho();
    curs_set(0);
    cbreak();
    halfdelay(5);
    keypad(stdscr, TRUE);
    start_color();

    // Define color pairs with colours defined in config.h
    init_pair(1, colors[INACTIVE][0], colors[INACTIVE][1]);
    init_pair(2, colors[ACTIVE][0], colors[ACTIVE][1]);

    // Refresh the screen to update changes.
    refresh();
}

/**
 * Function to create an array of ncurses windows.
 *
 * @param n 		Number of windows to create.
 * @param bottom_space 	Height of the space at the bottom of the screen.
 * @param window_type 	The type of windows to create.
 * @return An array of ncurses windows.
 */
static WINDOW **create_windows(const int number_windows, const int bottom_space, const WindowType window_type)
{
    // Get screen dimensions
    const int screen_h = LINES;
    const int screen_w = COLS;

    // Compute window dimensions
    const int win_w = screen_w / number_windows;
    const int win_h = window_type == WINDOW_PLAYBACK ? bottom_space : screen_h - bottom_space;

    // Create windows
    WINDOW **windows = calloc(number_windows, sizeof(WINDOW *));
    const int remaining_w = screen_w % number_windows;
    int x = 0;

    // Create windows with equal width and add extra column to some of them based on the remaining width
    for (int i = 0; i < number_windows; ++i) {

	// Determine the width of the current window
	int w = win_w + (i < remaining_w);
	
	// Create a new window with the calculated width
	windows[i] = newwin(win_h, w, window_type == WINDOW_PLAYBACK ? screen_h - bottom_space : 0, x);
	
	// Increment x-coordinate by the width of the current window
	x += w;

        if (window_type != WINDOW_PLAYBACK)
        {
            box(windows[i], 0, 0);
        }
    }
    return windows;
}

/**
 * Returns the number of characters (or glyphs) in a string
 *
 * @param str A null-terminated string
 * @return The number of characters (or glyphs) in the string
 */
static int get_char_count(const char * str)
{
    int count = 0;
    while (*str != '\0') {
        // Check if the current byte is the first byte of a multi-byte sequence
        if ((*str & 0xC0) != 0x80) {
            count++;
        }
        // Move to the next byte
        str++;
    }
    return count;
}

/**
 * Returns the number of bytes in a string that correspond to characters.
 *
 * @param text A null-terminated string
 * @param max_width The maximum width of the formatted text in characters
 * @return The number of bytes in the formatted text that correspond to characters
 */
static int get_character_byte_count(const char *text, const int max_width)
{
    const char *p = text;
    size_t formatted_len = 0;
    size_t byte_count = 0;

    // Advance glyph by glyph, computing the correct size
    while((*p != '\0') && (formatted_len < max_width)) {
        formatted_len++;
        switch (*p & 0xF0) {
            case 0x00:
            case 0x10:
            case 0x20:
            case 0x30: // single-byte character
                byte_count += 1;
                p += 1;
                break;
            case 0xC0: // two-byte character
                byte_count += 2;
                p +=2 ;
                break;
            case 0xE0: // three-byte character
                byte_count += 3;
                p += 3;
                break;
            case 0xF0: // four-byte character
                byte_count += 4;
                p += 4;
                break;
            default: // invalid character
                fprintf(stderr, "Invalid character.\n");
                return -1;
        }
    }
    return byte_count;
}

/**
 * Formats a string to fit within a certain width, adding a prefix and ellipsis if necessary.
 * Returns a newly allocated formatted string that must be freed by the caller or NULL on failure.
 *
 * @param text The input text string to format
 * @param max_width The maximum width of the output string
 * @param prefix The prefix to prepend to the output string
 * @return A newly allocated formatted string or NULL on failure
 */
char *format_text(const char *const  text, const int max_width, const char *const  prefix)
{
    if (text == NULL || prefix == NULL || max_width < 0)
    {
        return NULL;
    }

    // Lenghts measured in characters
    const int prefix_len = get_char_count(prefix);
    const int text_len = get_char_count(text);
    const int total_len = prefix_len + text_len;

    // Lengths measured in bytes
    const size_t prefix_size = strlen(prefix);
    const size_t text_size = strlen(text);

    if (total_len <= max_width) {
        // Determine number of padding spaces
        const int padding_len = max_width - total_len;

        // Allocate enough space for formatted string
        const size_t bytes_formatted = prefix_size + text_size + padding_len + 1;
        char * formatted = calloc(bytes_formatted, sizeof(char));
        if (formatted == NULL) {
                return NULL;
        }

        // Build formatted string with padding spaces
        memcpy(formatted, prefix, prefix_size);
        memcpy(formatted + prefix_size, text, text_size);
        memset(formatted + prefix_size + text_size, ' ', padding_len);
        formatted[bytes_formatted - 1] = '\0';
        return formatted;
    }
    else {
	// Lengths measured in bytes
        const int bytes_ellipsis = sizeof("\xE2\x80\xA6");
        const size_t bytes_prefix = strlen(prefix);
        const size_t bytes_text = get_character_byte_count(text, max_width - (prefix == NULL ? 0 : 1));
        const size_t bytes_formatted = bytes_prefix + bytes_text + bytes_ellipsis + 1;

        // Allocate enough space to hold the entire string with prefix and ellipsis
        char * formatted = calloc(bytes_formatted, sizeof(char));
	if (formatted == NULL) {
            return NULL;
	}

        // Build the formatted string with ellipses
        memcpy(formatted, prefix, bytes_prefix);
        memcpy(formatted + bytes_prefix, text, bytes_text);
        memcpy(formatted + bytes_prefix + bytes_text, "\xE2\x80\xA6", bytes_ellipsis);
        formatted[bytes_formatted - 1] = '\0';
        return formatted;
    }
}

/**
 * Prints the playlist associated with the given app state to the specified window.
 * The playlist is printed starting at the currently selected song, and if necessary,
 * is scrolled to ensure that the selected song is visible.
 *
 * @param app_state a pointer to the AppState struct containing the playlist to print.
 * @param window a pointer to the ncurses window in which to print the playlist.
 */
void print_playlist_data(const AppState *const app_state, WINDOW *const *const windows)
{
    WINDOW *const window = windows[0];
    const Playlist *const playlist = app_state->playlist;
    const int max_row = getmaxy(window) - 2;
    const int max_col = getmaxx(window) - 2;
    const int current_index = playlist->selected_song_idx;
    const int current_playing = playlist->current_playing;
    const int number_items = playlist->size;

    // Define the row decoration, based on whether the current panel is active or not
    const int first_item = (number_items <= max_row) ? 0 :
                           (current_index <= max_row/3) ? 0 :
                           (current_index < number_items - max_row*2/3) ? current_index - max_row/3 :
                           number_items - max_row;
    const int last_item = MIN(first_item + max_row, number_items);

    wclear(window);
    box(window, 0, 0);

    // Loop through each item to display, formatting the text as necessary and applying row decoration
    for (int i = first_item; i < last_item; i++) {
        char *text = format_text(playlist->songs[i]->name, max_col, (i == current_playing) ? appearance[ind_playing] : "");
	// Decorate the row, based on whether the current row is selected or not
        wattron(window, (i == current_index) ? COLOR_PAIR(ACTIVE + 1) : COLOR_PAIR(INACTIVE + 1) | A_REVERSE);
        mvwprintw(window, i - first_item + 1, 1, "%s", text);
        wattroff(window, (i == current_index) ? COLOR_PAIR(ACTIVE + 1) : COLOR_PAIR(INACTIVE + 1) | A_REVERSE);
        free(text);
    }
    wrefresh(window);
}

/**
 * Prints the data associated with a given panel type to the specified window. The data displayed
 * depends on the panel type provided, and is based on the given app state. The currently selected item
 * is highlighted, and if necessary, the data is scrolled to ensure that the selected item is visible.
 *
 * @param app_state A constant pointer to a struct representing the current state of the application.
 * @param panel An enum value indicating which panel to print the data onto.
 * @param windows A constant pointer to an array of WINDOW pointers, representing the different panels of the application.
 *
 * @return void
 */
void print_window_data(const AppState *const app_state, PanelType panel, WINDOW *const *const windows)
{
    WINDOW *const window = windows[panel];
    const int max_row = getmaxy(window) - 2;
    const int max_col = getmaxx(window) - 2;
    int current_index = 0;
    int number_items = 0;

    wclear(window);
    box(window, 0, 0);

    // Determine which data to display in the given panel, based on the app state and panel type
    void *ptr = NULL;
    switch (panel)
    {
        case PANEL_ARTISTS:
            current_index = app_state->selected_artist_idx;
            number_items = app_state->db->number_artists;
	    // Assign the address of the beginning of the array of artists in the database struct
	    // pointed to by app_state->db to the void pointer ptr
	    ptr = (void *)app_state->db->artists;
            break;
        case PANEL_ALBUMS:
            current_index = app_state->selected_album_idx;
            number_items = app_state->artist->number_albums;
	    // Assign the address of the beginning of the array of albums in the artist struct
	    // pointed to by app_state->artist to the void pointer ptr
	    ptr = (void *)app_state->artist->albums;
            break;
        case PANEL_SONGS:
            current_index = app_state->selected_song_idx;
            number_items = app_state->album->number_songs;
	    // Assign the address of the beginning of the array of songs in the artist struct
	    // pointed to by app_state->album to the void pointer ptr
	    ptr = (void *)app_state->album->songs;
            break;
        default:
            current_index = 0;
            number_items = 0;
            break;
    }

    // Determine which items to display in the window, based on the current index and the number of items
    const int first_item = (number_items <= max_row) ? 0 :
                           (current_index <= max_row/3) ? 0 :
                           (current_index < number_items - max_row*2/3) ? current_index - max_row/3 :
                           number_items - max_row;
    const int last_item = MIN(first_item + max_row, number_items);

    // Define the row decoration, based on whether the current panel is active or not
    const int row_decoration[][2] = {
        // Active panel		                  Inactive panel 	
	{  COLOR_PAIR(ACTIVE + 1),   	          COLOR_PAIR(INACTIVE + 1) },              // Selected row
	{  COLOR_PAIR(INACTIVE + 1) | A_REVERSE,  COLOR_PAIR(INACTIVE + 1) | A_REVERSE 	}, // Not selected row
    };

    const int is_active_panel = app_state->current_panel == panel ? 0 : 1;

    // Loop through each item to display, formatting the text as necessary and applying row decoration
    for (int i = first_item; i < last_item; i++) {
	char *text = NULL;
	if (ptr) {
	    if (panel == PANEL_ARTISTS) {
		text = ((Artist *)ptr)[i].name;
	    } else if (panel == PANEL_ALBUMS) {
		text = ((Album *)ptr)[i].name;
	    } else if (panel == PANEL_SONGS) {
		text = ((Song *)ptr)[i].name;
	    }
	}
	text = format_text(text, max_col, "");
	wstandend(window);
	int is_selected_item = (i == current_index - first_item) ? 0 : 1;
	wattron(window, row_decoration[is_selected_item][is_active_panel]);
        mvwprintw(window, i + 1, 1, "%s", text);
        wattron(window, COLOR_PAIR(INACTIVE + 1) | A_REVERSE);
        free(text);
    }
    wrefresh(window);
}

/**
 * Returns the action associated with the given key press, looked up from a local list
 *
 * @param keypress The key press to look up
 * @param keys_local An array of key-value pairs to search through
 * @param len The length of the keys_local array
 *
 * @return The corresponding action or -1 if not found
 */
int get_action(const int keypress)
{
    static int initialized = 0;
    static int hash_table[512] = {0}; // We use the maximum number as per ncurses definitions

    if (initialized == 0) {
        for (int i = 0; i < len_keys; ++i) {
            hash_table[keys[i][0]] = keys[i][1];
        }
        initialized = 1;
    }

    if (keypress == -1) {
        return -1;
    }

    int action = hash_table[keypress];
    if (action >= 0) {
        return action;
    }

    return -1;
}

/**
 * Updates the selected indexes of the panels based on the given movement and current state of the app.
 * If the current view is VIEW_PLAYLIST, only updates the selected song index.
 * If the movement is MOVE_TOP or MOVE_BOTTOM, sets the panel destination to the corresponding offset based on the current panel.
 * If the movement is up or down and the current view is VIEW_INFO, updates the selected index of the current panel if possible and also updates the selected indexes of other panels depending on the current panel.
 * If the movement is left or right and the current view is VIEW_INFO, updates the current panel index.
 
 * @param action The action to perform
 * @param app_state Pointer to the AppState struct
 * @param special_movement The type of special movement (MOVE_TOP or MOVE_BOTTOM) to perform, -1 if not applicable
 */
void movement(const int action, AppState *app_state, const SpecialMovement special_movement)
{
    if (action == -1) {
        return;
    }

    if (app_state->current_view == VIEW_PLAYLIST) {

        // Declare an array of pointers to integer variables that hold the selected index of each panel.
        int *const panel_destinations[1] = {
            &app_state->playlist->selected_song_idx,
        };

        const int current_panel = 0;

        // Define panel offsets for each panel type, which are used to determine
        // the first and last index of each panel when navigating with MOVE_TOP
        // and MOVE_BOTTOM movements.
        const int panel_offset[][2] = {
            //MOVE_TOP      MOVE_BOTTOM
            {0,             app_state->playlist->size - 1 }, // Playlist
        };

        // If the movement is valid, set the panel destination to the corresponding
        // offset based on the current panel and the movement type.
        if (special_movement < NUM_MOVEMENTS) {
            *panel_destinations[current_panel] = panel_offset[current_panel][special_movement];
        }
        // Check if it is possible to go up
        else if (action == up && app_state->playlist->selected_song_idx > 0) {
	    --app_state->playlist->selected_song_idx;
	}
        // Check if it is possible to go down
	else if (action == down && app_state->playlist->selected_song_idx < panel_offset[current_panel][1]) {
	    ++app_state->playlist->selected_song_idx;
	}
	return;
    }

    // Declare an array of pointers to integer variables that hold the selected index of each panel.
    int *const panel_destinations[3] = {
            &app_state->selected_artist_idx,
            &app_state->selected_album_idx,
            &app_state->selected_song_idx,
        };

    const PanelType current_panel = app_state->current_panel;

    // Define panel offsets for each panel type, which are used to determine
    // the first and last index of each panel when navigating with MOVE_TOP
    // and MOVE_BOTTOM movements.
    const int panel_offset[][2] = {
        // MOVE_TOP     MOVE_BOTTOM
        {0,             app_state->db->number_artists - 1   },  // ARTISTS_PANEL
        {0,             app_state->artist->number_albums -1 },  // ALBUMS_PANEL
        {0,             app_state->album->number_songs - 1  },  // SONGS_PANEL
    };

    // If the movement is valid, set the panel destination to the corresponding
    // offset based on the current panel and the movement type.
    if (special_movement < NUM_MOVEMENTS) {
        *panel_destinations[current_panel] = panel_offset[current_panel][special_movement];
    }
    else if (app_state->current_view == VIEW_INFO) {
        switch (action)
        {
            case up:
            case down:
                {
                    const int offset = action == down ? 1 : -1;
                    const int new_idx = *panel_destinations[current_panel] + offset;
                    if ((new_idx >= 0) && (new_idx <= panel_offset[current_panel][1])) {
                        *panel_destinations[current_panel] = new_idx;
                    }
                    if (current_panel == PANEL_ARTISTS) {
                        app_state->selected_album_idx = 0;
                    }

                    if (current_panel == PANEL_ALBUMS) {
                        app_state->selected_song_idx = 0;
                    }
                    break;
                }
            case left:
                if (app_state->current_panel > 0) {
                    --app_state->current_panel;
                }
                break;
            case right:
                if (app_state->current_panel < NUM_PANELS) {
                    ++app_state->current_panel;
                }
                break;
            default:
                break;
        }
    }
    Artist *const artist = &(app_state->db->artists[app_state->selected_artist_idx]);
    app_state->artist = artist;
    get_albums(app_state->connection, app_state->db, artist->id);
    Album *const album = &(artist->albums[app_state->selected_album_idx]);
    app_state->album = album;
    get_songs(app_state->connection, app_state->db, artist->id, album->id);
    return;
}

/**
 * Updates the contents of multiple windows based on the current view of the application state.
 *
 * @param app_state A pointer to the current state of the application
 * @param windows An array of pointers to the windows to be updated
 * @param number_windows The number of windows in the array
 */
void refresh_windows(const AppState *const app_state, WINDOW *const *const windows, const int number_windows)
{
    switch (app_state->current_view)
    {
        case VIEW_INFO:
            for (int i = 0; i < number_windows; ++i)
            {
                print_window_data(app_state, i, windows);
            }
            break;
        case VIEW_PLAYLIST:
            for (int i = 0; i < number_windows; ++i)
            {
                print_playlist_data(app_state, windows);
            }
            break;
        default:
            break;
    }
}

/**
 * Initializes a new empty database.
 * 
 * @return A new empty database struct.
 */
Database init_db(void) 
{
    // Initialize an empty database struct with NULL artists pointer and 0 number of artists.
    return (Database) {
        .artists = NULL,
        .number_artists = 0,
    };
}

/**
 * Initializes a new playlist by allocating memory for 10 songs and setting
 * all other fields to their default values.
 *
 * @return A new playlist struct.
 */
Playlist init_playlist(void)
{
    // Allocate memory for 10 Song pointers using malloc.
    Song **songs = (Song **) malloc(10 * sizeof(Song*));

    // If malloc fails to allocate memory, clean up and exit program.
    if (songs == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for playlist.\n");
        exit(EXIT_FAILURE);
    }

    // Initialize and return a new Playlist struct with dynamically allocated memory for songs.
    return (Playlist) {
        .songs = &songs[0],
        .size = 0,
        .capacity = 10,
        .current_playing = 0,
        .start_time = (time_t) NULL,
        .play_time = 0,
        .status = STOPPED,
        .repeat_shuffle = 0,
        .selected_song_idx = -1,
        .pid = -1,
        .shuffle_repeat_status = NONE,
    };
}

/**
 * Adds a song to the end of the playlist. If the playlist is full, its capacity is doubled.
 *
 * @param song     A pointer to the Song struct to be added.
 * @param playlist A pointer to the Playlist struct where the song will be added.
 */
void add_song(const Song *const song, Playlist *const playlist)
{
    // If the playlist is full, double its capacity by reallocating its memory block.
    if (playlist->size == playlist->capacity)
    {
        // Double the playlist's capacity using bit shifting for faster multiplication.
        playlist->capacity <<= 1;

        // Reallocate the memory block with the new capacity.
        void *p = realloc(playlist->songs, playlist->capacity * sizeof(playlist->songs));

	// If realloc fails, print an error message and exit the program.
	if (p == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory for playlist.\n");
            exit(EXIT_FAILURE);
        }

        // Update the playlist's memory block and song array pointers
        playlist->songs = p;
		
    }

    // Add the song to the end of the playlist and update its size
    playlist->songs[playlist->size++] = (Song *) song;

    // If the selected song index was not previously set, set it to the first song in the playlist.
    if (playlist->selected_song_idx == -1) {
        playlist->selected_song_idx = 0;
    }
}

/**
 * Deletes the currently selected song from the playlist and adjusts the playlist size and capacity if necessary.
 *
 * @param app_state A pointer to the AppState struct containing the current program state.
 */
void delete_song(const AppState *const app_state)
{
    Playlist *const playlist = app_state->playlist;
    const int index = playlist->selected_song_idx;
    const int playlist_size = playlist->size;

    // If the selected song index is out of range, return without deleting any song.
    if (index >= playlist_size || index < 0)
    {
        return;
    }

    // Stop playback when deleting the song that is currently being played
    if (index == playlist->current_playing)
    {
        stop_playback(app_state);
    }

    // Use memmove to shift all elements after the deleted song one position to the left.
    memmove(&playlist->songs[index], &playlist->songs[index + 1],
            (playlist_size - index - 1) * sizeof(*playlist->songs));

    // Decrement the playlist size after deleting the song.
    playlist->size--;

    // If the playlist size is less than a quarter of its capacity and the capacity is greater than 10,
    // reduce the capacity to half its current value using bit shifting instead of division.
    if (playlist->capacity > 10 && playlist_size < playlist->capacity / 4) {
        playlist->capacity >>= 1; // Bit shift right by 1 to divide by 2.
        playlist->songs = realloc(playlist->songs, playlist->capacity * sizeof(*playlist->songs));
        // If realloc fails to allocate memory, print an error message and exit the program.
        if (playlist->songs == NULL) {
            fprintf(stderr, "Error: Failed to reallocate memory for playlist.\n");
            exit(EXIT_FAILURE);
        }
    }

    // If there are no more songs in the playlist, set the selected song index to -1.
    if (playlist_size == 0) {
        playlist->selected_song_idx = -1;
    }
    // If the deleted song was the last one in the playlist, select the new last song.
    else if (index + 1== playlist_size) {
        --(playlist->selected_song_idx);
    }
}

/**
 * Gets the process ID (PID) of the running instance of the specified program.
 *
 * This function executes the "pidof" command to obtain the PID of the specified executable. It assumes that the
 * "pidof" command is available on the system and does not perform any error checking or validation.
 *
 * @param program A Playback_Program struct containing the name of the program executable to look up.
 * @return The PID of the running instance of the program, or -1 if no such instance is found.
 */
pid_t get_pid(const Playback_Program program)
{
    char command[100];
    // Add cut because pthreads spawns a `sh` command which, in turn, starts program.executable
    snprintf(command, sizeof(command), "pidof %s | cut -d \" \" -f 2", program.executable);

    FILE *const fp = popen(command, "r");
    if (fp == NULL) {
        perror("Error executing command");
        return (pid_t) -1;
    }

    // Read the output of the pidof command into a fixed-size buffer.
    char pid_str[16];
    if (fgets(pid_str, sizeof(pid_str), fp) == NULL) {
        pclose(fp);
        return (pid_t) -1;
    }

    // Convert the PID string to an integer
    pclose(fp);
    return (pid_t) atoi(pid_str);
}

/**
 * Changes the playback status of a running process by sending it a signal.
 *
 * This function sends the specified signal to the process with the given PID, using the kill() system call.
 *
 * @param pid The process ID of the running process.
 * @param signal The signal to send to the process.
 */
static inline void change_playback_status(const pid_t pid, const int signal)
{

    // If the PID is invalid or not set, there's nothing to do.
    if (pid <= 0) {
        return;
    }

    if (kill(pid, signal) == -1) {
        // If the kill() call fails, log an error message.
        perror("Error sending signal to process");
    }
}

/**
 * Plays a song from the current playlist.
 *
 * This function stops any currently-playing songs and starts playing the specified song. It generates a URL to stream the song,
 * executes the specified player program with appropriate flags, and updates the playlist state accordingly.
 *
 * @param app_state A pointer to the current application state.
 * @param index The index of the song in the playlist to play.
 */
void play_song(const AppState *const app_state, const int index)
{
    // Get references to the relevant data structures.
    Playlist *const playlist = app_state->playlist;
    const Connection *const connection = app_state->connection;
    const Playback_Program program = app_state->program;

    // Validate the song index.
    if (index >= playlist->size) {
        fprintf(stderr, "Invalid song index.\n");
        return;
    }

    // Send SIGTERM to the playlist process to stop any other playing processes.
    change_playback_status(playlist->pid, SIGTERM);

    // Set up playlist state for the new song.
    const Song *const song = playlist->songs[index];
    playlist->start_time = time(NULL);
    playlist->current_playing = index;
    playlist->status = PLAYING;
    playlist->play_time = 0;

    // Generate URL to stream the song.
    char *url = NULL;
    generate_subsonic_url(connection, PLAY, song->id, &url);

    // Construct the command string to run the playback program.
    const size_t command_len = snprintf(NULL, 0, "%s %s \"%s\" > /dev/null 2>&1", program.executable, program.flags, url) + 1;
    char command[command_len];
    snprintf(command, command_len, "%s %s \"%s\" > /dev/null 2>&1", program.executable, program.flags, url);

    // Set up arguments to pass to the playback thread.
    PlaybackThreadArgs *const args = malloc(sizeof(PlaybackThreadArgs));
    args->url = url;
    args->command = command;
    args->playlist_pid = playlist->pid;

    // Start the playback thread and detach it so that it runs independently.
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, &playback_thread, (void *) args);
    pthread_detach(thread_id);

    // Store the new process ID in the playlist state.
    playlist->pid = get_pid(program);
}

/**
 * Stops any currently-playing song in the playlist.
 *
 * This function sends a SIGTERM signal to the process ID associated with the current playlist, if one exists, and updates the
 * playlist state to indicate that playback has stopped.
 *
 * @param app_state A pointer to the current application state.
 */
void stop_playback(const AppState *const app_state)
{
    // Get a reference to the current playlist.
    Playlist *const playlist = app_state->playlist;

    // Check if there is a current song playing.
    if (playlist->status != STOPPED) {
        // Stop the playback process and update playlist state.
        change_playback_status(app_state->playlist->pid, SIGTERM);
        app_state->playlist->status = STOPPED;
        app_state->playlist->start_time = (time_t) NULL;
        app_state->playlist->play_time = -1;
    }
}

/**
 * Pauses or resumes playback of a song in the playlist.
 *
 * If there is a current song playing, this function sends a SIGSTOP signal to pause playback, or a SIGCONT signal to resume playback,
 * depending on the current state of the playlist. It then updates the playlist state to reflect the new status.
 *
 * @param app_state A pointer to the current application state.
 */
void pause_resume(const AppState *const app_state)
{
    Playlist *const playlist = app_state->playlist;
    time_t now;
    // Check if there is a current song playing.
    switch (playlist->status) {
        case STOPPED:
            // Do nothing if no song is playing.
            break;
        case PLAYING:
            // Pause playback and update playlist state.
            change_playback_status(playlist->pid, SIGSTOP);
            playlist->status = PAUSED;
            break;
        case PAUSED:
            // Resume playback and update playlist state.
            now = time(NULL);
            playlist->start_time = now;
            change_playback_status(playlist->pid, SIGCONT);
            playlist->status = PLAYING;
            break;
    }
    return;
}

/**
 * Callback function used to receive data from a libcurl request and write it to a buffer.
 *
 * This function is called by libcurl whenever new data is received in response to a URL request. It appends the new data to the existing
 * buffer pointed to by the `data` parameter, reallocating memory as necessary, and updates the size of the buffer accordingly.
 *
 * @param ptr A pointer to the received data.
 * @param size The size of each data element.
 * @param nmemb The number of elements received.
 * @param data A pointer to a url_data struct containing information about the buffer being written to.
 * @return The total number of bytes written to the buffer.
 */
int write_url_data(void *const ptr, const int size, const int nmemb, struct url_data *const data)
{
    // Get the starting index and number of bytes to write.
    const int start_index = data->size;
    const int bytes_written = (size * nmemb);

    // Resize the data buffer to accommodate the new data.
    char *new_data_ptr = realloc(data->data, data->size + bytes_written + 1); // +1 for '\0'
    if (new_data_ptr == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for URL data.\n");
        if (data->data) {
            free(data->data);
            data->data = NULL;
            data->size = 0;
        }
        return 0;
    }

    data->data = new_data_ptr;
    memcpy((data->data + start_index), ptr, bytes_written);
    data->size += bytes_written;
    data->data[data->size] = '\0';

    return bytes_written;
}

/**
 * Tests a connection to a Subsonic server by sending a ping request and parsing the JSON response.
 *
 * @param conn A pointer to a Connection struct containing information about the Subsonic server to connect to.
 * @return 1 if the connection is successful.
 */
int test_subsonic_connection(const Connection *const conn)
{
    CURL *const curl_handle = curl_easy_init();
    CURLcode curl_res;
    int subsonic_connection_ok = 0;
    char *ping_url = NULL;

    // Initialize response data
    struct url_data response_data = {
        .size = 0,
        .data = calloc(4096, sizeof(char))
    };

    if (response_data.data == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for response data.\n");
        exit(EXIT_FAILURE);
    }

    // Generate ping URL
    generate_subsonic_url(conn, PING, NULL, &ping_url);

    // Set curl options
    curl_easy_setopt(curl_handle, CURLOPT_URL, ping_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_url_data);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2);
    free(ping_url);

    // Execute request
    curl_res = curl_easy_perform(curl_handle);
    if (curl_res != CURLE_OK) {
        fprintf(stderr, "Error: curl_easy_perform() failed: %s\n", curl_easy_strerror(curl_res));
        exit(EXIT_FAILURE);
    }

    // Clean up curl handle
    curl_easy_cleanup(curl_handle);

    // Parse JSON response
    cJSON *const response_root = cJSON_Parse(response_data.data);
    cJSON *const subsonic_response = cJSON_GetObjectItemCaseSensitive(response_root, "subsonic-response");
    free(response_data.data);

    // Check subsonic response status
    if (subsonic_response) {
        cJSON *const status_item = cJSON_GetObjectItemCaseSensitive(subsonic_response, "status");
        if (status_item && cJSON_IsString(status_item)) {
            subsonic_connection_ok = (strcmp("ok", status_item->valuestring) == 0);
        }
    }

    // Delete cJSON objects
    cJSON_Delete(response_root);

    // Return connection status
    if (subsonic_connection_ok) {
        return 1;
    } else {
        fprintf(stderr, "Error: Failed to connect to Subsonic server.\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * Generates a Subsonic API URL for a given operation and data.
 *
 * @param conn The connection settings to use for generating the URL.
 * @param operation The operation to perform.
 * @param data The data to include in the URL (optional).
 * @param url A pointer to a char pointer to store the generated URL.
 *
 * @return void
 * 
 * @note The memory for the generated URL is allocated dynamically and must be freed by the caller.
 */
void generate_subsonic_url(const Connection *const conn, enum Operation operation, const char *data, char **url)
{
    const char *path;

    // Set the path based on 'operation'
    switch (operation) {
        case PING:
            path = "rest/ping.view";
            break;
        case ARTISTS:
            path = "rest/getArtists";
            break;
        case ALBUMS:
            path = "rest/getArtist";
            break;
        case SONGS:
            path = "rest/getAlbum";
            break;
        case PLAY:
            path = "rest/stream";
            break;
        default:
            fprintf(stderr, "Invalid operation.\n");
            return;
    }

    const size_t len_url = snprintf(NULL, 0, "%s:%d/%s?f=json&u=%s&p=%s&v=%s&c=%s%s%s",
            conn->url, conn->port, path, conn->user, conn->password,
            conn->version, conn->app, data ? "&id=" : "", data ? data : "");

    *url = malloc(sizeof(char) * (len_url + 1));
    if (NULL == (*url)) {
        fprintf(stderr, "Failed to allocate memory for the URL.\n");
        return;
    }

    snprintf(*url, len_url + 1, "%s:%d/%s?f=json&u=%s&p=%s&v=%s&c=%s%s%s",
            conn->url, conn->port, path, conn->user, conn->password,
            conn->version, conn->app, data ? "&id=" : "", data ? data : "");
}

/**
 * Fetches data from a given URL using libcurl and returns it as a string.
 *
 * @param url The URL to fetch data from.
 *
 * @return A pointer to the string containing the fetched data. This memory should be freed by the caller.
 *         If an error occurs, NULL is returned.
 */
char *fetch_url_data(const char *const url) {
    CURL *curl_handle = curl_easy_init();
    const int initial_size = 4096;

    struct url_data url_data = {
        .size = 0,
        .data = malloc(initial_size)
    };

    if (url_data.data == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        exit(EXIT_FAILURE);
    }

    url_data.data[0] = '\0';

    // If curl_handle failed, exit.
    if (curl_handle == NULL) {
        fprintf(stderr, "Error: Failed to initialize curl handle.\n");
        exit(EXIT_FAILURE);
    }

    CURLcode curl_result;

    // Set curl options and perform request.
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_url_data);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &url_data);
    curl_easy_setopt(curl_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2);
    curl_result = curl_easy_perform(curl_handle);

    if (curl_result != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(curl_result));
        free(url_data.data);
        return NULL;
    }

    // Return fetched data and clean up.
    curl_easy_cleanup(curl_handle);
    return url_data.data;
}

/**
 * Fetches a list of artists from a Subsonic server using the provided connection information, populates a database structure
 * with the artist information, and stores the result in memory.
 *
 * @param connection A pointer to a Connection struct that contains information about the Subsonic server.
 * @param db A pointer to a Database struct that will be populated with the list of artists.
 *
 * @return None.
 * 
 * @remarks The caller is responsible for freeing the memory allocated for the Database struct.
 */
void get_artists(const Connection *const connection, Database *db)
{
    // Generate the URL to fetch the artists data
    char *url;
    generate_subsonic_url(connection, ARTISTS, NULL, &url);

    char *response = fetch_url_data(url);
    cJSON *const response_root = cJSON_Parse(response);
    cJSON *const subsonic_response = cJSON_GetObjectItemCaseSensitive(response_root, "subsonic-response");
    
    if (strcmp("ok", cJSON_GetObjectItemCaseSensitive(subsonic_response, "status")->valuestring) != 0)
    {
        fprintf(stderr, "Error: Failed to retrieve artists from Subsonic server/\n");
        exit(EXIT_FAILURE);
    }

    int number_artists = 0;
    cJSON *const index = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(subsonic_response, "artists"), "index");
    const cJSON *alphabetical_index;

    // Count the number of artists in the response
    cJSON_ArrayForEach(alphabetical_index, index)
    {
        const cJSON *const artists_list = cJSON_GetObjectItemCaseSensitive(alphabetical_index, "artist");
        const cJSON *artist; 
        cJSON_ArrayForEach(artist, artists_list)
        {
            number_artists++;
        }
    }

    // Allocate memory for artists in the database
    db->number_artists = number_artists;
    db->artists = malloc(sizeof(Artist) * number_artists);

    // Loop through the artists in the response and store them in the database
    int i = 0;
    cJSON_ArrayForEach(alphabetical_index, index)
    {
        const cJSON *const artists_list = cJSON_GetObjectItemCaseSensitive(alphabetical_index, "artist");
        const cJSON *artist;
        cJSON_ArrayForEach(artist, artists_list)
        {
            Artist *const a = &(db->artists[i]);
            a->id = strdup(cJSON_GetObjectItemCaseSensitive(artist, "id")->valuestring);
            a->name = strdup(cJSON_GetObjectItemCaseSensitive(artist, "name")->valuestring);
            a->number_albums = 0;
            a->albums = NULL;
            i++;
        }
    }

    // Clean up
    cJSON_Delete(response_root);
    free(url);
    free(response);
}

/**
 * Finds an artist in the database based on the artist ID.
 *
 * @param db The database to search.
 * @param artist_id The ID of the artist to find.
 *
 * @return The index of the artist in the database, or -1 if not found.
 */
int find_artist(const Database *const db, const char *const artist_id) {
    // Check if the artist is already in the database
    for (int i = 0; i < db->number_artists; i++) {
        const Artist *const artist = &db->artists[i];
        if (strcmp(artist->id, artist_id) == 0) {
            // Artist is already in the database, return the album list
            return i;
        }
    }
    return -1;
}

/**
 * Finds an album in the given artist's album list with the given ID.
 *
 * @param artist   The artist to search for the album in.
 * @param album_id The ID of the album to search for.
 *
 * @return The index of the album in the artist's album list, or -1 if the album was not found.
 */
int find_album(const Artist *const artist, const char *const album_id) {
    // Check if the artist is already in the database
        for (int i = 0; i < artist->number_albums; i++)
        {
        const Album *const album = &artist->albums[i];
        if (strcmp(album->id, album_id) == 0) 
        {
            // Album is already in the database, return its index
            return i;
        }
    }
    return -1;
}

/**
 * Retrieves albums for an artist from a Subsonic server and stores them in the provided Artist struct.
 *
 * @param conn   Connection struct containing information about the Subsonic server
 * @param artist Artist struct to store the retrieved albums in
 */
void request_albums(const Connection *const conn, Artist *const artist) {
    if (artist->albums != NULL) {
        // If album information for this artist has already been retrieved, return
        return;
    }

    char *url = NULL;
    generate_subsonic_url(conn, ALBUMS, artist->id, &url);

    char *response = fetch_url_data(url);
    cJSON *const response_root = cJSON_Parse(response);
    cJSON *const subsonic_response = cJSON_GetObjectItemCaseSensitive(response_root, "subsonic-response");

    if (strcmp("ok", cJSON_GetObjectItemCaseSensitive(subsonic_response, "status")->valuestring) != 0)
    {
        fprintf(stderr, "Error: Failed to retrieve artists from Subsonic server/\n");
        exit(EXIT_FAILURE);
    }

    const cJSON *const artist_json = cJSON_GetObjectItemCaseSensitive(subsonic_response, "artist");
    const int number_albums = cJSON_GetObjectItemCaseSensitive(artist_json, "albumCount")->valueint;
    if (number_albums == 0) {
        return; // No albums found for this artist
    }
    
    artist->albums = malloc(sizeof(Album) * number_albums);
    artist->number_albums = number_albums;
    const cJSON *const albums = cJSON_GetObjectItemCaseSensitive(artist_json, "album");
    const cJSON *album;
    int i = 0;
    cJSON_ArrayForEach(album, albums)
    {
        Album *const a = &(artist->albums[i]);
        a->id = strdup(cJSON_GetObjectItemCaseSensitive(album, "id")->valuestring);
        a->name = strdup(cJSON_GetObjectItemCaseSensitive(album, "name")->valuestring);
        a->number_songs = 0;
        a->songs = NULL;
        i++;
    }

    cJSON_Delete(response_root);
    free(url);
    free(response);
}

void get_albums(const Connection *conn, Database *db, const char *artist_id) {
    // Retrieve the position occupied by the artist in the database
    int artist_idx = find_artist(db, artist_id);

    Artist *artist = &(db->artists[artist_idx]);

    // Check if the artist already has album information
    if (artist->number_albums == 0)
    {
        // Retrieve album information from the server
        request_albums(conn, artist);
    }
}

void get_songs(const Connection *conn, Database *db, const char *artist_id, const char *album_id) {
    // Retrieve the position occupied by the artist in the database
    int artist_idx = find_artist(db, artist_id);
    Artist *artist = &(db->artists[artist_idx]);

    // Retrieve the position occupied by the album in the Artist struct 
    int album_idx = find_album(artist, album_id);
    Album *album = &(artist->albums[album_idx]);

    // Check if hte album already has song information
    if (album->number_songs == 0)
    {
        // Retrieve song information from the server
        request_songs(conn, album);
    }
}

int approximate_duration(const cJSON *const song)
{
    const int size = cJSON_GetObjectItemCaseSensitive(song, "size")->valueint;
    const int rate = cJSON_GetObjectItemCaseSensitive(song, "bitRate")->valueint;
    return size*8/rate/1000;
}


void request_songs(const Connection *conn, Album *album) {
    if (album->songs != NULL) {
        // If album information for this artist has already been retrieved, return
        return;
    }

    char *url;
    generate_subsonic_url(conn, SONGS, album->id, &url);

    char *response = fetch_url_data(url);
    cJSON *response_root = cJSON_Parse(response);
    cJSON *subsonic_response = cJSON_GetObjectItemCaseSensitive(response_root, "subsonic-response");

    if (strcmp("ok", cJSON_GetObjectItemCaseSensitive(subsonic_response, "status")->valuestring) != 0)
    {
        fprintf(stderr, "Error: Failed to retrieve artists from Subsonic server/\n");
        exit(EXIT_FAILURE);
    }

    cJSON *album_json = cJSON_GetObjectItemCaseSensitive(subsonic_response, "album");
    int number_songs = cJSON_GetObjectItemCaseSensitive(album_json, "songCount")->valueint;
    
    album->songs = malloc(sizeof(Song) * number_songs);
    album->number_songs = number_songs;
    cJSON *songs = cJSON_GetObjectItemCaseSensitive(album_json, "song");
    cJSON *song;
    int i = 0;
    cJSON_ArrayForEach(song, songs)
    {
        Song *s = &(album->songs[i]);
        s->id = strdup(cJSON_GetObjectItemCaseSensitive(song, "id")->valuestring);
        s->name = strdup(cJSON_GetObjectItemCaseSensitive(song, "title")->valuestring);
        s->duration = (int) cJSON_HasObjectItem(song, "duration") ? (int) cJSON_GetObjectItemCaseSensitive(song, "duration")->valueint : approximate_duration(song);
        i++;
    }

    cJSON_Delete(response_root);
    free(url);
    free(response);
}

void play_previous_next(AppState *const app_state, const int action)
{
    Playlist *playlist = app_state->playlist;
    switch (action)
    {
        case previous:
            if (playlist->current_playing > 0)
            {
                --(playlist->current_playing);
                play_song(app_state, playlist->current_playing);
            }
            break;
        case next:
            if (playlist->current_playing < playlist->size - 1)
            {
                ++(playlist->current_playing);
                play_song(app_state, playlist->current_playing);
            }
            break;
        default:
            break;
    }
    return;
}



int add_to_playlist(AppState *app_state)
{
    Playlist *playlist = app_state->playlist;
    Artist *artist = app_state->artist;
    Album *album = app_state->album;
    Song *song = NULL;
    const int first_song_to_play = playlist->size;
    const int has_songs = playlist->size == 0 ? 0 : 1;
    const PanelType current_panel = app_state->current_panel;
    switch (current_panel)
    {
        case PANEL_ARTISTS:
            for (int i = 0; i < artist->number_albums; ++i)
            {
                album = &artist->albums[i];
                get_songs(app_state->connection, app_state->db, artist->id, album->id); 
                for (int j = 0; j < album->number_songs; ++j)
                {
                    song = &album->songs[j];
                    add_song(song, playlist);
                }
            }
            break;
        case PANEL_ALBUMS:
            get_songs(app_state->connection, app_state->db, artist->id, album->id);
            for (int i = 0; i < album->number_songs; i++)
            {
                song = &album->songs[i];
                add_song(song, playlist);
            }
            break;
        case PANEL_SONGS:
            song = &album->songs[app_state->selected_song_idx];
            add_song(song, playlist);
            break;
        default:
            break;
    }
    return has_songs == 0 ? 0 : first_song_to_play; 
}

void cleanup(AppState *app_state)
{
    // Make the playlist Song pointers point to NULL to avoid problems when we delete the underlying data
    for (int i = 0; i < app_state->playlist->size; i++)
    {
        app_state->playlist->songs[i] = NULL;
    }
    free(app_state->playlist->songs);

    // Clean the database
    for (int i = 0; i < app_state->db->number_artists; i++) {
        Artist *artist = &(app_state->db->artists[i]);
        for (int j = 0; j < artist->number_albums; j++) {
            Album *album = &(artist->albums[j]);
            for (int k = 0; k < album->number_songs; k++) {
                Song *song = &(album->songs[k]);
                free(song->id);
                free(song->name);
            }
            free(album->id);
            free(album->name);
            free(album->songs);
        }
        free(artist->id);
        free(artist->name);
        free(artist->albums);
    }
    free(app_state->db->artists);
}

void print_progress_bar(WINDOW **windows, const AppState *const app_state)
{
    // Get the window to print to
    WINDOW *window = windows[0];
    const Playlist *const playlist = app_state->playlist;

    // Clear the window
    werase(window);
    if (playlist->status == STOPPED)
    {
        wrefresh(window);
        return;
    }

    // Retrieve the current song and its duration
    const Song *const song = playlist->songs[app_state->playlist->current_playing];
    const int duration = song->duration;

    // Compute the progress bar width based on the window width
    const int bar_width = getmaxx(window) - 2;  // Subtracting 2 for the borders

    // Compute the number of hashes to print in the progress bar
    const int num_hashes = (int)((double)(playlist->play_time) / (double)(duration) * (double)(bar_width));
    char *artist_name = NULL;
    char *album_name = NULL;

    if (app_state->artist && app_state->album) {
        artist_name = app_state->artist->name;
        album_name = app_state->album->name;
    }
    char *status_symbol;
    switch (app_state->playlist->shuffle_repeat_status) {
        case SHUFFLE:
            status_symbol = strdup(appearance[ind_shuffle]);
            break;
        case REPEAT:
            status_symbol = strdup(appearance[ind_repeat]);
            break;
        default:
            status_symbol = " ";
            break;
    }

    // Print the progress bar
    wprintw(window, "\n %s - %s - %s [%s]\n", artist_name, album_name, song->name, status_symbol);
    wmove(window, 2, 1);
    for (int i = 0; i < bar_width; ++i) {
        if (i < num_hashes)
        {
            waddstr(window, appearance[ind_played]);
        } else
        {
            waddstr(window, appearance[ind_unplayed]);
        }
    } 

    // Refresh the window
    wrefresh(window);
}

void update_playlist_state(AppState *app_state)
{
    Playlist *const playlist = app_state->playlist;
    // Increment index to play next song
    switch (playlist->shuffle_repeat_status)
    {
        case NONE:
            playlist->current_playing++;
            break;
        case SHUFFLE:
            playlist->current_playing = rand() % playlist->size;
            break;
        case REPEAT:
            break;
    }

    // Start playing the next song
    playlist->play_time = 0;
    playlist->start_time = time(NULL);
    play_song(app_state, playlist->current_playing);
    
    if (app_state->current_view == VIEW_PLAYLIST) {
        refresh_windows(app_state, app_state->windows[WINDOW_PLAYLIST], 1);
    }
    return;
}

/*
 * Updates the shuffle and repeat status of the playlist based on the given action.
 *
 * @param playlist Pointer to the playlist whose shuffle and repeat status should be updated.
 * @param action Character representing the action to perform (either 'shuffle' or 'repeat').
 */
void change_shuffle_repeat(Playlist *const  playlist, const char action)
{
    if (playlist == NULL) {
        return;
    }

    switch (action) {
        case shuffle:
            playlist->shuffle_repeat_status = (playlist->shuffle_repeat_status == SHUFFLE) ? NONE : SHUFFLE;
            break;
        case repeat:
            playlist->shuffle_repeat_status = (playlist->shuffle_repeat_status == REPEAT) ? NONE : REPEAT;
            break;
    }
    return;
}

/**
 * Returns the chord corresponding to a given keypress.
 *
 * @param keypress The keypress for which we want to find the chord.
 * @param chords A 2D array of chords and their corresponding keypresses.
 * @param len The length of the chords array.
 *
 * @return The chord corresponding to the given keypress, or -1 if no such chord exists.
 */
int get_chord(const int keypress)
{
    static int initialized = 0;
    static int hash_table[512] = {0}; // We use the maximum number as per ncurses definitions

    if (initialized == 0) {
        for (int i = 0; i < len_chords; i++) {
            hash_table[chords[i][0]] = chords[i][1];
        }
        initialized = 1;
    }

    if (keypress == -1) {
        return -1;
    }
        
    int chord = hash_table[keypress];
    if (chord != 0) {
        return chord;
    }

    return -1;
}

/**
 * Handles a given action and updates the application state accordingly.
 *
 * @param action The action to handle.
 * @param app_state A pointer to the current state of the application.
 */
void handle_action(const int action, AppState *app_state)
{
    if (action == -1) {
        return;
    }

    Playlist *playlist = app_state->playlist;
    WINDOW **info_windows = app_state->windows[WINDOW_INFO];
    WINDOW **playlist_windows = app_state->windows[WINDOW_PLAYLIST];

    switch (action) {
        case up:
        case down:
        case left:
        case right:
        case bottom:
        case top: {
            const SpecialMovement special_movement = 
                (action == bottom) ? MOVE_BOTTOM :
                (action == top)    ? MOVE_TOP    :
                                     NUM_MOVEMENTS;

            movement(action, app_state, special_movement);

            if (app_state->current_view == VIEW_INFO) {
                refresh_windows(app_state, info_windows, 3);
            }
            else {
                refresh_windows(app_state, playlist_windows, 1);
            }

            break;
	}
        case add_and_play:
            if (app_state->current_view == VIEW_INFO) {
                const int first_song_to_play = add_to_playlist(app_state);
                play_song(app_state, first_song_to_play);
            }
            if (app_state->current_view == VIEW_PLAYLIST) {
                play_song(app_state, playlist->selected_song_idx);
                refresh_windows(app_state, playlist_windows, 1);
            }
            break;
        case previous:
        case next:
            play_previous_next(app_state, action);
            if (app_state->current_view == VIEW_INFO) {
                refresh_windows(app_state, info_windows, NUM_WINDOWS);
            }
            if (app_state->current_view == VIEW_PLAYLIST) {
                refresh_windows(app_state, playlist_windows, 1);
            }
            break;
        case shuffle:
        case repeat:
            change_shuffle_repeat(playlist, action);
            break;
        case add:
            add_to_playlist(app_state);
            break;
        case remove_one:
            if (app_state->current_view == VIEW_PLAYLIST) {
                delete_song(app_state);
                refresh_windows(app_state, playlist_windows, 1);
            }
            break;
        case remove_all:
            if (app_state->current_view == VIEW_PLAYLIST) {
                while(playlist->size > 0) {
                    delete_song(app_state);
                }
                refresh_windows(app_state, playlist_windows, 1);
            }
            break;
        case play_pause:
            pause_resume(app_state);
            break;
        case stop:
            stop_playback(app_state);
            break;
        case main_view:
            if (app_state->current_view == VIEW_PLAYLIST) {
                endwin();
                delete_windows(playlist_windows, 1);
                playlist_windows[0] = NULL;
                app_state->current_view = VIEW_INFO;
                info_windows = create_windows(NUM_PANELS, bottom_space, WINDOW_INFO);

                for (int i = 0; i < NUM_PANELS; i++) {
                    print_window_data(app_state, i, info_windows);
                }

                app_state->windows[WINDOW_INFO] = info_windows;
            }
            break;
        case playlist_view:
            if (app_state->current_view == VIEW_INFO) {
                endwin();
                delete_windows(info_windows, NUM_PANELS);

                if (playlist_windows[0] == NULL) {
                    playlist_windows = create_windows(1, bottom_space, WINDOW_PLAYLIST);
                }
                else {
                    playlist_windows = create_windows(1, bottom_space, WINDOW_PLAYLIST);
                }

                app_state->current_view = VIEW_PLAYLIST;
                print_playlist_data(app_state, playlist_windows);
                app_state->windows[WINDOW_PLAYLIST] = playlist_windows;
                refresh();
            }
            break;
        case resize:
            endwin();
            refresh();

            if (app_state->current_view == VIEW_INFO) {
                delete_windows(info_windows, NUM_PANELS);
                info_windows = create_windows(NUM_PANELS, bottom_space, WINDOW_INFO);

                for (int i = 0; i < NUM_PANELS; i++) {
                    print_window_data(app_state, i, info_windows);
                }
                app_state->windows[WINDOW_INFO] = info_windows;
            }

            if (app_state->current_view == VIEW_PLAYLIST) {
                delete_windows(playlist_windows, 1);
                playlist_windows = create_windows(1, bottom_space, WINDOW_PLAYLIST);
                print_playlist_data(app_state, playlist_windows);
                app_state->windows[WINDOW_PLAYLIST] = playlist_windows;
            }
            break;
        case quit:
            if (app_state->playlist->status == PLAYING) {
                kill(get_pid(app_state->program), SIGTERM);
            }

            endwin();

            for (int i = 0; i < 3; i++) {
                delwin(info_windows[i]);
            }

            cleanup(app_state);
            exit(0);
            break;
        case chord: {
            const int c = getch();
            const int chord_action = get_chord(c);
            handle_action(chord_action, app_state);
            break;
        }
        default:
            break;
    }
}

int main(void) {
    setlocale(LC_ALL, "");

    setup_ncurses();
    WINDOW **playlist_windows = create_windows(1, bottom_space, WINDOW_PLAYLIST);
    WINDOW **info_windows = create_windows(NUM_PANELS, bottom_space, WINDOW_INFO);
    WINDOW **playback_windows = create_windows(1, bottom_space, WINDOW_PLAYBACK);

    Database db = init_db();
    Playlist playlist = init_playlist();
    AppState app_state = init_appstate();
    app_state.playlist = &playlist;

    get_artists(app_state.connection, &db);
    Artist *artist = &(db.artists[app_state.selected_artist_idx]);
    app_state.artist = artist;

    get_albums(app_state.connection, &db, artist->id);
    Album *album = &(artist->albums[app_state.selected_album_idx]);
    app_state.album = album;

    get_songs(app_state.connection, &db, artist->id, album->id);
    app_state.db = &db;

    app_state.windows[WINDOW_INFO] = info_windows;
    app_state.windows[WINDOW_PLAYLIST] = playlist_windows;
    app_state.windows[WINDOW_PLAYBACK ] = playback_windows;

    print_window_data(&app_state, PANEL_ARTISTS, info_windows);
    print_window_data(&app_state, PANEL_ALBUMS, info_windows);
    print_window_data(&app_state, PANEL_SONGS, info_windows);

    int action = -1;
    int c = -1;
    time_t now;
    while (1) {
        switch (playlist.status) {
            case PLAYING:
                now = (time_t) time(NULL);
                playlist.play_time += difftime(now, playlist.start_time);

                // Check if current song has finished playing
                if (playlist.current_playing < playlist.size && playlist.play_time >= playlist.songs[playlist.current_playing]->duration) {
                     update_playlist_state(&app_state); // Call play_next_song function
                }
                playlist.start_time = now;
		print_progress_bar(playback_windows, &app_state);
                break;
            case PAUSED:
                break;
            case STOPPED:
		print_progress_bar(playback_windows, &app_state);
                break;
            default:
                break;
        }
        c = getch();
        action = get_action(c);
        handle_action(action, &app_state);
    }
    return 0;
}
