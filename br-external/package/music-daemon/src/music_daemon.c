/* music_daemon.c
 * AESD Final Project 
 * Local MP3 playback
 * Cloud streaming mode
 * HDMI Text UI (TTY1)
 * Socket Programming Control (HTTP server on port 8888)
 * FINAL STABLE VERSION – DEC 1
 * TEST_STRING: FINAL_BUILD_999
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

#define INPUT_DEV      "/dev/music_input"
#define MUSIC_DIR      "/usr/share/music"
#define NUM_SONGS      5
#define PORT           8888

/* ------------------------------------------------------- */
/*                   LOCAL SONG LIST                       */
/* ------------------------------------------------------- */

static const char *playlist[] = {
    MUSIC_DIR "/RunitUp.mp3",
    MUSIC_DIR "/BeatIt.mp3",
    MUSIC_DIR "/ShapeofYou.mp3",
    MUSIC_DIR "/Gasolina.mp3",
    MUSIC_DIR "/RapGod.mp3"
};

/* Local titles */
static const char *local_title[] = {
    "Run-it-Up",
    "Beat-it",
    "Shape-of-You",
    "Gasolina",
    "Rap-God",
};

/* Local Artists */
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

static const char *cloud_url[] = {
    "https://prudhvibelide.github.io/cloud-music-list/songs/Starboy.mp3",
    "https://prudhvibelide.github.io/cloud-music-list/songs/FEIN.mp3",
    "https://prudhvibelide.github.io/cloud-music-list/songs/HeatWaves.mp3",
    "https://prudhvibelide.github.io/cloud-music-list/songs/Sorry.mp3",
    "https://prudhvibelide.github.io/cloud-music-list/songs/STAY.mp3"
};

static const char *cloud_title[] = {
    "Starboy – The Weeknd",
    "FEIN – Travis Scott",
    "Heat Waves – Glass Animals",
    "Sorry – Justin Bieber",
    "STAY – The Kid LAROI & Justin Bieber"
};

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

static int running = 1;
static int current_song = 0;
static int current_volume = 75;

static const char *build_tag = "Music Daemon Build: FINAL_BUILD_999";

static int is_playing = 0;
static int is_muted = 0;
static int is_cloud = 0;      /* 0 = Local, 1 = Cloud */

static pid_t mpg_pid = -1;
static FILE *display_fp = NULL;

static int volume_before_mute = 75;
static unsigned long last_event_ms = 0;

/* ------------------------------------------------------- */
/*             TEXT DISPLAY ON HDMI (TTY1)                 */
/* ------------------------------------------------------- */

static void init_display(void)
{
    if (!display_fp) {
        display_fp = fopen("/dev/tty1", "w");
        if (!display_fp)
            display_fp = stdout;
    }
}

static const char *get_title(void)
{
    return is_cloud ? cloud_title[current_song % 5]
                    : local_title[current_song];
}

static const char *mode_text(void)
{
    return is_cloud ? "Cloud Mode" : "Local Mode";
}

static const char *status_text(void)
{
    return (mpg_pid > 0) ? "Playing" : "Stopped";
}

static void draw_status(const char *extra)
{
    init_display();

    fprintf(display_fp, "\033[2J\033[H");  /* Clear screen */

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

static void kill_all_players(void)
{
    (void)system("killall -q mpg123 2>/dev/null || true");
}

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

static void volume_up(void)   { set_volume(current_volume + 5); }
static void volume_down(void) { set_volume(current_volume - 5); }

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

static void start_playback(void)
{
    if (mpg_pid > 0)
        return;

    kill_all_players();

    mpg_pid = fork();
    if (mpg_pid == 0) {

        /* child: audio only, close extra FDs */
        for (int i = 3; i < 256; i++)
            close(i);

        (void)freopen("/dev/null", "r", stdin);

        if (!is_cloud) {
            execl("/usr/bin/mpg123", "mpg123", "-q",
                  playlist[current_song], NULL);
        } else {
            draw_status("Downloading from GitHub…");

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

static void handle_playpause(void)
{
    if (is_playing)
        stop_playback();
    else
        start_playback();
}

static void handle_next(void)
{
    stop_playback();
    current_song = (current_song + 1) % NUM_SONGS;
    start_playback();
}

static void handle_prev(void)
{
    stop_playback();
    current_song = (current_song == 0) ? NUM_SONGS - 1 : current_song - 1;
    start_playback();
}

/* ORIGINAL COMMENT KEPT */
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

/* simple HTTP request handler with /local socket demo */
static void handle_http_request(int fd)
{
    char buf[1024];
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        return;

    buf[n] = '\0';

    /* debug endpoint */
    if (strncmp(buf, "GET /test", 9) == 0) {
        send_response(fd, "OK\n");
        return;
    }

    /* basic remote controls (no playlist change) */
    if (strncmp(buf, "GET /play", 9) == 0)          handle_playpause();
    else if (strncmp(buf, "GET /pause", 10) == 0)    handle_playpause();
    else if (strncmp(buf, "GET /next", 9) == 0)      handle_next();
    else if (strncmp(buf, "GET /prev", 9) == 0)      handle_prev();
    else if (strncmp(buf, "GET /vol_up", 11) == 0)   volume_up();
    else if (strncmp(buf, "GET /vol_down", 13) == 0) volume_down();
    else if (strncmp(buf, "GET /mute", 9) == 0)      toggle_mute();
    else if (strncmp(buf, "GET /mode", 9) == 0)      toggle_mode();

    /* SOCKET DEMO: /local?song=N → play SD-card track N (direct mpg123) */
   /* else if (strncmp(buf, "GET /local", 10) == 0) {

        int id = 0;
        char *p = strstr(buf, "song=");

        if (p) {
            id = atoi(p + 5);
            if (id < 0 || id >= NUM_SONGS)
                id = 0;
        }

        is_cloud = 0;
        current_song = id;
        is_playing = 1;
        last_event_ms = 0;

        draw_status("HTTP: Local song request");

        // kill any existing audio and play selected track via system() 
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "/usr/bin/killall -q mpg123 2>/dev/null; "
                 "/usr/bin/mpg123 -q \"%s\" &",
                 playlist[id]);

        (void)system(cmd);

        send_response(fd, "OK\n");
        return;
    }*/
    
  
  else if (strncmp(buf, "GET /local", 10) == 0) {
    int id = 0;
    char *p = strstr(buf, "song=");

    if (p) {
        id = atoi(p + 5);
        if (id < 0 || id >= NUM_SONGS)
            id = 0;
    }

    /* Make this a normal LOCAL playback request */
    is_cloud = 0;          /* force local / SD-card mode        */
    current_song = id;     /* update internal index for buttons */
    last_event_ms = 0;     /* don't let debounce block this     */

    /* Stop any current playback and start the chosen track */
    stop_playback();
    start_playback();

    /* Update HDMI text to say this came from socket */
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

    /* fallback */
    send_response(fd, "OK\n");
}

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
/*                          MAIN                            */
/* ------------------------------------------------------- */

int main(void)
{
    int fd = open(INPUT_DEV, O_RDONLY);
    if (fd < 0) { perror("open /dev/music_input"); return 1; }

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    set_volume(current_volume);
    draw_status("Idle");

    start_http_server();

    struct pollfd pfd[2];
    pfd[0].fd = fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = server_fd;
    pfd[1].events = POLLIN;

    char ev;

    while (running) {
        int r = poll(pfd, 2, 200);
        if (r < 0) continue;

        /* Physical buttons */
        if (pfd[0].revents & POLLIN) {
            if (read(fd, &ev, 1) == 1) {

                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                unsigned long now = ts.tv_sec * 1000 + ts.tv_nsec/1000000UL;

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

        /* HTTP client */
        if (pfd[1].revents & POLLIN) {
            int cfd = accept(server_fd, NULL, NULL);
            if (cfd >= 0) {
                handle_http_request(cfd);
                close(cfd);
            }
        }
    }

    stop_playback();
    close(fd);
    if (display_fp != stdout) fclose(display_fp);

    return 0;
}

