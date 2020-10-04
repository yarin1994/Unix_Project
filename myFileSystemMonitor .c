#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

char dir[100];
char ip[32];

static void handle_events(int fd, int *wd, int fdHTML);

static void handle_events(int fd, int *wd, int fdHTML)
{
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    int i;
    ssize_t len;
    char *ptr;

    /* Loop while events can be read from inotify file descriptor. */
    for (;;)
    {

        /* Read some events. */

        len = read(fd, buf, sizeof buf);
        if (len == -1 && errno != EAGAIN)
        {
            perror("read");
            exit(EXIT_FAILURE);
        }

        if (len <= 0)
            break;

        /* Loop over all events in the buffer */

        char mainBuf[1024];
        char timeStrBuf[32]; // YYYY-MM-DD HH-MM-SS
        char opBuf[16];

        for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len)
        {
            time_t currTime;
            struct tm *timeInfo;
            memset(mainBuf, 0, 1024); // clean both buffers before rewrite them
            memset(opBuf, 0, 16);
            event = (const struct inotify_event *)ptr;

            if (!(event->mask & IN_OPEN))
            {
                memset(timeStrBuf, 0, sizeof(timeStrBuf));
                currTime = time(NULL);
                timeInfo = localtime(&currTime);
                strftime(timeStrBuf, 32, "%Y-%b-%d at %H:%M:%S", timeInfo); //%B
                write(fdHTML, timeStrBuf, strlen(timeStrBuf));
                write(fdHTML, ": ", strlen(": "));

                if (event->mask & IN_CLOSE_NOWRITE)
                    strcpy(opBuf, "NO_WRITE: ");
                if (event->mask & IN_CLOSE_WRITE)
                    strcpy(opBuf, "WRITE: ");
            }

            write(fdHTML, opBuf, strlen(opBuf));

            /* Print the name of the watched directory */

            if (*wd == event->wd)
                strcat(mainBuf, dir);

            /* Print the name of the file */

            if (event->len)
            {
                write(fdHTML, event->name, strlen(event->name));
                strcat(mainBuf, event->name);
            }

            /* Print type of filesystem object */

            if (event->mask & IN_ISDIR)
                write(fdHTML, " -> [dir]<br>", strlen(" -> [dir]<br>"));
            else
                write(fdHTML, " -> [file]<br>", strlen(" -> [file]<br>"));
        }
    }
}

int main(int argc, char *argv[])
{
    char buf;
    int fd, fdHTML, poll_num, opt;
    int *wd;
    nfds_t nfds;
    struct pollfd fds[2];

    if (argc != 5) // ./file -d [dir] -i [IP]
    {
        perror("incorrect number of arguments");
    }

    while ((opt = getopt(argc, argv, "d:i:")) != -1)
    {
        switch (opt)
        {
        case 'd':
        {
            strcpy(dir, optarg);
            break;
        }
        case 'i':
        {
            strcpy(ip, optarg);
            break;
        }
        default:
            printf("worng arguments!\n");
            break;
        }
    }

    fdHTML = open("/var/www/html/index.html", O_WRONLY | O_TRUNC);

    if (fdHTML == -1)
        perror("open failed");

    printf("Press ENTER key to terminate the program.\n");

    /* Create the file descriptor for accessing the inotify API */

    fd = inotify_init1(IN_NONBLOCK);
    if (fd == -1)
    {
        perror("inotify_init1");
        exit(EXIT_FAILURE);
    }

    /* Mark directories for events
	 - file was opened
	 - file was closed */

    wd = inotify_add_watch(fd, dir, IN_OPEN | IN_CLOSE);
    if (*wd == -1)
    {
        fprintf(stderr, "Cannot watch '%s'\n", dir);
        perror("inotify_add_watch");
        exit(EXIT_FAILURE);
    }

    /* Prepare for polling */

    nfds = 2;

    /* Console input */

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    /* Inotify input */

    fds[1].fd = fd;
    fds[1].events = POLLIN;

    /* Wait for events and/or terminal input */

    write(fdHTML, "<html><body>", strlen("<html><body>"));
    printf("Listening for events.\n");

    while (1)
    {
        poll_num = poll(fds, nfds, -1);
        if (poll_num == -1)
        {
            if (errno == EINTR)
                continue;
            perror("poll");
            exit(EXIT_FAILURE);
        }

        if (poll_num > 0)
        {

            if (fds[0].revents & POLLIN)
            {

                /* Console input is available. Empty stdin and quit */

                while (read(STDIN_FILENO, &buf, 1) > 0 && buf != '\n')
                    continue;
                break;
            }

            if (fds[1].revents & POLLIN)
            {
                /* Inotify events are available */
                handle_events(fd, wd, fdHTML);
            }
        }
    }

    printf("Listening for events stopped.\n");

    /* Close inotify file descriptor */

    close(fd);
    free(wd);
    exit(EXIT_SUCCESS);
}