#define _GNU_SOURCE
#include <bits/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
// #include <sys/vm86.h>

struct dos_state {
    uint16_t ip;
    uint16_t cs;
} __attribute__((packed));

__attribute__((noreturn)) void fatal() {
    while (1) {
        sleep(1);
    }
}

#define CHECKED(expr) { int rc; if ((rc = (expr)) < 0) { goto out; } }

int copy_file(const char* src, const char* dst) {
    int rc, src_fd = -1, dst_fd = -1;
    struct stat st;

    CHECKED(src_fd = open(src, O_RDONLY | O_CLOEXEC));
    CHECKED(fstat(src_fd, &st));
    CHECKED(dst_fd = open(dst, O_WRONLY | O_CREAT | O_CLOEXEC | O_TRUNC, st.st_mode));

    size_t len = st.st_size;

    while (len > 0) {
        ssize_t copied = 0;
        CHECKED(copied = copy_file_range(src_fd, NULL, dst_fd, NULL, len, 0));
        len -= copied;
    }

    rc = 0;

out:
    if (src_fd >= 0) {
        close(src_fd);
    }

    if (dst_fd >= 0) {
        close(dst_fd);
    }

    return rc;
}

int install_busybox() {
    pid_t install_pid = fork();

    if (install_pid < 0) {
        return install_pid;
    }

    if (install_pid == 0) {
        char busybox[] = "/bin/busybox";
        char install[] = "--install";
        char* argv[] = { busybox, install, NULL };
        execve(busybox, argv, NULL);
        perror("execve");
        exit(errno);
    }

    while (1) {
        int rc, wstat;

        if ((rc = waitpid(install_pid, &wstat, 0)) < 0) {
            return rc;
        }

        if (WIFEXITED(wstat)) {
            return WEXITSTATUS(wstat);
        }
    }
}

void initialize() {
    printf("\nHello world from DOS Subsystem for Linux!\n\n");

    // setup ramdisk for root partition
    // TODO - maybe a persistent ext4 fs on a loop device?

    if (mount("", "/doslinux/rootfs", "ramfs", 0, NULL)) {
        perror("mount ramfs");
        fatal();
    }

    // setup procfs

    if (mkdir("/doslinux/rootfs/proc", 0755)) {
        perror("mkdir /proc");
        fatal();
    }

    if (mount("", "/doslinux/rootfs/proc", "proc", 0, NULL)) {
        perror("mount /proc");
        fatal();
    }

    // setup bin and copy busybox into place

    if (mkdir("/doslinux/rootfs/bin", 0755)) {
        perror("mkdir /bin");
        fatal();
    }

    if (copy_file("/doslinux/busybox", "/doslinux/rootfs/bin/busybox")) {
        perror("copy busybox");
        fatal();
    }

    // setup mnt and pivot root

    if (mkdir("/doslinux/rootfs/mnt", 0755)) {
        perror("mkdir /mnt");
        fatal();
    }

    if (mkdir("/doslinux/rootfs/mnt/c", 0755)) {
        perror("mkdir /mnt/c");
        fatal();
    }

    if (syscall(SYS_pivot_root, "/doslinux/rootfs", "/doslinux/rootfs/mnt/c")) {
        perror("pivot_root");
        fatal();
    }

    // setup remaining bin dirs and install busybox

    if (mkdir("/sbin", 0755)) {
        perror("mkdir /sbin");
        fatal();
    }

    if (mkdir("/usr", 0755)) {
        perror("mkdir /usr");
        fatal();
    }

    if (mkdir("/usr/bin", 0755)) {
        perror("mkdir /usr/bin");
        fatal();
    }

    if (mkdir("/usr/sbin", 0755)) {
        perror("mkdir /usr/sbin");
        fatal();
    }

    if (install_busybox()) {
        perror("install busybox");
        fatal();
    }

    // setup /dev/mem

    if (mkdir("/dev", 0755)) {
        perror("mkdir /dev");
        fatal();
    }

    if (mknod("/dev/mem", S_IFCHR | 0600, makedev(1, 1))) {
        perror("mknod mem");
        fatal();
    }
}

__attribute__((noreturn)) void exec_shell() {
    char sh[] = "sh";
    char* argv[] = { sh, NULL };

    char path[] = "PATH=/usr/bin:/usr/sbin:/bin:/sbin";
    char* envp[] = { path, NULL };
    execve("/bin/busybox", argv, envp);

    perror("execve");
    fatal();
}

int main() {
    initialize();

    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memfd < 0) {
        perror("open mem");
        fatal();
    }

    // map the entire first MiB of memory in :)
    if (mmap(0, 0x110000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_FIXED, memfd, 0) == MAP_FAILED) {
        perror("mmap");
        fatal();
    }

    // struct dos_state* dos = (void*)0x100000;
    // printf("CS:IP => %04x:%04x\n\n", dos->cs, dos->ip);

    // uint8_t* vga = (void*)0xb8000;
    // for (int y = 0; y < 25; y++) {
    //     for (int x = 0; x < 80; x++) {
    //         vga[(y * 80 + x) * 2 + 1] = 0x7a;
    //     }
    // }

    // vm86()

    chdir("/mnt/c");
    exec_shell();
}