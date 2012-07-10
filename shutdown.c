#include <errno.h>
#include <linux/reboot.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <unistd.h>

static void fail(const char *msg) __attribute__ ((noreturn));

int main() {
    puts("Sending SIGTERM to all processes..");
    if( kill(-1, SIGTERM) < 0 )
        fail("Failed to send SIGTERM to all processes");

    /* Sleep for a second to give processes time to quit. We assume all
       processes terminate quickly in Awesom-O (except perhaps for the
       /bin/opera process, which shouldn't be running when we invoke shutdown
       anyway). */
    sleep(1);

    puts("Killing off any remaining processes with SIGKILL..");
    if( kill(-1, SIGKILL) < 0 )
        fail("Failed to kill processes with SIGKILL");

    /* Remount root read-only and flush filesystem buffers to make
       sure everything gets out to disk. */

    puts("Remounting root read-only...");
    if( mount("", "/", NULL, MS_REMOUNT | MS_RDONLY, NULL) < 0 )
        fail("Failed to remount root read-only");

    puts("Flushing filesystem buffers...");
    sync();

    puts("Shutting down...");
    /* This will programmatically turn off the power if the system supports
       it. (PC usually does provided ACPI support has been compiled in;
       BeagleBoard does not.) */
    reboot(LINUX_REBOOT_CMD_POWER_OFF);

    /* reboot() will not return upon success, so us being here means
       something went wrong. */
    fail("Shutdown failed");

    return 1;
}

static void fail(const char *msg) {
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(1);
}
