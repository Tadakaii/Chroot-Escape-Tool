# Chroot Escape Tool

## Overview

The Chroot Escape Tool is a proof-of-concept utility designed to demonstrate various classic techniques for escaping a chroot(2) environment on Unix-like systems.

This tool attempts to escape the chroot environment using three distinct methods, each exploiting a different weakness in how chroot isolation can be circumvented when the process retains root privileges inside the jail.

**Important:** Chroot is not a security mechanism. It was never designed to contain privileged processes. This tool illustrates why relying solely on chroot for security is insufficient.

---

## Technical Details

The program implements three independent escape methods. Each is attempted sequentially until one succeeds. All methods require the process to be running with **UID 0 (root)** inside the chroot jail.

### Method 1: Double chroot with Directory Traversal (`method_chdir_up`)

This is the classic chroot escape technique that relies on the fact that the kernel does not track the "jail" origin after the first chroot call.

1. The process changes its current working directory to `/` within the jail.
2. It creates a new directory named `escape_tunnel` inside the jail root.
3. It calls `chroot("escape_tunnel")`, establishing a new root at that subdirectory.
4. From this new root, the process repeatedly calls `chdir("..")` to walk upwards. Because the kernel's internal root pointer was moved to `escape_tunnel`, the `..` entry at that level points to the **original host root** that existed before the first chroot was applied.
5. Once the loop detects the presence of the target shell (`bin/sh`) by repeatedly checking `stat()`, it performs a final `chroot(".")` to lock the root directory to the actual host root.
6. Finally, the program executes a shell, giving the user full access to the host filesystem.

### Method 2: File Descriptor to Original Root (`method_fchdir`)

This method leverages the fact that file descriptors opened before entering a chroot jail retain a reference to the original filesystem namespace.

1. Before the jail is established, the process opens a file descriptor to the host's `/` directory using `open("/", O_RDONLY)`. (In a real scenario, the jail is already active, so this method relies on the process having opened such a descriptor earlier or being able to open `/` before the first chroot call. The POC simulates this by opening the descriptor while inside the jail but before creating the new chroot environment.)
2. The process changes directory to `/` inside the jail, creates the `escape_tunnel` directory, and chroots into it.
3. It then calls `fchdir(fd)` where `fd` is the descriptor pointing to the original host root. This changes the current working directory of the process to the **host** root despite the chroot confinement.
4. A final `chroot(".")` is executed, breaking out completely.
5. The file descriptor is closed and a shell is spawned.

### Method 3: Block Device Access via mknod (`method_mknod`)

This method demonstrates that a root process inside a chroot jail can still create device nodes using `mknod(2)`, provided the jail contains a `/dev` directory or the process has permission to create files.

1. The program attempts to create a block device node for the primary disk (e.g., `/dev/sda` for SCSI/SATA devices or `/dev/hda` for older IDE devices).
2. If `mknod` succeeds, the process has direct block-level access to the underlying host storage.
3. It then attempts to invoke `debugfs` (a filesystem debugger for ext2/ext3/ext4) with write access to the device. If `debugfs` is present in the jail, this can be used to read or modify arbitrary files on the host filesystem.
4. This method does not automatically spawn a shell; it only confirms that raw device access is possible. An attacker with such access could mount filesystems, read sensitive data, or inject malicious code into host binaries.

---

## Compilation

The source code is contained in a single file named `main.c`. Compilation requires a C compiler (gcc or clang) and standard development headers.

**Command:**

```bash
gcc main.c -o chroot-escape-tool
```

This produces an executable named `chroot-escape-tool`. No special libraries or build systems are required.

To compile with additional hardening flags disabled (for easier testing in controlled environments):

```bash
gcc main.c -o chroot-escape-tool -Wall -Wextra -O2
```

---

## Usage

### Prerequisites

- The tool **must** be executed as root (UID 0) inside the chroot jail.
- The chroot environment must provide a working shell at `/bin/sh` for methods 1 and 2 to spawn a shell upon success.
- For method 3 to be fully exploitable, `debugfs` must be installed inside the jail. Even without it, the mknod creation itself demonstrates a privilege boundary weakness.

### Running the Tool

Copy the compiled binary into the chroot jail and execute it:

```bash
./chroot-escape-tool
```

If successful, the program will print a message indicating which method worked and then drop you into a root shell **outside** the chroot jail. You can verify the escape by examining the filesystem or checking the output of `ls /`.

### Example Output

```
=== chroot escape POC ===
Method 1 failed, trying method 2...
$
```

After this prompt, you are outside the jail.

---

## Limitations and Notes

- The tool only works if the process is running with effective UID **0**. Non-root processes cannot perform the `chroot()` system call.
- Modern systems may employ additional protections (e.g., mount namespaces, seccomp filters, Linux Security Modules) that prevent these escape techniques. The tool is effective against traditional chroot setups without such hardening.
- Method 3 requires the `mknod` syscall to be permitted and the presence of a `/dev` directory where device files can be created. Some chroot configurations mount a `tmpfs` on `/dev` or use `devtmpfs`, which may still allow device node creation.
- The tool cleans up after itself (removes the `escape_tunnel` directory and device nodes) only partially. The `escape_tunnel` directory remains in the jail after successful execution; it can be removed manually
