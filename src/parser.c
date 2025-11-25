#include <dirent.h>
#include <stddef.h>

int main (int argc, char *argv[]) {
    DIR *dirp;
    struct dirent *dp;
    const char *dirpath = argv[1];
    dirp = opendir(dirpath);
    if (dirp == NULL) {
        //nivel automático
        return;
    }
    for (;;) {
        dp = readdir(dirp);
        if (dp == NULL)
            break;
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue; 
            printf("%s\n", dp->d_name);
    }
}