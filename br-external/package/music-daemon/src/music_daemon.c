/* music_daemon.c
 * User-space daemon for AESD final project.
 * - Reads button events from /dev/music_input.
 * - Sends notifications over Unix domain socket.
 * - Controls mpg123 playback for songs
 * - mpg123 remote control for pause/play(resume)/prev/next
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>


#define EVENT_DEBOUNCE_SEC 1

#define INPUT_DEV      "/dev/music_input"
#define SOCKET_PATH    "/tmp/musicd.sock"
#define MUSIC_DIR      "/usr/share/music"
#define NUM_SONGS      6

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
static pid_t mpg123_pid = -1;
static int mpg123_stdin = -1;
static int is_playing = 0;
static int is_paused = 0;

static void handle_sigint(int sig)
{
	(void)sig;
	running = 0;
}

static void sigchld_handler(int sig)
{
	int status;
	pid_t pid;
	
	(void)sig;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (pid == mpg123_pid) {
			if (mpg123_stdin >= 0) {
				close(mpg123_stdin);
				mpg123_stdin = -1;
			}
			mpg123_pid = -1;
			is_playing = 0;
			is_paused = 0;
			printf("music_daemon: song finished\n");
			/* Auto-advance */
			current_song = (current_song + 1) % NUM_SONGS;
		}
	}
}

static int setup_unix_socket(void)
{
	int fd;
	struct sockaddr_un addr;

	unlink(SOCKET_PATH);

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}

	chmod(SOCKET_PATH, 0666);
	return fd;
}

static void notify_clients(int sockfd, const char *msg)
{
	if (sockfd < 0)
		return;
	sendto(sockfd, msg, strlen(msg), 0, NULL, 0);
}

static void send_mpg123_command(const char *cmd)
{
	if (mpg123_stdin < 0 || mpg123_pid <= 0)
		return;
	
	write(mpg123_stdin, cmd, strlen(cmd));
	write(mpg123_stdin, "\n", 1);
}

static void stop_playback(void)
{
	if (mpg123_pid > 0) {
		send_mpg123_command("QUIT");
		sleep(1); /* Give it time to quit gracefully */
		kill(mpg123_pid, SIGTERM);
		waitpid(mpg123_pid, NULL, 0);
		mpg123_pid = -1;
	}
	if (mpg123_stdin >= 0) {
		close(mpg123_stdin);
		mpg123_stdin = -1;
	}
	is_playing = 0;
	is_paused = 0;
}

static void start_playback(void)
{
	int pipe_fd[2];
	
	if (mpg123_pid > 0)
		return; /* Already running */
	
	if (pipe(pipe_fd) < 0) {
		perror("pipe");
		return;
	}
	
	mpg123_pid = fork();
	if (mpg123_pid == 0) {
		/* Child process */
		dup2(pipe_fd[0], STDIN_FILENO);
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		
		execl("/usr/bin/mpg123", "mpg123", 
		      "-R",  /* Remote control mode */
		      "-a", "hw:0,0",
		      (char *)NULL);
		perror("execl mpg123 failed");
		exit(1);
	}
	
	/* Parent */
	close(pipe_fd[0]);
	mpg123_stdin = pipe_fd[1];
	
	/* Load and play the song */
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "LOAD %s", playlist[current_song]);
	send_mpg123_command(cmd);
	
	is_playing = 1;
	is_paused = 0;
	printf("music_daemon: playing %s\n", playlist[current_song]);
}

static void pause_playback(void)
{
	if (mpg123_pid <= 0 || !is_playing)
		return;
	
	send_mpg123_command("PAUSE");
	is_paused = 1;
	printf("music_daemon: paused\n");
}

static void resume_playback(void)
{
	if (mpg123_pid <= 0)
		return;
	
	if (is_paused) {
		send_mpg123_command("PAUSE"); /* Toggle pause off */
		is_paused = 0;
		printf("music_daemon: resumed\n");
	}
}

static void handle_play_pause(int sockfd)
{
	if (mpg123_pid <= 0) {
		/* Not running, start playback */
		start_playback();
		notify_clients(sockfd, "PLAYING\n");
	} else if (is_paused) {
		/* Paused, resume */
		resume_playback();
		notify_clients(sockfd, "PLAYING\n");
	} else {
		/* Playing, pause */
		pause_playback();
		notify_clients(sockfd, "PAUSED\n");
	}
}

static void handle_next(int sockfd)
{
	stop_playback();
	current_song = (current_song + 1) % NUM_SONGS;
	start_playback();
	notify_clients(sockfd, "NEXT_SONG\n");
}

static void handle_prev(int sockfd)
{
	stop_playback();
	if (current_song == 0)
		current_song = NUM_SONGS - 1;
	else
		current_song = current_song - 1;
	start_playback();
	notify_clients(sockfd, "PREV_SONG\n");
}

int main(void)
{
	int fd_input;
	int sockfd;
	struct pollfd pfds[1];
	char event;
	time_t last_event_time = 0;

	signal(SIGINT, handle_sigint);
	signal(SIGTERM, handle_sigint);
	signal(SIGCHLD, sigchld_handler);

	fd_input = open(INPUT_DEV, O_RDONLY);
	if (fd_input < 0) {
		perror("open " INPUT_DEV);
		return 1;
	}

	sockfd = setup_unix_socket();

	printf("music_daemon: started, listening on %s and %s\n",
	       INPUT_DEV, SOCKET_PATH);

	pfds[0].fd = fd_input;
	pfds[0].events = POLLIN;

	while (running) {
		int ret = poll(pfds, 1, 1000);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			break;
		}

		if (ret == 0) {
			if (!is_playing && mpg123_pid == -1) {
				start_playback();
			}
			continue;
		}

		if (pfds[0].revents & POLLIN) {
			ret = read(fd_input, &event, 1);
			if (ret == 1) {
				time_t now = time(NULL);
				if (now - last_event_time < 1) {
					printf("music_daemon: ignoring rapid press\n");
					continue;
				}
				last_event_time = now;

				printf("music_daemon: button event '%c'\n", event);
				notify_clients(sockfd, "BUTTON_EVENT\n");

				switch (event) {
				case 'P':
					handle_play_pause(sockfd);
					break;
				case 'N':
					handle_next(sockfd);
					break;
				case 'R':
					handle_prev(sockfd);
					break;
				default:
					printf("music_daemon: unknown event\n");
				}
			}
		}
	}

	printf("music_daemon: shutting down\n");
	stop_playback();
	if (sockfd >= 0) {
		close(sockfd);
		unlink(SOCKET_PATH);
	}
	close(fd_input);
	return 0;
}
