#define URL ""
#define PORT
#define USER ""
#define PWD ""
#define VERSION ""
#define APP ""
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

enum { INACTIVE, ACTIVE, NUM_COLORS };
static const short colors[NUM_COLORS][2] = {
    /*               fg, bg */
    [INACTIVE] =    {COLOR_BLACK,  COLOR_WHITE},
    [ACTIVE]   =    {COLOR_WHITE,  COLOR_RED},
};

enum { ind_playing, ind_repeat, ind_shuffle, ind_played, ind_unplayed };
static const char *appearance[5] = {
    ">", /* Playing indicator in the playlist */
    "R", /* Playing indicator, Repeat mode */
    "X", /* Playing indicator, Shuffle mode */
    "#", /* Played time */
    "-"  /* Unplayed time */
};

const unsigned int bottom_space = 4;

/* Actions */
enum { play_pause, stop, next, previous, repeat, shuffle, quit, add, 
       add_and_play, remove_one, remove_all, main_view, playlist_view, up, down,
       left, right, resize, bottom, top, chord
};

static const int keys[][2] = {
    /* Keybinding,      Action */
    {'p',               play_pause},
    {'s',               stop},
    {'>',               next},
    {'<',               previous},
    {'r',               repeat},
    {'x',               shuffle},
    {'q',               quit},
    {' ',               add},
    {10 ,               add_and_play},
    {KEY_ENTER,         add_and_play},
    {'d',               remove_one},
    {'c',               remove_all},
    {'1',               main_view},
    {'2',               playlist_view},
    {KEY_UP,            up},
    {KEY_DOWN,          down},
    {KEY_LEFT,          left},
    {KEY_RIGHT,         right},
    {KEY_RESIZE,        resize},
    {'G',               bottom},
    {'g',               chord},
};

enum { chord_top };
static const int chords[][2] = {
    /* Chord,           Action */
    {'g',               top},
};
static const int len_keys = sizeof(keys)/sizeof(keys[0]);
static const int len_chords = sizeof(chords)/sizeof(chords[0]);

// Define the variable and flags to use for playback
static char *const executable = "ffplay";
static char *const flags = "-nodisp -autoexit";

// Define the variable to use for notification
// Use NULL if this is unwanted
static char *const notify_cmd = NULL;

// Location where AppState is dumped at every song change
// Other programs can read that file and display the information
// Use NULL if this is unwanted
static char *const state_dump = NULL;
