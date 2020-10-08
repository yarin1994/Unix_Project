#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <strings.h>
#include <execinfo.h>
#include <sys/inotify.h>
#include <pthread.h>
#include <semaphore.h>
#include <libcli.h>
#include <stdbool.h>

#define NTCT_PORT 5555
#define TLNT_PORT 23456

char dir[100];
char ip[32];
char telnetBuff[1024];
int listenerSocket;
bool listenToTN = true;
bool backtraceCommnad = false;
sem_t newSemaphore;

static void handle_events(int fd, int wd, int fdHTML);
void sendToServer(char *time_str, char *op_str, char *main_str);
void BackTrace();
void *btTelnet();
void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void *this_fn, void *call_site);
int cmd_backtrace(struct cli_def *cli, char *command, char *argv[], int argc);

/* --------------------------------------------------------------------------------------------------------------- */

static void handle_events(int fd, int wd, int fdHTML)
{
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    ssize_t len;
    char *ptr;
    pid_t newProcess;

    /* Loop while events can be read from inotify file descriptor. */
    for (;;)
    {
        /* Read some events. */
        len = read(fd, buf, sizeof buf);
        if (len == -1 && errno != EAGAIN)
        {
            perror("Read faliure!");
            exit(EXIT_FAILURE);
        }

        if (len <= 0)
            break;

        char opBuf[16]; // WRITE / READ
        char timeStrBuf[32]; // DD-MONTH-YYYY at HH-MM-SS
        char mainBuf[1024];

        /* Loop over all events in the buffer */
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
                strftime(timeStrBuf, 32, "%d-%B-%Y at %H:%M:%S", timeInfo);
                write(fdHTML, timeStrBuf, strlen(timeStrBuf));
                write(fdHTML, " -> ", strlen(" -> "));

                if (event->mask & IN_CLOSE_WRITE)
                    strcpy(opBuf, "WRITE: ");
                if (event->mask & IN_CLOSE_NOWRITE)
                    strcpy(opBuf, "READ: ");
            }

            write(fdHTML, opBuf, strlen(opBuf));

            /* write the name of the watched directory */
            if (wd == event->wd)
                strcat(mainBuf, dir);

            /* write file name */
            if (event->len)
            {
                write(fdHTML, event->name, strlen(event->name));
                strcat(mainBuf, event->name);
            }

            /* write type of filesystem object */
            if (event->mask & IN_ISDIR)
                write(fdHTML, " [dir]<br>", strlen(" [dir]<br>"));
            else
                write(fdHTML, " [file]<br>", strlen(" [file]<br>"));

            newProcess = fork();
            if (newProcess == -1)
                perror("Fork faliure!");

            if (newProcess == 0)
                sendToServer(timeStrBuf, opBuf, mainBuf);
        }
    }
}

/* --------------------------------------------------------------------------------------------------------------- */

void sendToServer(char *time_str, char *op_str, char *main_str)
{
    /* in order to send the data, we need to create a new socket */
    int sock;
    struct sockaddr_in senderSocket = {0};
    senderSocket.sin_family = AF_INET;
    senderSocket.sin_port = htons(NTCT_PORT);

    if (inet_pton(AF_INET, ip, &senderSocket.sin_addr.s_addr) <= 0)
    {
        perror("Address faliure!");
        exit(1);
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (connect(sock, (struct sockaddr *)&senderSocket, sizeof(senderSocket)) < 0)
    {
        perror("Connection failure!");
        exit(1);
    }

    char packet[2048];
    memset(packet, 0, sizeof(packet));

    /* create the packet to be send */
    strcpy(packet, "\nFILE ACCESSED: ");
    strcat(packet, main_str);
    strcat(packet, "\nACCESS: ");
    strcat(packet, op_str);
    strcat(packet, "\nTIME OF ACCESS: ");
    strcat(packet, time_str);
    strcat(packet, "\n\0");

    int chsent;
    if ((chsent = send(sock, packet, strlen(packet), 0)) < 0)
    {
        perror("Receive failure!");
        exit(1);
    }

    close(sock);
    exit(0);
}

/* --------------------------------------------------------------------------------------------------------------- */

int cmd_backtrace(struct cli_def *cli, char *command, char *argv[], int argc)
{
    backtraceCommnad = true;
    sem_wait(&newSemaphore); // decrement by 1
    cli_print(cli, "%s", telnetBuff);
    return CLI_OK;
}

/* --------------------------------------------------------------------------------------------------------------- */

void BackTrace()
{
    int nptrs;
    char **strings;
    char counter[16];
    void *buffer[1024];

    memset(buffer, 0, sizeof(buffer));
    memset(telnetBuff, 0, sizeof(telnetBuff));

    nptrs = backtrace(buffer, 1024);
    printf("backtrace() returned %d addresses\n", nptrs);

    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL)
    {
        perror("backtrace_symbols failure!");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < nptrs; i++)
    {
        sprintf(counter, "[%d]: ", i + 1);
        strcat(telnetBuff, counter);
        strcat(telnetBuff, strings[i]);
        strcat(telnetBuff, "\n\0");
    }

    free(strings);
}

/* --------------------------------------------------------------------------------------------------------------- */

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    if (backtraceCommnad)
    {
        backtraceCommnad = false;
        BackTrace();
        sem_post(&newSemaphore); // increment by 1
    }
}

/* --------------------------------------------------------------------------------------------------------------- */

void *btTelnet()
{
    struct sockaddr_in servaddr;
    struct cli_command *c;
    struct cli_def *cli;
    int on = 1;

    /* !!! Compile and link with -lcli flag !!! */

    // Must be called first to setup data structures
    cli = cli_init();

    // Set the hostname (shown in the the prompt)
    cli_set_hostname(cli, "myFileSystemMonitor");

    // Set the greeting
    cli_set_banner(cli, "Welcome to the CLI backtrace programs\nfor backtrace enter 'backtrace'.");

    // Enable username / password combinations
    cli_allow_user(cli, "final", "1234");

    // Set up a few simple one-level commands
    cli_register_command(cli, NULL, "backtrace", cmd_backtrace, PRIVILEGE_UNPRIVILEGED, MODE_EXEC, NULL);

    // Create a socket
    listenerSocket = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(listenerSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    // Listen on port TLNT_PORT(23456)
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TLNT_PORT);
    bind(listenerSocket, (struct sockaddr *)&servaddr, sizeof(servaddr));

    // Wait for a connection
    listen(listenerSocket, 50);

    int x;
    while (listenToTN && (x = accept(listenerSocket, NULL, 0)))
    {
        // Pass the connection off to libcli
        cli_loop(cli, x);
        close(x);
    }

    cli_done(cli);
    pthread_exit(0);
}

/* --------------------------------------------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    char buf;
    int fd, fdHTML, poll_num, opt, wd;
    struct pollfd fds[2];
    nfds_t nfds;
    pthread_t newThread;

    /* !!! Link with -pthread flag !!! */
    sem_init(&newSemaphore, 0, 0);

    if (argc != 5) // ./file -d [dir] -i [IP]
        perror("incorrect number of arguments");

    /* !!! Compile and link with -pthread flag !!!  */
    if (pthread_create(&newThread, NULL, btTelnet, NULL) != 0)
        perror("Thread faliure!");

    while ((opt = getopt(argc, argv, "d:i:")) != -1)
    {
        switch (opt)
        {
        case 'd':
        {
            strcpy(dir, optarg);
            printf("dir is: %s\n", dir);
            break;
        }
        case 'i':
        {
            strcpy(ip, optarg);
            printf("ip is: %s\n", ip);
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

    if (argc < 3)
    {
        printf("Usage: %s PATH [PATH ...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("Press ENTER key to terminate the program.\n");

    /* Create the file descriptor for accessing the inotify API */
    fd = inotify_init1(IN_NONBLOCK);
    if (fd == -1)
    {
        perror("inotify_init1 failure");
        exit(EXIT_FAILURE);
    }

    wd = inotify_add_watch(fd, dir, IN_OPEN | IN_CLOSE);
    if (wd == -1)
    {
        fprintf(stderr, "Cannot watch '%s'\n", dir);
        perror("inotify_add_watch faliure");
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
    write(fdHTML, "<!DOCTYPE html><html><title>File Access Monitor</title><body>", strlen("<!DOCTYPE html><html><title>File Access Monitor</title><body>"));

    printf("Listening for events.\n");

    while (true)
    {
        poll_num = poll(fds, nfds, -1);
        if (poll_num == -1)
        {
            if (errno == EINTR)
                continue;
            perror("Poll failure!");
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

    listenToTN = false;
    close(listenerSocket);
    write(fdHTML, "</html></body>", strlen("</html></body>")); //close html tags
    close(fdHTML);
    close(fd);
    exit(EXIT_SUCCESS);
}