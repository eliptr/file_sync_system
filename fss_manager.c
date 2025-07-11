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
#include <sys/wait.h>

#define MONI_EV_SIZE (sizeof(struct inotify_event))
#define MAX_BUF 1024*(MONI_EV_SIZE+32)

typedef struct sync_info {
    char sou[128], tar[128];
    int status;
    char last_sync[32];
    int active;
    int error_count;

    int wd;
    int pidsync;
    char nm[128];
    struct sync_info *next;
} sync_info;

struct file_event_tracker {
    char last_file[NAME_MAX];
    time_t last_time;
};

struct file_event_tracker tracker = { "", 0 };


typedef struct pip {
    int fdpip[2];
    int act;
    int prid;
} pip;

typedef struct worq {
    char sou[128], tar[128], filen[128], op[16];
    struct worq *next;
} worq;

void get_time(char *buffer) {
    time_t now;
    struct tm *timeinfo;

    time(&now);
    timeinfo = localtime(&now);
    strftime(buffer, 64, "[%Y-%m-%d %H:%M:%S]", timeinfo);
}

int actwork = 0;
int gwork = 5;
pip *glob_p = NULL;
sync_info *list = NULL;
sync_info *que = NULL;
FILE *fp_log = NULL;

void set_syncp(sync_info *h, const char *src, int p) {
    sync_info *pls = list;
    while (pls != NULL) {
        if (strcmp(pls->sou, src) == 0) {
            break;
        }
        pls = pls->next;
    }
    //printf("f %d\n", p);
    pls->pidsync = p;
    //printf("try\n");

}
// signal handler for when a worker finishes
void handler(int sig, siginfo_t *si, void *unused) {
    //printf("killed\n");
    int s, p;
    while ((p = waitpid(-1, &s, WNOHANG)) > 0) {
        //printf("Handler collected child %d\n", p);
    }
    if (p > 0) {
        //printf("Handler collected child %d\n", p);
    } else {
        //printf("Handler found no child to reap\n");
        p = si->si_pid;
    }
    //printf("Child killed %d\n", actwork);
    //if (p > 0) {
        actwork--;
        char rep[9000];
        //printf("Child killed. actwork = %d\n", actwork);
        for (int i = 0; i < gwork; i++) {
            if (glob_p[i].prid == p) {
                //printf("Worker %d (pid %d) terminated. act: %d read pipe %d\n", i, p, glob_p[i].act, glob_p[i].fdpip[0]);
                
                int n = read(glob_p[i].fdpip[0], rep, sizeof(rep) -1);
                //printf("trigger\n");
                if (n>0) {
                    rep[n] = 0;
                    //printf("from worker %s", rep);
                } else {
                    close(glob_p[i].fdpip[0]);
                }
                
                //fprintf(fp_log, "%s\n", rep);
                glob_p[i].act = 0; 
                break;
            }
        }
        sync_info *pls = list;
        while (pls != NULL) {
            if (pls->pidsync == p) {
                break;
            }
            pls = pls->next;
        }
        //printf("%s source file magic\n", pls->sou);
        //pls->status = 0;

        char manlog[9000];
        
        time_t now;
        struct tm *timeinfo;
        time(&now);
        timeinfo = localtime(&now);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
        char statman[16];
        if (pls->status == 1 || pls->status == 5) {
            strcpy(statman, "FULL");
        } else if (pls->status == 2) {
            strcpy(statman, "ADDED");
        } else if (pls->status == 3) {
            strcpy(statman, "MODIFIED");
        } else if (pls->status == 4) {
            strcpy(statman, "DELETED");
        }
        char sucman[32];
        int mp = 0, tp = 0;
        while (rep[mp] != ':') {
            mp++;
        }
        mp += 2;
        while (rep[mp] != '\n') {
            sucman[tp++] = rep[mp++];
        } 
        sucman[tp] = 0;
        tp = 0;
        char deman[256];
        if (pls->status == 1 || pls->status == 5) {
            while (rep[mp] != ':') {
                mp++;
            }
            mp += 2;
            while (rep[mp] != '\n') {
                deman[tp++] = rep[mp++];
            } 
            deman[tp] = 0;
        } else {
            snprintf(deman, sizeof(deman), "File: %s", pls->nm);
            while (rep[mp] != ':') {
                mp++;
            }
            mp += 2;
            while (rep[mp] != '\n') {
                deman[tp++] = rep[mp++];
            } 
            deman[tp] = 0;
        }
        int erep = 0;
        mp++;
        while (rep[mp] != ':') {    
            mp++;
        }
        mp += 2;
        erep = atoi(rep + mp);
        //printf("error count: %d\n", erep);
        pls->error_count = pls->error_count + erep;

        snprintf(manlog, sizeof(manlog), "%s [%s] [%s] [%d] [%s] [%s] [%s]\n", buffer, pls->sou, pls->tar, p, statman, sucman, deman);
        fprintf(fp_log, "%s\n", manlog);
        pls->status = 0;
        if (que != NULL) {
            printf("que\n");
            actwork++;
            int cp;
            pls = que;
            for (int i = 0; i<gwork;i++) {
                if (glob_p[i].act == 0) {
                    cp = i;
                    break;
                }
            }
            //printf("%d  \n", cp);
            glob_p[cp].act = 1;
            printf("%d act is \n", glob_p[cp].act);
            if (pipe(glob_p[cp].fdpip) == -1) {
                perror("pipe");
            }
            fcntl(glob_p[cp].fdpip[0], F_SETFL, O_NONBLOCK);
            int p = fork();
            if (p == -1) {
                perror("fork");
            }
            if (p != 0) {
                //printf("parent here\n");
                close(glob_p[cp].fdpip[1]);
                glob_p[cp].prid = p;
                set_syncp(list, pls->sou, p);
            } else {
                //printf("child here\n");
                close(glob_p[cp].fdpip[0]);
                dup2(glob_p[cp].fdpip[1], STDOUT_FILENO);
                close(glob_p[cp].fdpip[1]);
                
                if (pls->status == 1 || pls->status == 5) {
                    execlp("./worker", "./worker", pls->sou, pls->tar, "ALL", "FULL", NULL);
                    perror("exec");
                } else if (pls->status == 2) {
                    execlp("./worker", "./worker", pls->sou, pls->tar, pls->nm, "ADDED", NULL);
                    perror("exec");
                } else if (pls->status == 3) {
                    execlp("./worker", "./worker", pls->sou, pls->tar, pls->nm, "MODIFIED", NULL);
                    perror("exec");
                } else if (pls->status == 4) {
                    execlp("./worker", "./worker", pls->sou, pls->tar, pls->nm, "DELETED", NULL);
                    perror("exec");
                }
                
            }
            que = que->next;
        }
    //}
}




sync_info *add_sync_info(sync_info *head, const char *src, const char *tgt, int status, int active, int err, int fdinot) {
    sync_info *new_node = malloc(sizeof(sync_info));
    strcpy(new_node->sou, src);
    strcpy(new_node->tar, tgt);
    new_node->status = status;
    time_t now;
    struct tm *timeinfo;
    char buffer[32];
    time(&now);
    timeinfo = localtime(&now);
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", timeinfo);
    strcpy(new_node->last_sync, buffer);
    new_node->active = active;
    new_node->error_count = err;
    new_node->next = head;
    new_node->wd = inotify_add_watch(fdinot, src, IN_CREATE | IN_MODIFY | IN_DELETE);
    //printf("added %s\n", src);
    return new_node;
}

sync_info *add_que(sync_info *head, const char *src, const char *tgt, int status, int active, int err, const char *nm) {
    sync_info *new_node = malloc(sizeof(sync_info));
    strcpy(new_node->sou, src);
    strcpy(new_node->tar, tgt);
    strcpy(new_node->nm, nm);
    new_node->status = status;
    time_t now;
    struct tm *timeinfo;
    char buffer[32];
    time(&now);
    timeinfo = localtime(&now);
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", timeinfo);
    strcpy(new_node->last_sync, buffer);
    new_node->active = active;
    new_node->error_count = err;
    new_node->next = NULL;
    //new_node->wd = inotify_add_watch(fdinot, src, IN_CREATE | IN_MODIFY | IN_DELETE);
    //printf("added %s\n", src);
    sync_info *tempq = head;
    while (tempq != NULL) {
        if (tempq->next == NULL) {
            break;
        }
        tempq = tempq->next;
    }
    if (tempq != NULL) {
        tempq->next = new_node;    
    } else {
        head = new_node;
    }
    return head;
    
    //return new_node;
}



int main(int argc, char* argv[]) {
    char *logfile = NULL;
    char *config_file = NULL;
    int worker_limit = 5;

    int opt;
    while ((opt = getopt(argc, argv, "l:c:n:")) != -1) {
        switch (opt) {
            case 'l':
                logfile = optarg;
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'n':
                worker_limit = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s -l <logfile> -c <config_file> -n <worker_limit>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    //printf("Logfile: %s\n", logfile);
    //printf("Config File: %s\n", config_file);
    //printf("Worker Limit: %d\n", worker_limit);
    gwork = worker_limit;

    pip fds[worker_limit];
    glob_p = fds;
    for (int i = 0;i<worker_limit;i++) {
        fds[i].act = 0;
    }


    time_t now;
    struct tm *timeinfo;
    char buffer[30];

    //int actwork = 0;
    int cp = 0;

    //sync_info *list = NULL;
    int fdinot = inotify_init();

    //signal(SIGCHLD, handler);
    struct sigaction sa;
    sa.sa_sigaction = handler;  // <-- not sa_handler
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }


    unlink("fss_in");
    unlink("fss_out");
    if (mkfifo("fss_in", 0777) == -1){
        perror("make fifo");
    }
        if (mkfifo("fss_out", 0777) == -1){
        perror("make fifo");
        return 1;
    }

    // read config file
    char tarbuf[128], soubuf[128];
    FILE *fp = fopen(config_file, "r");
    fp_log = fopen(logfile, "w");
    char temp;
    int c =  0;    

    int pdout = open("fss_out", O_RDWR | O_NONBLOCK);
    if (pdout == -1) {
        return 1;
    }
    char resp[2048];
    //read config file and start backup for the listed directories
    while ((temp = fgetc(fp)) != EOF) {
        while (temp != ' ') {
            soubuf[c++] = temp;
            temp = fgetc(fp);
        }
        soubuf[c] = '\0';
        //printf("read source %s\n", soubuf);
        c = 0;
        temp = fgetc(fp);
        while (temp != 10) {
            tarbuf[c++] = temp;
            if ((temp = fgetc(fp)) == EOF) {
                break;
            }
        }
        tarbuf[c] = '\0';

        time(&now);
        timeinfo = localtime(&now);
        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
        list = add_sync_info(list, soubuf, tarbuf, 1, 1, 0, fdinot);
        printf("%s Added directory: %s -> %s\n", buffer, soubuf, tarbuf);
        // snprintf(resp, sizeof(resp), "%s Added directory: %s -> %s\n", buffer, soubuf, tarbuf);
        // if (write(pdout, resp, strlen(resp)) == -1) {
        //     perror("write");
        //     break;
        // }
        if (actwork < worker_limit) {
            actwork++;
            for (int i = 0; i<worker_limit;i++) {
                if (fds[i].act == 0) {
                    cp = i;
                    break;
                }
            }
            //printf("%d  \n", cp);
            fds[cp].act = 1;
            if (pipe(fds[cp].fdpip) == -1) {
                perror("pipe");
            }
            //fcntl(fds[cp].fdpip[0], F_SETFL, O_NONBLOCK);
            int p = fork();
            if (p == -1) {
                perror("fork");
            }
            if (p != 0) {
                //printf("parent here\n");
                close(fds[cp].fdpip[1]);
                fds[cp].prid = p;
                set_syncp(list, soubuf, p);
                
            } else {
                //printf("child here\n");
                close(fds[cp].fdpip[0]);
                dup2(fds[cp].fdpip[1], STDOUT_FILENO);
                close(fds[cp].fdpip[1]);
                
                execlp("./worker", "./worker", soubuf, tarbuf, "ALL", "FULL", NULL);
                perror("exec");
            }
        } else {
            add_que(que, soubuf, tarbuf, 1, 0, 0, "");

        }
        printf("%s Monitoring started for: %s\n", buffer, soubuf);
        fprintf(fp_log, "%s Added directory: %s -> %s\n", buffer, soubuf, tarbuf);
        fprintf(fp_log, "%s Added directory: %s -> %s\n", buffer, soubuf, tarbuf);
        //list = add_sync_info(list, soubuf, tarbuf, 0, 1, 0, fdinot);

        memset(soubuf, 0, 128);
        memset(tarbuf, 0, 128);
        c = 0;
    } 

    int pdin = open("fss_in", O_RDWR | O_NONBLOCK);
    if (pdin == -1) {
        return 1;
    }
    
    // sync_info *head = NULL;
    // head = add_sync_info(head, "/src/A", "/tgt/A", 0, time(NULL), 1, 0);
    //list = add_sync_info(list, "testdir", "/tgt/B", 0, time(NULL), 1, 0, fdinot);
    //printf("monitor starting\n");

    
    char ibuf[1024];
    int i =0;
    //with select watch for the pipes and changes in directories
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fdinot, &read_fds);
        FD_SET(pdin, &read_fds);
        //printf("test\n");

        int maxfd = (fdinot > pdin ? fdinot : pdin) + 1;
        // for (int i = 0;i<worker_limit; i++) {
        //     //printf("onw\n");
        //     if (fds[i].act) {
        //         FD_SET(fds[i].fdpip[0], &read_fds);
        //         if (fds[i].fdpip[0] > maxfd)
        //             maxfd = fds[i].fdpip[0];
        //     } 
        // }
        // int ret = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        // //printf("after\n");
        // if (ret < 0) {
        //     perror("select");
        //     break;
        // }  
        int ret;
        do {
            ret = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        } while (ret == -1 && errno == EINTR);

        if (ret == -1) {
            perror("select");
            break; // or exit
        }

        if (FD_ISSET(fdinot, &read_fds)) {
            int totalr = read(fdinot, ibuf, 1024);
            //printf("go %d\n", list->wd);
            if (totalr < 0) {
                perror("read inotofy");
            }
            sync_info *tmp = list;

            i = 0;
            while (i<totalr) {
                struct inotify_event *event= (struct inotify_event*)&ibuf[i];
                if (event->mask & IN_IGNORED) {
                    //printf("Watch removed for wd: %d\n", event->wd);
                    i += MONI_EV_SIZE + event->len;
                    continue;  // skip further processing
                }
                if (event->len) {
                    while (tmp->wd != event->wd) {
                        tmp = tmp->next;
                    }

                    if (event->mask & IN_CREATE) {      
                        //printf("file created \"%s\" from folder %s\n", event->name, tmp->sou);
                        tmp->status = 2;
                        strcpy(tmp->nm, event->name);
                        if (actwork < worker_limit) {
                            actwork++;
                            for (int i = 0; i<worker_limit;i++) {
                                if (fds[i].act == 0) {
                                    cp = i;
                                    break;
                                }
                            }
                            //printf("%d  \n", cp);
                            fds[cp].act = 1;
                            if (pipe(fds[cp].fdpip) == -1) {
                                perror("pipe");
                            }
                            fcntl(fds[cp].fdpip[0], F_SETFL, O_NONBLOCK);
                            int p = fork();
                            if (p == -1) {
                                perror("fork");
                            }
                            if (p != 0) {
                                //printf("parent here\n");
                                close(fds[cp].fdpip[1]);
                                fds[cp].prid = p;
                                set_syncp(list, tmp->sou, p);
                            } else {
                                //printf("child here\n");
                                close(fds[cp].fdpip[0]);
                                dup2(fds[cp].fdpip[1], STDOUT_FILENO);
                                close(fds[cp].fdpip[1]);

                                execlp("./worker", "./worker", tmp->sou, tmp->tar, event->name, "ADDED", NULL);
                                perror("exec");
                            }
                        } else {
                            add_que(que, tmp->sou, tmp->tar, 2, 0, 0, event->name);
                        }
                    }
                    if (event->mask & IN_MODIFY) {
                        time_t now = time(NULL);
                        if (strcmp(event->name, tracker.last_file) == 0 && now - tracker.last_time < 5) {
                            // Ignore duplicate event within 1 second
                            continue;
                        }
                        strcpy(tracker.last_file, event->name);
                        tracker.last_time = now;
                        
                        //printf("file modified \"%s\"\n", event->name);
                        tmp->status = 3;
                        strcpy(tmp->nm, event->name);
                        if (actwork < worker_limit) {
                            actwork++;
                            for (int i = 0; i<worker_limit;i++) {
                                if (fds[i].act == 0) {
                                    cp = i;
                                    break;
                                }
                            }
                            //printf("%d  \n", cp);
                            fds[cp].act = 1;
                            if (pipe(fds[cp].fdpip) == -1) {
                                perror("pipe");
                            }
                            fcntl(fds[cp].fdpip[0], F_SETFL, O_NONBLOCK);
                            int p = fork();
                            if (p == -1) {
                                perror("fork");
                            }
                            if (p != 0) {
                                //printf("parent here\n");
                                close(fds[cp].fdpip[1]);
                                fds[cp].prid = p;
                                set_syncp(list, tmp->sou, p);
                            } else {
                                //printf("child here\n");
                                close(fds[cp].fdpip[0]);
                                dup2(fds[cp].fdpip[1], STDOUT_FILENO);
                                close(fds[cp].fdpip[1]);

                                execlp("./worker", "./worker", tmp->sou, tmp->tar, event->name, "MODIFIED", NULL);
                                perror("exec");
                            }
                        } else {
                            add_que(que, tmp->sou, tmp->tar, 3, 0, 0, event->name);
                        }
                    }
                    if (event->mask & IN_DELETE) {
                        //printf("file deleted \"%s\"\n", event->name);
                        tmp->status = 4;
                        strcpy(tmp->nm, event->name);
                        if (actwork < worker_limit) {
                            actwork++;
                            for (int i = 0; i<worker_limit;i++) {
                                if (fds[i].act == 0) {
                                    cp = i;
                                    break;
                                }
                            }
                            //printf("%d  \n", cp);
                            fds[cp].act = 1;
                            if (pipe(fds[cp].fdpip) == -1) {
                                perror("pipe");
                            }
                            fcntl(fds[cp].fdpip[0], F_SETFL, O_NONBLOCK);
                            int p = fork();
                            if (p == -1) {
                                perror("fork");
                            }
                            if (p != 0) {
                                //printf("parent here\n");
                                close(fds[cp].fdpip[1]);
                                fds[cp].prid = p;
                                set_syncp(list, tmp->sou, p);
                            } else {
                                //printf("child here\n");
                                close(fds[cp].fdpip[0]);
                                dup2(fds[cp].fdpip[1], STDOUT_FILENO);
                                close(fds[cp].fdpip[1]);

                                execlp("./worker", "./worker", tmp->sou, tmp->tar, event->name, "DELETED", NULL);
                                perror("exec");
                            }
                        } else {
                            add_que(que, tmp->sou, tmp->tar, 4, 0, 0, event->name);
                        }
                    }
                    i+= MONI_EV_SIZE + event->len;
                }
            }
        }
        int s;
        if (FD_ISSET(pdin, &read_fds)) {
            char command[1024];
            FILE *fpipe = fdopen(pdin, "r");
            int flag = 0;
            if (!fpipe) {
                perror("fdopen");
            }
            fgets(command, sizeof(command), fpipe);
            //printf("Reicieved line %s", command);
            fflush(stdout);
            if (strcmp(command, "shutdown\n") == 0) {
                //printf("shut\n");
                flag = 1;
                get_time(buffer);
                printf("%s Shutting down manager...\n%s Waiting for all active workers to finish.\n", buffer, buffer);
                snprintf(resp, sizeof(resp), "%s Shutting down manager...\n%s Waiting for all active workers to finish.\n%s Processing remaining queued tasks\n%s Manager shutdown complete.\n", buffer, buffer, buffer, buffer);
                if (write(pdout, resp, strlen(resp)) == -1) {
                    perror("write");
                    break;
                }
                get_time(buffer);
                printf("%s Processing remaining queued tasks\n", buffer);
                // snprintf(resp, sizeof(resp), "%s Processing remaining queued tasks\n", buffer);
                // if (write(pdout, resp, strlen(resp)) == -1) {
                //     perror("write");
                //     break;
                // }
                while (que != NULL) {
                    wait(&s);
                }
                get_time(buffer);
                printf("%s Manager shutdown complete.\n", buffer);
                // snprintf(resp, sizeof(resp), "%s Manager shutdown complete.\n", buffer);
                // if (write(pdout, resp, strlen(resp)) == -1) {
                //     perror("write");
                //     break;
                // }
                //break;
            } else {
                char des[8];
                strncpy(des, command, 3);
                des[3] = 0;
                if (strcmp(des, "add") == 0) {
                    //printf("add now %s\n", command);
                    int count = 4;
                    int fr = 0;
                    char tsou[128], ttar[128];
                    while (command[count] != ' ') {
                        tsou[fr++] = command[count++];
                    }
                    tsou[fr] = '\0';
                    
                    fr = 0;
                    count++;
                    while(command[count] != '\n') {
                        ttar[fr++] = command[count++];
                    }
                    ttar[fr] = '\0';
                    //printf("source to add %s to target %s\n", tsou, ttar);
                    sync_info *tmp = list;
                    int check = 0;
                    while (tmp != NULL) {
                        if (strcmp(tmp->sou, tsou) == 0 && strcmp(tmp->tar, ttar) == 0 && tmp->active == 1) {
                            check = 1;
                            break;
                        }
                        tmp = tmp->next;
                    }

                    if (check && tmp->active == 1) {
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Already in queue: %s\n", buffer, tsou);
                        snprintf(resp, sizeof(resp), "%s Already in queue: %s\n", buffer, tsou);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                                            
                    } else {
                        if (check) {
                            tmp->active = 1;
                        } else {
                            list = add_sync_info(list, tsou, ttar, 5, 1, 0, fdinot);
                        }
                        
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Added directory: %s -> %s\n", buffer, tsou, ttar);
                        // snprintf(resp, sizeof(resp), "%s Added directory: %s -> %s\n", buffer, tsou, ttar);
                        // if (write(pdout, resp, strlen(resp)) == -1) {
                        //     perror("write");
                        //     break;
                        // }
                        if (actwork < worker_limit) {
                            actwork++;
                            for (int i = 0; i<worker_limit;i++) {
                                //printf("seg\n");
                                if (fds[i].act == 0) {
                                    cp = i;
                                    break;
                                }
                            }
                            //printf("%d  \n", cp);
                            fds[cp].act = 1;
                            if (pipe(fds[cp].fdpip) == -1) {
                                printf("invalid pipe\n");
                                perror("pipe");
                            }
                            //printf("pipe created: read end %d write end %d\n", fds[cp].fdpip[0], fds[cp].fdpip[1]);

                            
                            fcntl(fds[cp].fdpip[0], F_SETFL, O_NONBLOCK);
                            //printf("seg\n");
                            int p = fork();
                            if (p == -1) {
                                perror("fork");
                            }
                            if (p != 0) {
                                //printf("parent here\n");
                                close(fds[cp].fdpip[1]);
                                fds[cp].prid = p;
                                set_syncp(list, tsou, p);
                            } else {
                                //printf("child here\n");
                                close(fds[cp].fdpip[0]);
                                //printf("invalid pipe write end %d for worker %d\n", fds[cp].act, cp);
                                if (fds[cp].fdpip[1] <= 0) {
                                    printf("invalid pipe write end %d for worker %d\n", fds[cp].fdpip[1], cp);
                                    exit(1);
                                }    
                                //printf("before dup2: writing end is %d\n", fds[cp].fdpip[1]);
                                dup2(fds[cp].fdpip[1], STDOUT_FILENO);
                                //printf("seg\n");
                                close(fds[cp].fdpip[1]);
                                //printf("//seg 2\n");

                                execlp("./worker", "./worker", tsou, ttar, "ALL", "FULL", NULL);
                                perror("exec");
                            }
                        } else {
                            add_que(que, tsou, ttar, 5, 0, 0, "");
                        }
                        //tmp->active = 1;
                        printf("%s Monitoring started for: %s\n", buffer, tsou);
                        snprintf(resp, sizeof(resp), "%s Added directory: %s -> %s\n%s Monitoring started for: %s\n",buffer, tsou, ttar, buffer, tsou);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                        fprintf(fp_log, "%s\n", resp);
                    }
                } else if (strcmp(strncpy(des, command, 6), "cancel") == 0) {
                    //printf("cancel now\n");
                    int fr = 0;
                    int count = 7;
                    char tsou[1024];
                    while(command[count] != '\n') {
                        tsou[fr++] = command[count++];
                    }
                    tsou[fr] = '\0';
                    //printf("path to delete %s \n", tsou);
                    //list = delete_sync_info(list, tsou, fdinot);
                    sync_info *cur = list;
                    while(cur != NULL) {
                        if (strcmp(cur->sou, tsou) == 0) {
                            break;
                        }
                        cur = cur->next;
                    }
                    if (cur != NULL && cur->active == 1) {
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Monitoring stopped for: %s\n", buffer, tsou);
                        snprintf(resp, sizeof(resp), "%s Monitoring stopped for: %s\n", buffer, tsou);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                        fprintf(fp_log, "%s\n", resp);
                        inotify_rm_watch(fdinot, cur->wd);
                        cur->active = 0;
                        cur->status = 0;
                    } else {
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Directory not monitored: %s\n", buffer, tsou);
                        snprintf(resp, sizeof(resp), "%s Directory not monitored: %s\n", buffer, tsou);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                        
                    }
                } else if (strncmp(command, "sync", 4) == 0) {
                    //printf("sync\n");
                    int fr = 0;
                    int count = 5;
                    char tsou[1024];
                    while(command[count] != '\n') {
                        tsou[fr++] = command[count++];
                    }
                    tsou[fr] = '\0';
                    sync_info *cur = list;
                    while(cur != NULL) {
                        if (strcmp(cur->sou, tsou) == 0) {
                            break;
                        }
                        cur = cur->next;
                    }
                    if (cur == NULL || cur->active == 0) {
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Directory not monitored: %s\n", buffer, tsou);
                        snprintf(resp, sizeof(resp), "%s Directory not monitored: %s\n", buffer, tsou);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                        
                    } else if (cur->status == 1) {
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Sync already in progress %s\n", buffer, tsou);
                        snprintf(resp, sizeof(resp), "%s Sync already in progress %s\n", buffer, tsou);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                        
                    } else if (cur->status == 0) {
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Syncing directory: %s -> %s\n", buffer, tsou, cur->tar);
                        snprintf(resp, sizeof(resp), "%s Syncing directory: %s -> %s\n", buffer, tsou, cur->tar);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                        fprintf(fp_log, "%s\n", resp);
                        int s, p;
                        if (actwork >= worker_limit) {
                            sync_info *new_node = malloc(sizeof(sync_info));
                            strcpy(new_node->sou, cur->sou);
                            strcpy(new_node->tar, cur->tar);
                            //strcpy(new_node->nm, nm);
                            new_node->status = 5;
                            time_t now;
                            struct tm *timeinfo;
                            char buffer[32];
                            time(&now);
                            timeinfo = localtime(&now);
                            strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", timeinfo);
                            strcpy(new_node->last_sync, buffer);
                            new_node->active = cur->active;
                            new_node->error_count = cur->error_count;
                            new_node->next = que;
                            que = new_node;
                            wait(&s);
                        }else if (actwork < worker_limit) {
                            actwork++;
                            for (int i = 0; i<worker_limit;i++) {
                                //printf("seg\n");
                                if (fds[i].act == 0) {
                                    cp = i;
                                    break;
                                }
                            }
                            //printf("%d  \n", cp);
                            fds[cp].act = 1;
                            if (pipe(fds[cp].fdpip) == -1) {
                                printf("invalid pipe\n");
                                perror("pipe");
                            }
                            //printf("pipe created: read end %d write end %d\n", fds[cp].fdpip[0], fds[cp].fdpip[1]);

                            
                            fcntl(fds[cp].fdpip[0], F_SETFL, O_NONBLOCK);
                            //printf("seg\n");
                            p = fork();
                            if (p == -1) {
                                perror("fork");
                            }
                            if (p != 0) {
                                //printf("parent here\n");
                                close(fds[cp].fdpip[1]);
                                fds[cp].prid = p;
                                set_syncp(list, tsou, p);
                            } else {
                                //printf("child here\n");
                                close(fds[cp].fdpip[0]);
                                //printf("invalid pipe write end %d for worker %d\n", fds[cp].act, cp);
                                if (fds[cp].fdpip[1] <= 0) {
                                    printf("invalid pipe write end %d for worker %d\n", fds[cp].fdpip[1], cp);
                                    exit(1);
                                }    
                                //printf("before dup2: writing end is %d\n", fds[cp].fdpip[1]);
                                dup2(fds[cp].fdpip[1], STDOUT_FILENO);
                                //printf("seg\n");
                                close(fds[cp].fdpip[1]);
                                //printf("//seg 2\n");

                                execlp("./worker", "./worker", tsou, cur->tar, "ALL", "FULL", NULL);
                                perror("exec");
                            }
                        }
                        waitpid(p, &s, 0);
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", timeinfo);
                        strcpy(cur->last_sync, buffer);

                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Sync completed: %s -> %s Errors: %d\n", buffer, tsou, cur->tar, cur->error_count);
                        snprintf(resp, sizeof(resp), "%s Sync completed: %s -> %s Errors: %d\n", buffer, tsou, cur->tar, cur->error_count);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                        fprintf(fp_log, "%s\n", resp);
                    }

                } else if (strncmp(command, "status", 6) == 0) {
                    int fr = 0;
                    int count = 7;
                    char tsou[1024];
                    while(command[count] != '\n') {
                        tsou[fr++] = command[count++];
                    }
                    tsou[fr] = '\0';
                    sync_info *cur = list;
                    while(cur != NULL) {
                        if (strcmp(cur->sou, tsou) == 0) {
                            break;
                        }
                        cur = cur->next;
                    }
                    if (cur == NULL || cur->active == 0) {
                        time(&now);
                        timeinfo = localtime(&now);
                        strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", timeinfo);
                        printf("%s Directory not monitored: %s\n", buffer, tsou);
                        snprintf(resp, sizeof(resp), "%s Directory not monitored: %s\n", buffer, tsou);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                    } else {
                        get_time(buffer);                    
                        printf("%s Status requested for %s\nDirectory: %s\nTarget: %s\nLast sync: %s\nErrors: %d\nStatus: Active\n", buffer, cur->sou, cur->sou, cur->tar, cur->last_sync, cur->error_count);
                        snprintf(resp, sizeof(resp), "%s Status requested for %s\nDirectory: %s\nTarget: %s\nLast sync: %s\nErrors: %d\nStatus: Active\n", buffer, cur->sou, cur->sou, cur->tar, cur->last_sync, cur->error_count);
                        if (write(pdout, resp, strlen(resp)) == -1) {
                            perror("write");
                            break;
                        }
                    }
                }



            }
            if (flag) break;
        }
        // for (int i =0;i<actwork;i++) {
        //     //printf("start\n");
        //     if (fds[i].act && FD_ISSET(fds[i].fdpip[0], &read_fds)) {
        //         char rep[9000];
        //         int n = read(fds[i].fdpip[0], rep, sizeof(rep) -1);
        //         printf("trigger\n");
        //         if (n>0) {
        //             rep[n] = 0;
        //             printf("from worker %s", rep);
        //         } else {
        //             close(fds[i].fdpip[0]);
                    
        //             //fds[i].act = 0; 
        //             //actwork--;
        //             //printf("idk\n");
        //         }
                
        //     }
        // }
        
    }
    
    close(fdinot);
    close(pdin);
    unlink("fss_in");
    unlink("fss_out");
    fclose(fp);
    fclose(fp_log);
    return 0;
}
