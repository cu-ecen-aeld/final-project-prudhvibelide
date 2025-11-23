#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define DEVICE_PATH "/dev/music_input"

int main(int argc, char *argv[])
{
    int fd;
    char buf[32];
    ssize_t bytes_read;
    int button_state;
    int prev_state = -1;
    
    printf("Music Input Button Test\n");
    printf("========================\n");
    printf("Reading from: %s\n", DEVICE_PATH);
    printf("Press Ctrl+C to exit\n\n");
    
    /* Open the device */
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        printf("Make sure the driver is loaded: modprobe music_input_driver\n");
        return 1;
    }
    
    printf("Device opened successfully!\n");
    printf("Monitoring button state...\n\n");
    
    /* Main loop - poll button state */
    while (1) {
        /* Read button state */
        lseek(fd, 0, SEEK_SET); /* Reset file position */
        bytes_read = read(fd, buf, sizeof(buf) - 1);
        
        if (bytes_read < 0) {
            perror("Read failed");
            break;
        }
        
        buf[bytes_read] = '\0'; /* Null terminate */
        button_state = atoi(buf);
        
        /* Only print when state changes */
        if (button_state != prev_state) {
            if (button_state == 0) {
                printf("Button PRESSED  (GPIO = 0)\n");
            } else {
                printf("Button RELEASED (GPIO = 1)\n");
            }
            prev_state = button_state;
        }
        
        /* Small delay to avoid CPU hogging */
        usleep(50000); /* 50ms */
    }
    
    close(fd);
    return 0;
}
