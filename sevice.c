#include <unistd.h>
#include <sys/types.h>
#include <syslog.h>
#include <linux/sched.h>

char *dirpaths[] = { "" };
#define ARRAY_SIZE(arr) sizeof(dirpaths)/sizeof(char*)

int main (int argc, char** argv) {
    pid_t pid;
    // global init

    openlog("Mephistopheles", LOG_PID, LOG_USER);

    pid = daemon(0, 0);
    if (pid < 0) {
        syslog(LOG_CRIT, "Failed to fork!");
        closelog();
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // child
        while (1) {
            DIR *dir;
            size_t i;
            struct dirent *ep;

            for (i = 0; i < ARRAY_SIZE(dirpaths); i++) 
            {
                dir = opendir(dirpaths[i]);
                if(dir) {
                    while ((ep = readdir(dir)) != NULL) {
                        // unlink?
                        DO_SOMETHING(ep);
                        syslog(LOG_DEBUG, "Found something!");
                    }
                    closedir(dir);
                }
            }
            sched_yield();
        }
    }
}