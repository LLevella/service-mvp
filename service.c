#include <unistd.h>
#include <syslog.h>
#include <fnmatch.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

// Confined, will add marks
char *confined_dirs[] = {
    "/home/user/test/",
};

// remove files from here
char *remove_dirs[] = { 
    "/home/user/Документы",
    "/home/user/Рабочий стол"
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

#ifdef DEBUG
	int cons_fd;
#endif
enum action_type {
    MARK_FILE,
    REMOVE_FILE
};

static char* make_full_path(char* dir, char* filename, char* ext_add)
{
    char *fullname;
    fullname = malloc(strlen(dir) + 1 + strlen(filename) + (ext_add ? strlen(ext_add) : 0) + 1);
    if (!ext_add)
    	sprintf(fullname, "%s/%s", dir, filename);
    else
        sprintf(fullname, "%s/%s%s", dir, filename, ext_add);
    return fullname;
}

static void file_action(char* dir, struct dirent *ep, enum action_type action)
{
    printf("%s %s%s\n", (action == REMOVE_FILE ? "remove" : "mark"), dir, ep->d_name);
    switch(action) {
        case MARK_FILE:
            {
                char** m_ext = mark_patterns;
                for (; *m_ext; m_ext++)
                    if(fnmatch(*m_ext, ep->d_name, 0)) {
                        //create mark
                        char* fmark_name = make_full_path(dir, ep->d_name, file_mark_ext);
                        struct stat filestat;
                        if (stat(fmark_name, &filestat) < 0) {
			                FILE* fmark = fopen(fmark_name, "w");
                            fprintf(fmark, "00000004 00000004");
                            fclose(fmark);

                			printf("Marked '%s%s' by '%s'", dir, ep->d_name, fmark_name);
                            syslog(LOG_DEBUG, "Marked '%s/%s' by '%s'", dir, ep->d_name, fmark_name);
                        }
                        free(fmark_name);
                    }
		break;
            }
        case REMOVE_FILE:
            {
                char** ext;
                for (ext = remove_patterns; *ext; ext++)
                    if(fnmatch(*ext, ep->d_name, 0)) {
                        //remove
                        char *full_path = make_full_path(dir, ep->d_name, NULL);
                        unlink(full_path);
                        syslog(LOG_DEBUG, "Removed '%s'", full_path);
                        free(full_path);
                    }
		break;
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
	    if(!strcmp(ep->d_name, ".") || !strcmp(ep->d_name, ".."))
		    continue;
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
			char** exts = action == MARK_FILE ? mark_patterns : remove_patterns;
                        for (ext = exts; *ext; ext++)
                            if (fnmatch(*ext, ep->d_name, 0) == 0)
                                file_action(path, ep, action);
                    }
                default: ;
            }
        }
        closedir(dir);
    }
} 

#define ARRAY_SIZE(arr) sizeof(arr)/sizeof(char*)

int main (int argc, char** argv) {
    pid_t pid;
    // global init

    cons_fd = dup(STDOUT_FILENO);
    /*
    pid = daemon(0, 0);
    if (pid < 0) {
        syslog(LOG_CRIT, "Failed to fork!");
        closelog();
	perror("daemon");
        exit(EXIT_FAILURE);
    }*/
    pid = 0;

    if (pid == 0) {
	openlog("Mephisto", LOG_CONS | LOG_PID, LOG_USER);

#ifdef DEBUG
	setlogmask(LOG_UPTO(LOG_DEBUG));
#endif

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
