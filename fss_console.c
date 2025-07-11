#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/inotify.h>
#include <errno.h>

int main (int argc, char *argv[]) {
    char *logfile = NULL;

    if (argc != 3 || strcmp(argv[1], "-l") != 0) {
        fprintf(stderr, "Usage: %s -l <console-logfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    logfile = argv[2];

    // Now you can use `logfile` to open your log
    FILE *log = fopen(logfile, "w");
    if (!log) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    //fprintf(log, "Console logging started.\n");
    // Continue with your main logic...

    //printf("heloo\n");
    int pdin = open("fss_in", O_WRONLY);
    if (pdin == -1) {
        return 1;
    }
    int pdout = open("fss_out", O_RDONLY);
    if (pdin == -1) {
        return 1;
    }

    time_t now;
    struct tm *timeinfo;
    char buffer[30];
    char logb[2048];

    char buf[1024];
    char resp[2048];
    printf("> ");
    while (fgets(buf, 1024, stdin) != NULL) {
        time(&now);
        timeinfo = localtime(&now);
        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
        snprintf(logb, sizeof(logb), "%s Command %s\n", buffer, buf);
        fprintf(log, "%s\n", logb);
        if (write(pdin, buf, strlen(buf)) == -1) {
            perror("write");
            break;
        }

        int n = read(pdout, resp, sizeof(resp) - 1);
        if (n > 0) {
            resp[n] = 0;
            printf("%s", resp);
            fprintf(log, "%s\n", resp);
            if (resp[23] == 'h') {
                // printf("shut\n");
                // n = read(pdout, resp, sizeof(resp) - 1);
                // if (n > 0) {
                //     resp[n] = 0;
                //     printf("succes\n");
                //     printf("%s", resp);
                // } else {
                //     perror("reaa 1");
                //     break;
                // }
                // n = read(pdout, resp, sizeof(resp) - 1);
                // if (n > 0) {
                //     resp[n] = 0;
                //     printf("%s", resp);
                // } else {
                //     perror("reaad 2");
                //     break;
                // }
                break;
            } else if (resp[23] == 'd') {
                //printf("add found\n");
                // n = read(pdout, resp, sizeof(resp) - 1);
                // if (n > 0) {
                //     resp[n] = 0;
                //     printf("%s", resp);
                // } else {
                //     perror("reaad add");
                //     break;
                // }
                //printf("add doen\n");
            } else if (resp[26] == 'i') {
                //printf("add found\n");
                n = read(pdout, resp, sizeof(resp) - 1);
                if (n > 0) {
                    resp[n] = 0;
                    printf("%s", resp);
                    fprintf(log, "%s\n", resp);
                } else {
                    perror("reaad add");
                    break;
                }
                //printf("add doen\n");
            }
        } else {
            perror("read");
            break;
        }
        printf("> ");
    } 
    //int r = 5;
    //write(pdin, &r, sizeof(int));
    //printf("done\n");
    fclose(log);

    return 0;
}
