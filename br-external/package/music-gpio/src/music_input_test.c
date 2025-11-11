/* music_input_test.c
 * Blocks on /dev/music_input and prints on button events.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

int main(void)
{
    int fd = open("/dev/music_input", O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }

    while (1) {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        int rc = poll(&p, 1, -1);
        if (rc < 0) { perror("poll"); break; }
        if (p.revents & POLLIN) {
            char c;
            if (read(fd, &c, 1) == 1)
                printf("event: %c\n", c);
        }
    }
    close(fd);
    return 0;
}
