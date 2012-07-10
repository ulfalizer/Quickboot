#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The TTY (terminal device) to which the standard streams of the
   spawned shell should be bound; this also becomes the controlling TTY
   of the shell. Using the startup default of /dev/console would break
   job control (Ctrl-C, etc.) in the shell as /dev/console can not be
   used as a controlling terminal.

   /dev/tty1 is the first Linux virtual console. */
#define TTY "/dev/tty1"

/* The initialization script to run. */
#define INITSCRIPT "/etc/initscript"

/* Leave the characters non-const to avoid annoying casts.
   (execve() never touches the strings.) */

/* execve() arguments for running INITSCRIPT. */
static char *const initscript_args[]
    = {"/bin/busybox", "ash", INITSCRIPT, NULL};

/* execve() arguments for starting an interactive shell. */
static char *const ishell_args[] = {"/bin/busybox", "ash", NULL};

/* Empty environment for execve() calls. */
static char *const empty_env[] = {NULL};

static void fail(const char *msg) __attribute__ ((noreturn));
static void go_to_sleep() __attribute__ ((noreturn));

int main(int argc, char *argv[]) {
    /* SIGCHLD is a signal sent when a child process exits. As we do not
       care about the exit status of our children at this point we
       explicitly ignore this signal, which according to POSIX.1-2001,
       honored by Linux 2.6, will prevent child processes from becoming
       zombies. */

    /* Use handy GCC-specific struct initialization syntax. */
    struct sigaction act = { .sa_handler = SIG_IGN };
    if( sigaction(SIGCHLD, &act, NULL) < 0 )
        fail("Could not ignore SIGCHLD");

    /* Create a new session; we need this for job control in the shell. */
    if( setsid() < 0 )
        fail("Could not create new session");

    /* Rebind standard streams to TTY. */

    /* First close the old stdin/out/err (probably bound to /dev/console). */
    close(0); close(1); close(2);

    /* Ignore any errors from above. */
    errno = 0;

    /* This will use the lowest available file descriptor, i.e. 0 (=
       stdin), so the effect is to bind stdin to TTY. TTY will also become
       the controlling terminal. */
    if( open(TTY, O_RDWR | O_NONBLOCK, 0) != 0 )
        fail("Could not open " TTY " as stdin");

    /* Make stdout and stderr point to the same place as stdin by
       duplicating the stdin file descriptor. dup() always uses the lowest
       available descriptor, so this will bind 1 (stdout) and 2 (stderr),
       in that order. */
    if( dup(0) != 1 ) /* Assigns stdout. */
        fail("Failed to reassign stdout to " TTY);
    if( dup(0) != 2 ) /* Assigns stderr. */
        fail("Failed to reassign stderr to " TTY);

    /* Standard streams rebound! Now run INITSCRIPT or spawn an interactive
       shell. */

    if( argc == 2 && !strcmp(argv[1], "ishell") ) {
        /* Spawn an interactive shell if "ishell" was passed on the kernel
           command line; this is sometimes handy during development. */
        if( !fork() )
            if( execve("/bin/busybox", ishell_args, empty_env) == -1 )
                fail("Failed to launch interactive shell");
    }
    else {
        /* Run the init script. */
        if( !fork() )
            if( execve(INITSCRIPT, initscript_args, empty_env) == -1 )
                fail("Failed to run initialization script " INITSCRIPT);
    }

    /* The init process must not die, and we shouldn't busy-wait (init
       using ~100% CPU would be bad), so go to sleep. */
    go_to_sleep();
}

static void fail(const char *msg) {
    char error_msg[256];
    ssize_t write_res;
    int msglen;
    int fd;

    fd = open("/dev/console", O_WRONLY | O_NOCTTY);
    if( fd == -1 )
        go_to_sleep();

    msglen =
        snprintf(error_msg, sizeof(error_msg),
                 "%s\nerrno = %d (%s)\n", msg, errno, strerror(errno));
    write_res = write(fd, error_msg, msglen > sizeof(error_msg) ?
                                     sizeof(error_msg) : msglen);
    if( write_res != -1 ) {
        /* Write an extra newline just to be sure the message becomes
           visible in case it was truncated. */

        /* Assign write_res to suppress GCC warning. */
        write_res = write(fd, "\n", 1);
    }

    close(fd);
    go_to_sleep();
}

static void go_to_sleep() {
    const struct timespec ts = {60, 0};

    for(;;)
        nanosleep(&ts, NULL);
}
