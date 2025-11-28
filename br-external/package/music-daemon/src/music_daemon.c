/* music_daemon.c
 * User-space daemon for AESD final project.
 * - Reads button/encoder events from /dev/music_input
 * - Controls mpg123 to play MP3s
 * - Simple, stable playback with pause/resume and mute toggle
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

#define INPUT_DEV      "/dev/music_input"
#define MUSIC_DIR      "/usr/share/music"
#define NUM_SONGS      6

/* Song list */
static const char *playlist[] = {
    MUSIC_DIR "/Song1.mp3",
    MUSIC_DIR "/Song2.mp3",
    MUSIC_DIR "/Song3.mp3",
    MUSIC_DIR "/Song4.mp3",
    MUSIC_DIR "/Song5.mp3",
    MUSIC_DIR "/Song6.mp3"
};

static int running = 1;
static int current_song = 0;
static int current_volume = 75;
static int is_playing = 0;
static int is_paused  = 0;
static pid_t mpg_pid  = -1;

/* Mute state */
static int is_muted = 0;
static int volume_before_mute = 75;

/* simple debounce in userspace: ignore events within 200 ms */
static unsigned long last_event_ms = 0;

static void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

/* Start mpg123 in foreground for current song */
static void start_playback(void)
{
    if (mpg_pid > 0) {
        /* Already have a player process (playing or paused) */
        return;
    }

    mpg_pid = fork();
    if (mpg_pid == 0) {
        execl("/usr/bin/mpg123", "mpg123",
              "-q",
              playlist[current_song],
              (char *)NULL);
        perror("execl mpg123");
        _exit(1);
    }

    is_playing = 1;
    is_paused  = 0;
    printf("music_daemon: playing song %d: %s\n",
           current_song, playlist[current_song]);
}

/* Stop playback completely */
static void stop_playback(void)
{
    if (mpg_pid > 0) {
        kill(mpg_pid, SIGTERM);
        waitpid(mpg_pid, NULL, 0);
        mpg_pid = -1;
    }
    is_playing = 0;
    is_paused  = 0;
    printf("music_daemon: stopped\n");
}

/* Button logic: play/pause with resume */
static void handle_playpause(void)
{
    int status;

    /* First, see if the player already exited */
    if (mpg_pid > 0) {
        pid_t r = waitpid(mpg_pid, &status, WNOHANG);
        if (r == mpg_pid) {
            /* Child is gone, reset state */
            mpg_pid    = -1;
            is_playing = 0;
            is_paused  = 0;
        }
    }

    /* If nothing is running now, just start playback from beginning */
    if (mpg_pid <= 0) {
        start_playback();
        return;
    }

    /* We have a live mpg123 process: do real pause/resume */
    if (is_playing && !is_paused) {
        /* Pause in place */
        if (kill(mpg_pid, SIGSTOP) == 0) {
            is_paused  = 1;
            is_playing = 0;
            printf("music_daemon: paused\n");
        } else {
            /* If pause fails, treat as dead and start fresh */
            perror("SIGSTOP mpg123");
            mpg_pid    = -1;
            is_playing = 0;
            is_paused  = 0;
            start_playback();
        }
    } else if (is_paused) {
        /* Resume from where we paused */
        if (kill(mpg_pid, SIGCONT) == 0) {
            is_paused  = 0;
            is_playing = 1;
            printf("music_daemon: resumed\n");
        } else {
            /* If resume fails, restart song from beginning */
            perror("SIGCONT mpg123");
            mpg_pid    = -1;
            is_playing = 0;
            is_paused  = 0;
            start_playback();
        }
    } else {
        /* Fallback: start playback if our flags somehow got inconsistent */
        start_playback();
    }
}

static void handle_next(void)
{
    stop_playback();
    current_song = (current_song + 1) % NUM_SONGS;
    printf("music_daemon: next -> song %d\n", current_song);
    start_playback();
}

static void handle_prev(void)
{
    stop_playback();
    current_song = (current_song == 0) ? (NUM_SONGS - 1)
                                       : (current_song - 1);
    printf("music_daemon: prev -> song %d\n", current_song);
    start_playback();
}

/* Hardware volume via amixer */
static void set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;

    current_volume = v;

    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "amixer -c 0 sset 'PCM' %d%% >/dev/null 2>&1", v);
    system(cmd);

    printf("music_daemon: volume=%d%%\n", v);
}

static void volume_up(void)
{
    set_volume(current_volume + 5);
}

static void volume_down(void)
{
    set_volume(current_volume - 5);
}

/* Encoder button: mute toggle */
static void toggle_mute(void)
{
    if (!is_muted) {
        volume_before_mute = current_volume;
        set_volume(0);
        is_muted = 1;
        printf("music_daemon: muted\n");
    } else {
        set_volume(volume_before_mute);
        is_muted = 0;
        printf("music_daemon: unmuted\n");
    }
}

int main(void)
{
    int fd = open(INPUT_DEV, O_RDONLY);
    if (fd < 0) {
        perror("open /dev/music_input");
        return 1;
    }

    printf("music_daemon: started (pause/resume mode)\n");

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    set_volume(current_volume);

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    char ev;

    while (running) {
        int r = poll(&pfd, 1, 200);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        if (r == 0)
            continue;

        if (pfd.revents & POLLIN) {
            if (read(fd, &ev, 1) == 1) {

                /* debounce all events: ignore anything within 200ms */
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                unsigned long now_ms =
                    ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL;

                if (now_ms - last_event_ms < 200) {
                    continue;
                }
                last_event_ms = now_ms;

                switch (ev) {
                case 'P': handle_playpause(); break;
                case 'N': handle_next();      break;
                case 'R': handle_prev();      break;
                case 'U': volume_up();        break;
                case 'D': volume_down();      break;
                case 'M': toggle_mute();      break;
                default:
                    printf("music_daemon: unknown event %c\n", ev);
                }
            }
        }
    }

    stop_playback();
    close(fd);
    printf("music_daemon: exiting cleanly\n");
    return 0;
}
