#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    int fid = STDIN_FILENO;
    while (1) {
        const int nbuf = 1024 * 1024;
        char buf[nbuf];
        int nr = read(fid, buf, nbuf);
        printf("Read %i\n", nr);
        if (nr == nbuf)
            continue;
        if (nr == 0) {
            printf("EOF!\n");
            break;
        }
        if (nr == -1) {
            printf("Failed: %s\n", strerror(errno));
            exit(errno);
        }
        // short read.
    }
    return 0;
}
