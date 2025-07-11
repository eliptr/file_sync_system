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
#include <sys/select.h>

#define BUF_SIZE 4096

int create_dir_if_not_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) == -1) {
            return -1;
        }
    }
    return 0;
}

void copy_file(const char *src_path, const char *dest_path, char *errlog, int *errcount) {
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        sprintf(errlog + strlen(errlog), "File %s: %s\n", src_path, strerror(errno));
        (*errcount)++;
        return;
    }

    int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd < 0) {
        sprintf(errlog + strlen(errlog), "File %s: %s\n", dest_path, strerror(errno));
        (*errcount)++;
        close(src_fd);
        return;
    }

    char buffer[BUF_SIZE];
    ssize_t bytes;
    while ((bytes = read(src_fd, buffer, BUF_SIZE)) > 0) {
        if (write(dest_fd, buffer, bytes) != bytes) {
            sprintf(errlog + strlen(errlog), "Write error to %s: %s\n", dest_path, strerror(errno));
            (*errcount)++;
            break;
        }
    }

    if (bytes < 0) {
        sprintf(errlog + strlen(errlog), "Read error from %s: %s\n", src_path, strerror(errno));
        (*errcount)++;
    }

    close(src_fd);
    close(dest_fd);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <source> <target> <filename> <operation>\n", argv[0]);
        return 1;
    }

    //printf("Worker started with:\n");
    //printf("Source: %s\n", argv[1]);
    //printf("Target: %s\n", argv[2]);
    //printf("Filename: %s\n", argv[3]);
    //printf("Operation: %s\n", argv[4]);
    char sou[32], tar[32], fi[32], op[32];
    strcpy(sou, argv[1]);
    strcpy(tar, argv[2]);
    strcpy(fi, argv[3]);
    strcpy(op, argv[4]);

    //printf("%s\n", op);

    char error_log[8192] = "";
    int error_count = 0;
    char rep[9000];
    char rstat[16];
    if (strcmp(op, "FULL") == 0) {
        DIR *dir = opendir(sou);
        if (!dir) {
            printf("Failed to open source directory: %s\n", strerror(errno));
            return 1;
        }
        int suf = 0, faf = 0;

        create_dir_if_not_exists(tar);
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                char src_path[1024], dest_path[1024];
                snprintf(src_path, sizeof(src_path), "%s/%s", sou, entry->d_name);
                snprintf(dest_path, sizeof(dest_path), "%s/%s", tar, entry->d_name);
                int prever = error_count;
                copy_file(src_path, dest_path, error_log, &error_count);
                if (prever == error_count) {
                    suf++;
                } else {
                    faf++;
                }
            }
        }
        closedir(dir);
        
        if (faf == 0) {
            strcpy(rstat, "SUCCESS");
            snprintf(rep, sizeof(rep), "EXEC_REPORT_START\nSTATUS: %s\nDETAILS: %d files copied\nERRORS: %d\n %sEXEC_REPORT_END\n", rstat, suf, error_count, error_log);
        } else if (suf != 0) {
            strcpy(rstat, "PARTIAL");
            snprintf(rep, sizeof(rep), "EXEC_REPORT_START\nSTATUS: %s\nDETAILS: %d files copied, %d skipped\nERRORS: %d\n %sEXEC_REPORT_END\n", rstat, suf, faf, error_count, error_log);
        } else {
            strcpy(rstat, "ERROR");
            snprintf(rep, sizeof(rep), "EXEC_REPORT_START\nSTATUS: %s\nDETAILS: %d skipped\nERRORS: %d\n %sEXEC_REPORT_END\n", rstat, faf, error_count, error_log);
        }
        
        if (error_count > 0) {
           //printf("Errors (%zu):\n%s", error_count, error_log);
            //strcpy(rstat, "PARTIAL");
        } else {
            //printf("Sync complete with no errors.\n");
            //strcpy(rstat, "SUCCESS");
        }
        
        
        //sleep(3);
        printf("%s", rep);
        fflush(stdout);
    } else if (strcmp(op, "ADDED") == 0) {
        //printf("go\n");
        char src_path[1024], dest_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", sou, fi);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", tar, fi);
        copy_file(src_path, dest_path, error_log, &error_count);
        if (error_count == 0) {
            strcpy(rstat, "SUCCESS");
        } else {
            strcpy(rstat, "ERROR");
        }
        snprintf(rep, sizeof(rep), "EXEC_REPORT_START\nSTATUS: %s\nDETAILS: 1 file added\nERRORS: %d\n %sEXEC_REPORT_END\n", rstat, error_count, error_log);
        //sleep(3);
        printf("%s", rep);
        fflush(stdout);
    } else if (strcmp(op, "MODIFIED") == 0) {
        char src_path[1024], dest_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", sou, fi);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", tar, fi);
        copy_file(src_path, dest_path, error_log, &error_count);
        if (error_count == 0) {
            strcpy(rstat, "SUCCESS");
        } else {
            strcpy(rstat, "ERROR");
        }
        snprintf(rep, sizeof(rep), "EXEC_REPORT_START\nSTATUS: %s\nDETAILS: 1 file modified\nERRORS: %d\n %sEXEC_REPORT_END\n", rstat, error_count, error_log);
        //sleep(3);
        printf("%s", rep);
        fflush(stdout);
    } else if (strcmp(op, "DELETED") == 0) {
        char dest_path[1024];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", tar, fi);
    
        if (remove(dest_path) != 0) {
            sprintf(error_log + strlen(error_log), "File %s: %s\n", dest_path, strerror(errno));
            error_count++;
        } 
        if (error_count == 0) {
            snprintf(rep, sizeof(rep), "EXEC_REPORT_START\nSTATUS: SUCCESS\nDETAILS: 1 file deleted\nERRORS: %d\n %sEXEC_REPORT_END\n", error_count, error_log);
        } else {
            snprintf(rep, sizeof(rep), "EXEC_REPORT_START\nSTATUS: ERROR\nDETAILS: 1 file deleted\nERRORS: %d\n %sEXEC_REPORT_END\n", error_count, error_log);
        }
        printf("%s", rep);
        fflush(stdout);
        
    }

    return 0;
}
