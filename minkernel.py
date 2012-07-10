import kconfiglib
import os
import re
import shlex
import socket
import subprocess
import sys

# Timeout in seconds before a boot is considered unsuccessful
timeout = 10

# Kernel command line
kernel_cmdline = \
"root=/dev/sda1 rootfstype=ext2 init=/bin/start " \
"video=vesa:ywrap,mtrr vga=0x344 console=/dev/ttyS0 console=tty0 rw"

# Command to start QEMU with
qemu_cmd = """
qemu -hda ../pcdisk.img
     -vga std
     -serial stdio
     -net user -net nic,model=e1000
     -m 1024M
     -usbdevice mouse
     -kernel arch/x86/boot/bzImage
     -append '{0}'
""".format(kernel_cmdline)

# Command to compile the kernel with
compile_cmd = "make CROSS_COMPILE=i686-linux- -j8"

# Headers of sections listing essential/unessential symbols in the
# .config's
req_header     = "Required:"
not_req_header = "Not required:"

# Names of symbols we have determined must be enabled in order for the
# Devices SDK to be functional
req = set()

# Names of symbols we have determined need not be enabled, mapped to the
# size in bytes of the kernel image saved by disabling them
not_req = {}

#
# Helper functions
#

def run_cmd(cmd, cwd = None):
    """Runs the command 'cmd', split up as in a Bourne shell, and returns
    its exit code. Correctly handles commands that generate a lot of
    output."""
    try:
        process = subprocess.Popen(shlex.split(cmd),
                                   cwd = cwd,
                                   stdout = subprocess.PIPE,
                                   stderr = subprocess.PIPE)
        process.communicate()
    except OSError, e:
        sys.stderr.write("Failed to execute '{0}': {1}".format(cmd, e))
        sys.exit(1)

    return process.returncode

def get_process(cmd):
    """Like run_cmd(), but return the process object instead of waiting for
    the process to finish."""
    try:
        process = subprocess.Popen(shlex.split(cmd),
                                   stdout = subprocess.PIPE,
                                   stderr = subprocess.PIPE)
        return process
    except OSError, e:
        sys.stderr.write("Failed to execute '{0}': {1}".format(cmd, e))
        sys.exit(1)

def write_config(conf, filename):
    not_req_items = sorted(["{0} ({1})".format(sym, size) for
                            (sym, size) in not_req.iteritems()])

    conf.write_config(filename,
                      "\n".join([req_header] +
                                sorted(req) +
                                [not_req_header] +
                                not_req_items))

# Main script logic

# Set up environment variables used by Kconfig files
os.environ["ARCH"] = "i386"
os.environ["SRCARCH"] = "x86"
os.environ["KERNELVERSION"] = "2"

print "Parsing Kconfig"
conf = kconfiglib.Config("Kconfig", base_dir = ".")

# Check if we should resume from a configuration in configs/

if not os.path.exists("configs"):
    os.mkdir("configs")

# Find .config with maximum index in configs/ (presumably the most recent
# one)
conf_index = -1
for c in os.listdir("configs"):
    match = re.match(r"^config_(\d+)$", c)
    if match:
        index = int(match.group(1))
        conf_index = max(conf_index, index)

if conf_index == -1:
    print "Starting from known good configuration"
    run_cmd("cp .goodconfig .config")
else:
    print "Resuming from configs/config_{0}".format(conf_index)
    run_cmd("cp configs/config_{0} .config".format(conf_index))

conf.load_config(".config")

print "Parsing .config header"

DISCARD, ADD_REQ, ADD_NOT_REQ = 0, 1, 2
state = DISCARD

lines = conf.get_config_header().splitlines()
for line in lines:
    line = line.strip()

    if line == "":
        continue

    if line in (req_header, not_req_header):
        state = ADD_REQ if line == req_header else ADD_NOT_REQ
        continue

    if state == DISCARD:
        continue

    sym_name = line.split()[0]

    (req if state == ADD_REQ else not_req).add(line)

# Compile initial kernel to get reference for size (it is assumed to boot
# successfully)

print "Compiling initial kernel (assumed to be good) to get reference" \
      "for size"

run_cmd(compile_cmd)

# Size of the previously built kernel in bytes
old_kernel_size = os.path.getsize("arch/x86/boot/bzImage")

# Main loop - disable symbols one by one and test the resulting kernels

while True:
    print "Disabling modules"
    # Run in a loop in case disabling one module enables other modules
    # which can then be disabled in turn (unlikely, but just to be safe)
    while True:
        for sym in conf:
            if not sym.is_choice_item() and \
               sym.calc_value() == "m"  and \
               sym.get_lower_bound() == "n":
                sym.set_value("n")
        else:
            break

    # Search for symbol to disable
    for sym in conf:
        if not sym.is_choice_item() and \
           not sym.get_name() in req and \
           sym.get_type() in (kconfiglib.BOOL, kconfiglib.TRISTATE) and \
           sym.calc_value() == "y":

            val = sym.calc_value()
            # Get the lowerst value the symbol can be assigned, with "n",
            # "m", and "y" being arranged from lowest to highest
            lower_bound = sym.get_lower_bound()

            if lower_bound is not None and \
               kconfiglib.tri_less(lower_bound, val):
                print "Lowering the value of {0} from '{1}' to '{2}'" \
                      .format(sym.get_name(), val, lower_bound)
                sym.set_value(lower_bound)
                break
    else:
        print "Done - no more symbols can be disabled"
        break

    # Test the kernel

    write_config(conf, ".config")

    print "Compiling kernel"
    run_cmd(compile_cmd)

    # Compare size of kernel to size of previous kernel to make sure it has
    # decreased

    kernel_size = os.path.getsize("arch/x86/boot/bzImage")
    if kernel_size >= old_kernel_size:
        print "Disabling {0} did not decrease the size of the kernel. " \
              "Re-enabling it.".format(sym.get_name())
        sym.set_value("y")
        req.add(sym.get_name())
        continue

    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
    # This avoids some situations in which we fail to reuse the port after
    # restarting the script because the connection is in the TIME_WAIT
    # state.
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.settimeout(timeout)

    # The empty string in the host portion is treated like INADDR_ANY
    listen_sock.bind(("", 1234))
    listen_sock.listen(10)

    print "Running system in QEMU"
    qemu_process = get_process(qemu_cmd)

    print "Waiting for the system to contact us"

    try:
        sock, addr = listen_sock.accept()
        boot_successful = True
        sock.close()
    except socket.timeout, e:
        boot_successful = False

    if boot_successful:
        # Write a new configuration
        print "Boot successful! Disabling {0} saved {1} bytes.".\
              format(sym.get_name(), old_kernel_size - kernel_size)
        conf_index += 1
        not_req[sym.get_name()] = old_kernel_size - kernel_size
        write_config(conf, "configs/config_{0}".format(conf_index))
        old_kernel_size = kernel_size
    else:
        print "Boot failed! Turning {0} back on".format(sym.get_name())
        sym.set_value("y")
        req.add(sym.get_name())

    print "Terminating QEMU"
    qemu_process.terminate()

    listen_sock.close()
