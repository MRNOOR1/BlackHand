/*
 * ============================================================================
 * BlackHand OS — init.c  (PID 1)
 * ============================================================================
 *
 * WHAT IS THIS FILE?
 * ------------------
 * This is the very first program the Linux kernel runs after it finishes
 * loading. It gets Process ID 1 — PID 1 — which means it is the ancestor
 * of every single process that will ever run on this device. Nothing runs
 * before this file. Nothing is above it. The kernel executes it directly.
 *
 * The kernel finds this binary at /sbin/blackhand-init, set via the kernel
 * command line parameter: init=/sbin/blackhand-init
 *
 * WHAT DOES IT DO?
 * ----------------
 * init.c has exactly four responsibilities — nothing more:
 *
 *   1. MOUNT VIRTUAL FILESYSTEMS
 *      The kernel creates /proc, /sys, /dev, /run, and /tmp entirely in RAM.
 *      They are not on disk. Without them nothing works — no device files,
 *      no process information, no socket files for IPC, no temporary storage.
 *      init mounts all five before doing anything else.
 *
 *   2. SET UP SIGNAL HANDLERS
 *      When a child process dies, the kernel sends SIGCHLD to its parent.
 *      For PID 1 this means every orphaned process on the entire system
 *      becomes our responsibility. We register ksigchld_handler to reap
 *      dead processes by calling waitpid() — this prevents zombie processes
 *      from accumulating and eventually filling the process table.
 *      We also register ksigterm_handler for graceful shutdown.
 *
 *   3. SPAWN THE SERVICE MANAGER
 *      init does not start services directly. It spawns one child process —
 *      the service manager (blackhand-svc-mgr) — which takes over all
 *      service lifecycle management. This keeps init.c simple and focused.
 *
 *   4. WAIT FOREVER
 *      PID 1 can never exit. If it exits the kernel panics. After spawning
 *      the service manager, init enters an infinite loop calling pause(),
 *      which sleeps until a signal arrives. SIGCHLD wakes it to reap
 *      zombies, then it goes back to sleep.
 *
 * RELATIONSHIP TO OTHER FILES
 * ---------------------------
 *
 *   init.c  ──spawns──▶  service_manager.c
 *                              │
 *                              ├──starts──▶ blackhand-audio  (owns /run/bh-audio.sock)
 *                              ├──starts──▶ blackhand-storage (owns /run/bh-storage.sock)
 *                              ├──starts──▶ blackhand-modem  (owns /run/bh-modem.sock)
 *                              └──starts──▶ blackhand-ui     (connects to all sockets)
 *
 *   The socket files that services communicate through live in /run/ —
 *   which only exists because init.c mounted tmpfs there. So init.c is
 *   the prerequisite for all IPC communication in the system.
 *
 *   IPC (blackhand-ipc) provides the send_msg/recv_msg framing and the
 *   JSON-RPC dispatch layer that services use to talk to each other.
 *   init.c has no direct knowledge of IPC — it just ensures /run exists
 *   so the socket files can be created.
 *
 * WHY INIT IS SEPARATE FROM THE SERVICE MANAGER
 * -----------------------------------------------
 *   init.c must be kept as small and simple as possible. If it has a bug
 *   and crashes, the kernel panics — there is no recovery. The service
 *   manager is a normal child process. If it crashes, init can restart it.
 *   Separating concerns means bugs in service management cannot bring down
 *   the entire system.
 *
 * BOOT SEQUENCE
 * -------------
 *   Power on
 *     → BCM2711 bootloader loads kernel + device tree
 *     → Linux kernel initialises hardware, mounts root filesystem
 *     → kernel executes /sbin/blackhand-init  ← YOU ARE HERE
 *         → ksetup_signals()
 *         → kmount_vfs()
 *         → kspawn_services()   spawns service manager
 *         → for(;;) { pause(); }
 *
 * ============================================================================
 */

#include <stdio.h>     /* fprintf, perror                                    */
#include <stdlib.h>    /* exit, EXIT_FAILURE                                 */
#include <unistd.h>    /* fork, execv, pause, getpid, write                  */
#include <signal.h>    /* sigaction, SIGCHLD, SIGTERM, sig_atomic_t          */
#include <sys/wait.h>  /* waitpid, WNOHANG                                   */
#include <sys/mount.h> /* mount(), MS_NOSUID, MS_NOEXEC, MS_NODEV            */
#include <sys/stat.h>  /* mkdir                                              */
#include <string.h>    /* memset, strerror                                   */
#include <errno.h>     /* errno, EEXIST                                      */

/*
 * shutdown_requested — global flag set by ksigterm_handler.
 *
 * volatile: tells the compiler this variable can change at any time from
 * outside normal code flow (i.e. from a signal handler). Without volatile
 * the compiler might cache the value in a register and never see the update.
 *
 * sig_atomic_t: guaranteed by the C standard to be read/written atomically —
 * the CPU cannot be interrupted halfway through accessing it. The correct
 * type for variables shared between signal handlers and normal code.
 */
volatile sig_atomic_t shutdown_requested = 0;

/*
 * ksigchld_handler — reap all zombie child processes.
 *
 * The kernel invokes this function automatically whenever a child process
 * dies. We do not call it ourselves. The kernel calls it.
 *
 * When a process dies it does not disappear immediately. The kernel keeps
 * a small record of it — its PID and exit status — until the parent
 * acknowledges the death by calling waitpid(). Until then the dead process
 * sits in the process table as a "zombie". If zombies are never collected
 * they accumulate until the process table fills and no new processes can
 * be created.
 *
 * We loop until waitpid() returns 0 (no more children ready) because
 * multiple children may have died at once and signals are not queued —
 * we might receive one SIGCHLD for three deaths and must collect all three.
 *
 * waitpid(-1, NULL, WNOHANG):
 *   -1      = collect any child, not a specific one
 *   NULL    = we do not care about the exit status, just acknowledge death
 *   WNOHANG = do not block — return 0 immediately if no child is ready
 *
 * write() is used instead of printf/fprintf because stdio functions are
 * not async-signal-safe. write() is on the POSIX async-signal-safe list.
 */
static void ksigchld_handler(int sig)
{
	(void)sig; /* parameter required by signal handler signature but unused */
	while (1)
	{
		pid_t pid = waitpid(-1, NULL, WNOHANG);
		if (pid <= 0)
			break; /* 0 = no more zombies ready, -1 = error, both mean stop */
		write(2, "init: reaped child\n", 19);
	}
}

/*
 * ksigterm_handler — handle graceful shutdown request.
 *
 * The kernel or another process sends SIGTERM to request a clean shutdown.
 * Rather than shutting down immediately inside the signal handler (which is
 * unsafe), we set a flag and let the main loop handle the shutdown sequence.
 *
 * The main loop checks shutdown_requested after each pause() returns and
 * runs the clean shutdown sequence when it is set.
 */
static void ksigterm_handler(int sig)
{
	if (sig == SIGTERM)
	{
		shutdown_requested = 1;
	}
}

/*
 * ksetup_signals — register signal handlers with the kernel.
 *
 * This function runs once at startup. It fills in a struct sigaction and
 * calls sigaction() to tell the kernel: "when this signal arrives, call
 * this function." After this call returns the kernel knows about our
 * handlers and will invoke them automatically.
 *
 * struct sigaction fields we set:
 *   sa_handler  = the function to call when the signal arrives
 *   sa_flags    = behavioural flags (see below)
 *   sa_mask     = signals to block while the handler runs (none)
 *
 * SA_RESTART:    if a system call like pause() is interrupted by the signal,
 *                automatically restart it instead of returning EINTR. Keeps
 *                the main loop clean.
 * SA_NOCLDSTOP:  only fire SIGCHLD when a child dies, not when it is stopped
 *                or continued. Filters out spurious signals.
 *
 * sigemptyset() zeroes the sa_mask field properly. memset() already zeroed
 * it but sigemptyset() makes the intent explicit and is the correct API.
 */
static void ksetup_signals(void)
{
	struct sigaction sa;

	/* register SIGCHLD handler — reap zombie children */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = ksigchld_handler;
	sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);

	/* register SIGTERM handler — graceful shutdown */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = ksigterm_handler;
	sa.sa_flags   = SA_RESTART;
	sigaction(SIGTERM, &sa, NULL);
}

/*
 * kmount_vfs — mount the five virtual filesystems.
 *
 * Virtual filesystems are not on disk. The kernel creates them entirely in
 * RAM. We must mount them before any service starts because:
 *
 *   /proc  — exposes kernel data as files. Required by almost every program
 *             for process info, memory info, CPU info.
 *
 *   /sys   — exposes device and driver information. Required for touchscreen
 *             detection (/sys/class/input) and hardware management.
 *
 *   /dev   — device files. /dev/tty1 for display, /dev/input/event* for
 *             touch, /dev/snd for audio. Nothing hardware-related works
 *             without this.
 *
 *   /run   — tmpfs runtime data. Unix socket files live here:
 *             /run/bh-audio.sock, /run/bh-storage.sock etc.
 *             IPC between services cannot happen without this mount.
 *             Cleared on every reboot.
 *
 *   /tmp   — tmpfs temporary files. Standard scratch space expected by
 *             many libraries and programs.
 *
 * mkdir() is called first to ensure the mount point directory exists.
 * errno != EEXIST means we only fail if the error is NOT "already exists" —
 * the directory existing is fine, any other mkdir error is fatal.
 *
 * Mount flags:
 *   MS_NOSUID  — prevent setuid binaries from elevating privileges
 *   MS_NOEXEC  — prevent executing binaries directly from this filesystem
 *   MS_NODEV   — prevent device files on proc/sys (they do not belong there)
 *
 * /tmp uses mode 1777 — the sticky bit means only the owner of a file can
 * delete it, which is the standard for shared tmp directories.
 */
static void kmount_vfs(void)
{
	if (mkdir("/proc", 0755) < 0 && errno != EEXIST)
		exit(1);
	if (mkdir("/sys",  0755) < 0 && errno != EEXIST)
		exit(1);
	if (mkdir("/dev",  0755) < 0 && errno != EEXIST)
		exit(1);
	if (mkdir("/run",  0755) < 0 && errno != EEXIST)
		exit(1);
	if (mkdir("/tmp",  1777) < 0 && errno != EEXIST)
		exit(1);

	if (mount("proc",     "/proc", "proc",     MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0)
		perror("mount /proc"), exit(1);

	if (mount("sysfs",    "/sys",  "sysfs",    MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0)
		perror("mount /sys"), exit(1);

	if (mount("devtmpfs", "/dev",  "devtmpfs", MS_NOSUID, "mode=755") < 0)
		perror("mount /dev"), exit(1);

	if (mount("tmpfs",    "/run",  "tmpfs",    MS_NOSUID | MS_NODEV, "mode=755") < 0)
		perror("mount /run"), exit(1);

	if (mount("tmpfs",    "/tmp",  "tmpfs",    MS_NOSUID | MS_NODEV, "mode=1777") < 0)
		perror("mount /tmp"), exit(1);
}

/*
 * kspawn — fork and exec a binary at the given path.
 *
 * This is the standard Unix pattern for starting a new process:
 *
 *   fork()  — duplicates the current process. After fork() two processes
 *              run identical code. They are distinguished only by the return
 *              value: 0 in the child, child's PID in the parent, -1 on error.
 *
 *   execv() — called in the child only. Replaces the child's code, stack,
 *              and heap entirely with the binary at path. The child ceases
 *              to be a copy of init and becomes the target program.
 *
 * argv[0] is conventionally the program name (the path itself here).
 * The array must be NULL-terminated — execv uses the NULL to know where
 * the argument list ends.
 *
 * _exit() is used (not exit()) if execv fails in the child. exit() flushes
 * stdio buffers which can corrupt the parent's output since both share the
 * same file descriptors immediately after fork.
 *
 * Returns the child PID on success, -1 if fork failed.
 */
static pid_t kspawn(const char *path)
{
	pid_t pid = fork();

	if (pid == -1)
	{
		perror("init: fork failed");
		return -1;
	}
	else if (pid == 0)
	{
		/* child process — replace with the target binary */
		char *argv[] = { (char *)path, NULL };
		execv(path, argv);

		/* execv only returns if it failed */
		perror("init: execv failed");
		_exit(EXIT_FAILURE);
	}
	else
	{
		/* parent process — return child PID to caller */
		return pid;
	}
}

/*
 * kspawn_services — spawn the service manager as init's first child.
 *
 * init does not start services directly. It delegates all service lifecycle
 * management to a dedicated child process — the service manager. This keeps
 * init.c minimal and means a bug in service management cannot crash PID 1.
 *
 * The service manager reads /etc/blackhand/services.conf and starts all
 * BlackHand services in the correct order. If a service crashes, the service
 * manager restarts it — init is not involved.
 *
 * If the service manager itself fails to spawn, that is fatal — the system
 * cannot function without services, so we exit which causes a kernel panic.
 * This is intentional: a device that cannot start its service manager needs
 * human intervention.
 */
static void kspawn_services(void)
{
	const char *svc_mgr_path = "/usr/bin/blackhand-svc-mgr";
	pid_t pid = kspawn(svc_mgr_path);
	if (pid < 0)
	{
		write(2, "init: failed to spawn service manager\n", 38);
		exit(1);
	}
	write(2, "init: service manager spawned\n", 30);
}

/*
 * main — PID 1 entry point.
 *
 * The kernel calls main() directly. There is no shell, no script, no parent
 * process. This is the root of the entire process tree.
 *
 * After setup, main enters an infinite loop calling pause(). pause() sleeps
 * until any signal arrives. When SIGCHLD arrives (a child died), pause()
 * returns, ksigchld_handler runs automatically (reaping the zombie), then
 * pause() is called again. The loop uses essentially zero CPU while idle.
 *
 * The shutdown_requested flag is checked after each pause() returns. When
 * SIGTERM sets it, the loop breaks and the shutdown sequence runs.
 *
 * main() must NEVER return. If PID 1 exits, the kernel panics.
 */
int main(void)
{
	/* safety check — only run as PID 1 */
	if (getpid() != 1)
	{
		write(2, "init: not PID 1, aborting\n", 26);
		return 1;
	}

	ksetup_signals();   /* register SIGCHLD and SIGTERM handlers            */
	kmount_vfs();       /* mount /proc /sys /dev /run /tmp                  */
	kspawn_services();  /* spawn the service manager                        */

	/* sit and wait forever — signal handlers do the work */
	for (;;)
	{
		pause(); /* sleep until a signal arrives */
		if (shutdown_requested)
		{
			write(2, "init: shutdown requested\n", 25);
			/* TODO: stop services, unmount filesystems, call reboot() */
			break;
		}
	}

	return 0; /* never reached */
}
