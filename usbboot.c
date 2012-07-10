#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>

/* The "real" init process to replace ourself with. */
#define INIT "/bin/start"

/* In case we fail to mount the root, the number of milliseconds to wait
   before trying again. */
#define MOUNT_RETRY_MS 100

/* Turns a macro into a string corresponding to its expansion. */
#define STR(x) STR_(x)
#define STR_(x) #x

static void print_with_errno(const char *msg);
static void fail(const char *msg) __attribute__ ((noreturn));
static void go_to_sleep() __attribute__ ((noreturn));

/* Leave the characters non-const to avoid annoying casts.
   (execve() never touches the strings.) */

/* execve() arguments for running the real init process once we
   chdir() + chroot() into the root filesystem. */
static char *const init_args[] = {INIT, NULL};

/* Empty environment for execve() call. */
static char *const empty_env[] = {NULL};

int main(int argc, char *argv[]) {
    const struct timespec mount_retry_ts = {0, MOUNT_RETRY_MS * 1000000};
    int failed_mounts = 0;
    char msgbuf[100];
    int msglen;
    int serial_fd;
    struct termios serial_opts;

    /* Repeatedly try to mount the root until the USB device becomes available.
       In a production system we would mount by UUID here, and listening for
       kernel events might be a bit more graceful. */

    while( mount("/dev/sdb1", "root/", "ext2", 0, NULL) < 0 ) {
        print_with_errno("Could not mount the root filesystem - retrying "
                         "in " STR(MOUNT_RETRY_MS) " milliseconds");
        nanosleep(&mount_retry_ts, NULL);
        ++failed_mounts;
    }

    /* Remove files before chroot'ing into the new file system so that they
       do not use up memory unnecessarily. */

    unlink("usbboot");

    /* Switch to the "real" root. */

    if( chdir("/root") < 0 )
        fail("Failed to chdir() into the root filesystem");

    if( chroot("/root") < 0 )
        fail("Failed to chroot() into the root filesystem");

    /* Record time spent waiting for root filesystem to become available. (This
       was used for benchmarking in the thesis.) */

    serial_fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_NDELAY);
    tcgetattr(serial_fd, &serial_opts);
    cfsetospeed(&serial_opts, B115200);
    tcsetattr(serial_fd, TCSANOW, &serial_opts);
    msglen = sprintf(msgbuf,
                     "Waited about %d milliseconds for "
                     "the root to become available\n",
                     failed_mounts * MOUNT_RETRY_MS);
    write(serial_fd, msgbuf, msglen);
    close(serial_fd);

    /* Execute the real init process. */
    execve(INIT, init_args, empty_env);

    /* The above should replace our process, so us being here means
       something went wrong. */
    fail("Failed to execute the init process " INIT
         " after chdir()+chroot()'ing into the root filesystem");
}

static void print_with_errno(const char *msg) {
    char error_msg[256];
    ssize_t write_res;
    int msglen;
    int fd;

    fd = open("/dev/console", O_WRONLY | O_NOCTTY);
    if( fd == -1 )
        return;

    msglen =
        snprintf(error_msg, sizeof(error_msg),
                 "%s\n errno = %d (%s)\n", msg, errno, strerror(errno));
    write_res = write(fd, error_msg, msglen > sizeof(error_msg) ?
                                     sizeof(error_msg) : msglen);
    if( write_res != -1 ) {
        /* Write an extra newline just to be sure the message becomes
           visible in case it was truncated. */

        /* Assign write_res to suppress GCC warning. */
        write_res = write(fd, "\n", 1);
    }

    close(fd);
}

static void fail(const char *msg) {
    print_with_errno(msg);
    go_to_sleep();
}

static void go_to_sleep() {
    const struct timespec ts = {60, 0};

    for(;;)
        nanosleep(&ts, NULL);
}
