/*
 * music_daemon.c
 *
 * AESD Final Project – Raspberry Pi Music Player Daemon
 *
 * Features:
 *   - Local MP3 playback from SD card
 *   - Cloud streaming mode (HTTP streaming of MP3s)
 *   - HDMI text-based UI on TTY1
 *   - HTTP remote control interface on port 8888
 *
 * Build/Author information for demo/debug:
 *   - FINAL STABLE VERSION – DEC 2
 *   - AUTHOR : PRUDHVI RAJ BELIDE
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ------------------------------------------------------- */
/*                        CONSTANTS                        */
/* ------------------------------------------------------- */

#define INPUT_DEV      "/dev/music_input"   /* Character device for physical button input */
#define MUSIC_DIR      "/usr/share/music"   /* Base directory for local MP3 files        */
#define NUM_SONGS      5                    /* Number of local songs in the playlist     */
#define PORT           8888                 /* HTTP control port for remote interface    */

/* ------------------------------------------------------- */
/*                   LOCAL SONG LIST                       */
/* ------------------------------------------------------- */

/* Absolute paths to local MP3 files stored on the Pi */
static const char *playlist[] = {
    MUSIC_DIR "/RunitUp.mp3",
    MUSIC_DIR "/BeatIt.mp3",
    MUSIC_DIR "/ShapeofYou.mp3",
    MUSIC_DIR "/Gasolina.mp3",
    MUSIC_DIR "/RapGod.mp3"
};

/* User-friendly local song titles */
static const char *local_title[] = {
    "Run-it-Up",
    "Beat-it",
    "Shape-of-You",
    "Gasolina",
    "Rap-God",
};

/* Local artist names matching the titles above */
static const char *local_artist[] = {
    "Hanumand Kind",
    "Michael Jackson",
    "Ed Sheeran",
    "Yankee",
    "Eminem",
};


/* ------------------------------------------------------- */
/*                   CLOUD SONG LIST                       */
/* ------------------------------------------------------- */

/* HTTP URLs for cloud-streamed tracks (hosted on GitHub Pages) */
static const char *cloud_url[] = {
    "https://prudhvibelide.github.io/cloud-music-list/songs/Starboy.mp3",
    "https://prudhvibelide.github.io/cloud-music-list/songs/FEIN.mp3",
    "https://prudhvibelide.github.io/cloud-music-list/songs/HeatWaves.mp3",
    "https://prudhvibelide.github.io/cloud-music-list/songs/Sorry.mp3",
    "https://prudhvibelide.github.io/cloud-music-list/songs/STAY.mp3"
};

/* Display names for cloud songs */
static const char *cloud_title[] = {
    "Starboy – The Weeknd",
    "FEIN – Travis Scott",
    "Heat Waves – Glass Animals",
    "Sorry – Justin Bieber",
    "STAY – The Kid LAROI & Justin Bieber"
};

/* Cloud artist names, aligned with cloud_url/cloud_title */
static const char *cloud_artist[] = {
    "The Weeknd",
    "Travis Scott",
    "Glass Animals",
    "Justin Bieber",
    "The Kid LAROI & Justin Bieber"
};

/* ------------------------------------------------------- */
/*                   RUNTIME STATE                         */
/* ------------------------------------------------------- */

/* Global state variables controlling playback and UI */
static int running = 1;                /* Main loop flag */
static int current_song = 0;           /* Index into local/cloud playlist */
static int current_volume = 75;        /* Volume percentage (0–100) */

static const char *build_tag = "Music Daemon Build: FINAL_BUILD_999";

static int is_playing = 0;             /* 1 = playback active, 0 = stopped */
static int is_muted = 0;               /* Logical mute state flag */
static int is_cloud = 0;               /* 0 = Local mode, 1 = Cloud streaming mode */

static pid_t mpg_pid = -1;             /* Child process running mpg123 */
static FILE *display_fp = NULL;        /* Output stream for HDMI text UI (TTY1 or stdout) */

static int volume_before_mute = 75;    /* Volume snapshot saved when mute is enabled */
static unsigned long last_event_ms = 0;/* Timestamp used for button debounce (ms) */

/* ------------------------------------------------------- */
/*             TEXT DISPLAY ON HDMI (TTY1)                 */
/* ------------------------------------------------------- */

/* Lazily open TTY1 for display output; fallback to stdout if unavailable */
static void init_display(void)
{
    if (!display_fp) {
        display_fp = fopen("/dev/tty1", "w");
        if (!display_fp)
            display_fp = stdout;
    }
}

/* Return the current song title based on mode and index */
static const char *get_title(void)
{
    return is_cloud ? cloud_title[current_song % 5]
                    : local_title[current_song];
}

/* Human-readable playback mode string */
static const char *mode_text(void)
{
    return is_cloud ? "Cloud Mode" : "Local Mode";
}

/* Human-readable playback status string */
static const char *status_text(void)
{
    return (mpg_pid > 0) ? "Playing" : "Stopped";
}

/* Clear and redraw the HDMI status UI with optional extra status text */
static void draw_status(const char *extra)
{
    init_display();

    fprintf(display_fp, "\033[2J\033[H");  /* Clear screen and move cursor home */

    fprintf(display_fp, "=============================================\n");
    fprintf(display_fp, "         RASPBERRY PI MUSIC PLAYER           \n");
    fprintf(display_fp, "=============================================\n\n");

    fprintf(display_fp, "  SONG      : %s\n", get_title());
    fprintf(display_fp, "  NUMBER    : %d / %d\n", current_song + 1, NUM_SONGS);
    fprintf(display_fp, "  MODE      : %s\n", mode_text());
    fprintf(display_fp, "  STATUS    : %s\n", extra ? extra : status_text());
    fprintf(display_fp, "  VOLUME    : %d%%\n\n", current_volume);

    if (!is_cloud)
        fprintf(display_fp, "  ARTIST    : %s\n", local_artist[current_song]);
    else
        fprintf(display_fp, "  ARTIST    : %s\n", cloud_artist[current_song % 5]);

    fprintf(display_fp, "\n  INFO      : %s\n\n", extra ? extra : build_tag);

    fprintf(display_fp, "---------------------------------------------\n");
    fprintf(display_fp, "  CONTROLS (PHYSICAL)\n");
    fprintf(display_fp, "   P = Play/Pause\n");
    fprintf(display_fp, "   N = Next Song\n");
    fprintf(display_fp, "   R = Previous Song\n");
    fprintf(display_fp, "   U = Volume Up\n");
    fprintf(display_fp, "   D = Volume Down\n");
    fprintf(display_fp, "   M = Mute Toggle\n");
    fprintf(display_fp, "   C = Cloud/Local Toggle\n");
    fprintf(display_fp, "---------------------------------------------\n");

    fprintf(display_fp, "  REMOTE:  http://<pi-ip>:%d\n", PORT);
    fprintf(display_fp, "---------------------------------------------\n");

    fflush(display_fp);
}

/* ------------------------------------------------------- */
/*                INTERNAL AUDIO HELPERS                   */
/* ------------------------------------------------------- */

/* Best-effort kill of any mpg123 processes that might still be running */
static void kill_all_players(void)
{
    (void)system("killall -q mpg123 2>/dev/null || true");
}

/* Clamp and apply volume to ALSA via amixer, then update UI */
static void set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    current_volume = v;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "amixer -c 0 sset 'PCM' %d%% >/dev/null", v);
    (void)system(cmd);

    draw_status("Volume changed");
}

/* Relative volume controls used by buttons and HTTP API */
static void volume_up(void)   { set_volume(current_volume + 5); }
static void volume_down(void) { set_volume(current_volume - 5); }

/* Toggle mute while remembering the previous volume level */
static void toggle_mute(void)
{
    if (!is_muted) {
        volume_before_mute = current_volume;
        set_volume(0);
        is_muted = 1;
        draw_status("Muted");
    } else {
        set_volume(volume_before_mute);
        is_muted = 0;
        draw_status("Unmuted");
    }
}

/* ------------------------------------------------------- */
/*                    PLAYBACK CONTROL                     */
/* ------------------------------------------------------- */

/* Stop current playback process (if any) and clean up state */
static void stop_playback(void)
{
    if (mpg_pid > 0) {
        kill(mpg_pid, SIGTERM);
        waitpid(mpg_pid, NULL, 0);
        mpg_pid = -1;
    }
    kill_all_players();
    is_playing = 0;
    draw_status("Stopped");
}

/* Fork and start mpg123 for either local or cloud audio source */
static void start_playback(void)
{
    if (mpg_pid > 0)
        return;

    kill_all_players();

    mpg_pid = fork();
    if (mpg_pid == 0) {

        /* Child process: audio playback only, close inherited FDs */
        for (int i = 3; i < 256; i++)
            close(i);

        (void)freopen("/dev/null", "r", stdin);

        if (!is_cloud) {
            execl("/usr/bin/mpg123", "mpg123", "-q",
                  playlist[current_song], NULL);
        } else {
            draw_status("Downloading from GitHub…");

            /* Stream MP3 over HTTP using wget and pipe into mpg123 */
            char cmd[600];
            snprintf(cmd, sizeof(cmd),
                     "/usr/bin/wget -qO- \"%s\" | /usr/bin/mpg123 -q -",
                     cloud_url[current_song % 5]);

            execl("/bin/sh", "sh", "-c", cmd, NULL);
        }

        perror("exec failed");
        _exit(1);
    }

    is_playing = 1;
    draw_status("Playing");
}

/* Play/pause toggle used by both buttons and HTTP API */
static void handle_playpause(void)
{
    if (is_playing)
        stop_playback();
    else
        start_playback();
}

/* Advance to the next track in the list and start playback */
static void handle_next(void)
{
    stop_playback();
    current_song = (current_song + 1) % NUM_SONGS;
    start_playback();
}

/* Go back to the previous track and start playback */
static void handle_prev(void)
{
    stop_playback();
    current_song = (current_song == 0) ? NUM_SONGS - 1 : current_song - 1;
    start_playback();
}

/* Toggle between local and cloud mode and keep index in range */
static void toggle_mode(void)
{
    is_cloud = !is_cloud;

    stop_playback();

    if (is_cloud)
        current_song = current_song % 5;
    else
        current_song = current_song % NUM_SONGS;

    start_playback();

    draw_status("Mode changed");
}

/* ------------------------------------------------------- */
/*              SOCKET PROGRAMMING: HTTP SERVER            */
/* ------------------------------------------------------- */

static int server_fd = -1;

/* Send a simple text-based HTTP 200 response with CORS enabled */
static void send_response(int fd, const char *msg)
{
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %zu\r\n\r\n%s",
        strlen(msg), msg);
    send(fd, header, strlen(header), 0);
}

/* Serve a minimal HTML control page for testing in a browser */
static void send_html(int fd)
{
    const char *html =
        "<html><body><h1>Pi Music Remote</h1>"
        "<button onclick='fetch(\"/play\")'>Play/Pause</button><br>"
        "<button onclick='fetch(\"/next\")'>Next</button><br>"
        "<button onclick='fetch(\"/prev\")'>Prev</button><br>"
        "<button onclick='fetch(\"/vol_up\")'>Vol +</button><br>"
        "<button onclick='fetch(\"/vol_down\")'>Vol -</button><br>"
        "<button onclick='fetch(\"/mute\")'>Mute</button><br>"
        "<button onclick='fetch(\"/mode\")'>Toggle Local/Cloud</button><br>"
        "</body></html>";

    char resp[3000];
    snprintf(resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length:%zu\r\n\r\n%s",
        strlen(html), html);

    send(fd, resp, strlen(resp), 0);
}

/* Basic HTTP parser that maps paths to player control actions */
static void handle_http_request(int fd)
{
    char buf[1024];
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        return;

    buf[n] = '\0';

    /* Lightweight debug endpoint to test connectivity */
    if (strncmp(buf, "GET /test", 9) == 0) {
        send_response(fd, "OK\n");
        return;
    }

    /* Map HTTP paths to transport and playback operations */
    if (strncmp(buf, "GET /play", 9) == 0)          handle_playpause();
    else if (strncmp(buf, "GET /pause", 10) == 0)    handle_playpause();
    else if (strncmp(buf, "GET /next", 9) == 0)      handle_next();
    else if (strncmp(buf, "GET /prev", 9) == 0)      handle_prev();
    else if (strncmp(buf, "GET /vol_up", 11) == 0)   volume_up();
    else if (strncmp(buf, "GET /vol_down", 13) == 0) volume_down();
    else if (strncmp(buf, "GET /mute", 9) == 0)      toggle_mute();
    else if (strncmp(buf, "GET /mode", 9) == 0)      toggle_mode();

    /*
     * HTTP endpoint: /local?song=N
     * Switches to local mode and starts playing the requested track index N.
     * Demonstrates socket-based control that integrates cleanly with
     * the existing state machine (buttons + HTTP share the same path).
     */
  else if (strncmp(buf, "GET /local", 10) == 0) {
    int id = 0;
    char *p = strstr(buf, "song=");

    if (p) {
        id = atoi(p + 5);
        if (id < 0 || id >= NUM_SONGS)
            id = 0;
    }

    /* Treat /local as a normal local playback request through the daemon */
    is_cloud = 0;          /* Force local mode (SD-card / local playlist)     */
    current_song = id;     /* Update internal index so physical controls work */
    last_event_ms = 0;     /* Reset debounce window for immediate response    */

    /* Use the existing stop/start helpers for a clean transition */
    stop_playback();
    start_playback();

    /* Indicate on HDMI that this action was triggered via HTTP socket */
    draw_status("SOCKET: Playing local song via /local");

    char resp[256];
   snprintf(resp, sizeof(resp),
    "TCP SOCKET SUCCESS:\n"
    " → Raspberry Pi is now playing LOCAL track %d (%s).\n"
    " → Triggered via /local?song=%d over HTTP.\n",
    current_song,
    local_title[current_song],
    current_song);

  send_response(fd, resp);


    return;
}

 

    else if (strncmp(buf, "GET / ", 6) == 0) {
        send_html(fd);
        return;
    }

    /* Default response for unrecognized paths */
    send_response(fd, "OK\n");
}

/* Create and configure a simple blocking HTTP server socket */
static void start_http_server(void)
{
    struct sockaddr_in addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        { perror("bind"); return; }

    if (listen(server_fd, 5) < 0)
        { perror("listen"); return; }

    printf("HTTP server running on port %d\n", PORT);
}

/* ------------------------------------------------------- */
/*                          MAIN                           */
/* ------------------------------------------------------- */

int main(void)
{
    /* Open the input device that delivers physical button events */
    int fd = open(INPUT_DEV, O_RDONLY);
    if (fd < 0) { perror("open /dev/music_input"); return 1; }

    /* Allow default handling for SIGINT/SIGTERM (systemd or shell can stop us) */
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    /* Initialize audio and user interface state */
    set_volume(current_volume);
    draw_status("Idle");

    /* Spin up the HTTP control server (non-blocking via poll) */
    start_http_server();

    struct pollfd pfd[2];
    pfd[0].fd = fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = server_fd;
    pfd[1].events = POLLIN;

    char ev;

    while (running) {
        /* Wait for either a button press or an incoming HTTP connection */
        int r = poll(pfd, 2, 200);
        if (r < 0) continue;

        /* Handle physical button input from /dev/music_input */
        if (pfd[0].revents & POLLIN) {
            if (read(fd, &ev, 1) == 1) {

                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                unsigned long now = ts.tv_sec * 1000 + ts.tv_nsec/1000000UL;

                /* Simple software debounce: ignore events that are too close */
                if (now - last_event_ms < 200) continue;
                last_event_ms = now;

                switch (ev) {
                    case     'P': handle_playpause(); break;
                    case     'N': handle_next(); break;
                    case     'R': handle_prev(); break;
                    case     'U': volume_up(); break;
                    case     'D': volume_down(); break;
                    case     'M': toggle_mute(); break;
                    case     'C': toggle_mode(); break;
                    default:      break;
                }
            }
        }

        /* Handle new HTTP clients on the control port */
        if (pfd[1].revents & POLLIN) {
            int cfd = accept(server_fd, NULL, NULL);
            if (cfd >= 0) {
                handle_http_request(cfd);
                close(cfd);
            }
        }
    }

    /* Clean shutdown: stop playback, close devices, and release resources */
    stop_playback();
    close(fd);
    if (display_fp != stdout) fclose(display_fp);

    return 0;
}

