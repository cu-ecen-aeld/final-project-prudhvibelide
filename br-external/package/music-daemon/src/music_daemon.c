/* music_daemon.c
 * User-space daemon for AESD final project.
 * - Reads button events from /dev/music_input.
 * - Sends simple notifications over a Unix domain socket.
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

#define INPUT_DEV      "/dev/music_input"
#define SOCKET_PATH    "/tmp/musicd.sock"

static int running = 1;

static void handle_sigint(int sig)
{
	(void)sig;
	running = 0;
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

	/* Broadcast-style: for now just send to the socket itself as a log sink.
	 * Later you can adapt this to send to a connected client address.
	 */
	sendto(sockfd, msg, strlen(msg), 0, NULL, 0);
}

int main(void)
{
	int fd_input;
	int sockfd;
	struct pollfd pfds[1];
	char ch;

	signal(SIGINT, handle_sigint);
	signal(SIGTERM, handle_sigint);

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
		if (ret == 0)
			continue;

		if (pfds[0].revents & POLLIN) {
			ret = read(fd_input, &ch, 1);
			if (ret == 1) {
				const char *msg = "BUTTON_EVENT\n";
				printf("music_daemon: button event\n");
				notify_clients(sockfd, msg);
				/* Later you will hook this into ALSA / playback. */
			}
		}
	}

	printf("music_daemon: shutting down\n");
	if (sockfd >= 0) {
		close(sockfd);
		unlink(SOCKET_PATH);
	}
	close(fd_input);

	return 0;
}

