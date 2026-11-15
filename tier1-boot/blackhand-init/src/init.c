#include "restart_policy.h"
/* ============================================================================
 * BlackHand OS — init.c  (PID 1)
 * ============================================================================
 *
 * Rewritten to follow BusyBox init patterns:
 *   - setsid() + TIOCSCTTY for proper tty/session management
 *   - Correct signal handler ordering (registered before any spawning)
 *   - waitpid(-1) reaping loop handles multiple simultaneous child deaths
 *   - PID 1 never returns — infinite pause loop with safe shutdown path
 *   - shutdown_requested handled with actual reboot(3) call
 *
 * Boot sequence:
 *   kernel executes /sbin/blackhand-init
 *     → kmount_vfs()           mount proc/sys/dev/run/tmp
 *     → ksetup_console()       bind stdin/stdout/stderr to /dev/console
 *     → ksetup_signals()       register SIGCHLD + SIGTERM + SIGINT handlers
 *     → krun_sysinit()         run /etc/init.d/rcS synchronously (no SIGCHLD race)
 *     → kspawn_services()      service manager takes over
 *     → kspawn_tty1()          bh-session (tmux client) on tty1, respawned
 *     → for(;;) { pause(); }   sleep forever, handlers do the work
 *
 * ============================================================================
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* ============================================================================
 * Global state
 * ============================================================================
 */

/*
 * shutdown_requested — 0, or the signal number that asked for shutdown.
 * Set by the signal handler, consumed by the main loop. The number is kept
 * because it selects reboot vs halt vs power-off (see kdo_shutdown).
 *
 * volatile sig_atomic_t: the only type safe to read/write from both a signal
 * handler and normal code. volatile prevents the compiler from caching it in
 * a register; sig_atomic_t guarantees the CPU cannot be interrupted mid-access.
 */
volatile sig_atomic_t shutdown_requested = 0;

/*
 * ctrlaltdel_requested — set when SIGINT arrives (kernel sends SIGINT to PID 1
 * when Ctrl-Alt-Del is pressed, same as busybox init behaviour).
 */
volatile sig_atomic_t ctrlaltdel_requested = 0;

/*
 * Service-manager supervision.
 *
 * svcmgr_pid   — PID of the running /usr/bin/blackhand-svc-mgr, or -1.
 * svcmgr_died  — set by the SIGCHLD handler when that PID is reaped; the main
 *                loop respawns it. Without this the manager's death silently
 *                ends supervision of every service.
 */
static volatile sig_atomic_t svcmgr_pid = -1;
static volatile sig_atomic_t svcmgr_died = 0;

/*
 * tty1 client — the bh-session process that shows the tmux-hosted UI on the
 * phone's own screen. tty1_pid is ONLY the pid kspawn_tty1() forked; the tmux
 * server and its UI loop are other processes (see ksigchld_handler).
 * tty1_died is set when that pid is reaped; main() respawns it.
 */
static volatile sig_atomic_t tty1_pid = -1;
static volatile sig_atomic_t tty1_died = 0;

/* Crash-loop guard: more than this many manager deaths inside the window and
 * we stop respawning. Respawning forever would peg the CPU with no prospect of
 * recovery; stopping keeps whatever is still running alive and logs loudly. */

#ifdef BLACKHAND_DEBUG_SHELL
/* PID of the bring-up rescue shell (kspawn_shell). Written by main(), read by
 * the SIGCHLD handler, so it must be sig_atomic_t. */
volatile sig_atomic_t debug_shell_pid = -1;
/* Set by the handler when that shell exits so main() respawns it — otherwise
 * typing "exit" once would lock you out of the device until a reboot. */
volatile sig_atomic_t debug_shell_died = 0;
#endif

/*
 * log_fd — open file descriptor to /dev/tty1 for on-screen log messages.
 *
 * The HyperPixel display renders the framebuffer for tty1. /dev/console is
 * wired to tty3 (via console=tty3 in cmdline.txt) for the serial console.
 * Any write to STDERR_FILENO goes to tty3 — invisible on the screen.
 *
 * By opening /dev/tty1 separately we can write startup logs to the display
 * without changing the console binding that the rest of the system expects.
 *
 * -1 = not yet open (before devtmpfs is mounted). Checked in klog().
 */
static int log_fd = -1;

/* ============================================================================
 * Logging
 * ============================================================================
 */

/*
 * klog — write a message to both the serial console (STDERR → tty3) and the
 * display (log_fd → tty1).
 *
 * This is async-signal-safe: it uses only write(), which is on the POSIX
 * async-signal-safe list. Safe to call from signal handlers.
 *
 * strlen() is NOT async-signal-safe, so we compute the length with a plain
 * pointer walk instead.
 */
static void klog(const char *msg)
{
	size_t len = 0;
	const char *p = msg;
	while (*p++)
		len++;

	/* Always write to serial console (tty3) for headless/SSH debugging */
	write(STDERR_FILENO, msg, len);

	/* Also write to tty1 so the message appears on the HyperPixel display.
	 * log_fd is -1 until after devtmpfs is mounted in kmount_vfs(). */
	if (log_fd >= 0)
		write(log_fd, msg, len);
}

/* ============================================================================
 * Signal handlers
 * ============================================================================
 */

/*
 * ksigchld_handler — reap all zombie children.
 *
 * Called automatically by the kernel when any child process dies.
 * We loop with WNOHANG because multiple children may die simultaneously and
 * signals are not queued — one SIGCHLD may represent several deaths.
 *
 * write() is used instead of printf because stdio is NOT async-signal-safe.
 * write() is on the POSIX async-signal-safe list.
 *
 * Mirrors busybox init's mark_terminated() + waitpid loop.
 */
static void ksigchld_handler(int sig)
{
	(void)sig;
	while (1)
	{
		pid_t pid = waitpid(-1, NULL, WNOHANG);
		if (pid <= 0)
			break; /* 0 = no zombie ready, -1 = no children at all */
#ifdef BLACKHAND_DEBUG_SHELL
		/* Only the rescue shell is respawned here; real services are the
		 * service manager's job. */
		if (pid == (pid_t)debug_shell_pid)
			debug_shell_died = 1;
#endif
		/* Everything else that dies here is an orphan re-parented to PID 1
		 * (rcS background jobs, ex-service-manager children) — reaping is
		 * all that is owed to them. The service manager is the one child
		 * init actively supervises. */
		if (pid == (pid_t)svcmgr_pid)
			svcmgr_died = 1;

		/* Exact-pid match only. tmux daemonises: the server (and the UI
		 * loop it runs) is re-parented to init, so it is reaped here as an
		 * "orphan" if it ever dies, but its pid is never tty1_pid. Clearing
		 * tty1_pid also stops a recycled pid from matching later. */
		if (pid == (pid_t)tty1_pid)
		{
			tty1_pid = -1;
			tty1_died = 1;
		}

		klog("init: reaped child\n");
	}
}

/*
 * ksigterm_handler — graceful shutdown on SIGUSR1 (halt), SIGTERM (reboot),
 * SIGUSR2 (poweroff). Mirrors busybox halt_reboot_pwoff().
 *
 * Records WHICH signal arrived rather than acting directly — signal handlers
 * should do as little as possible, and the main loop needs the signal number
 * to pick reboot vs halt vs power-off.
 */
static void ksigterm_handler(int sig)
{
	shutdown_requested = sig;
}

/*
 * ksigint_handler — Ctrl-Alt-Del.
 *
 * The kernel sends SIGINT to PID 1 when it detects Ctrl-Alt-Del on the
 * console. Busybox runs CTRLALTDEL actions here. We request a reboot.
 */
static void ksigint_handler(int sig)
{
	(void)sig;
	ctrlaltdel_requested = 1;
}

/*
 * ksetup_signals — register all signal handlers.
 *
 * Called BEFORE any fork/spawn so the handlers are in place from the moment
 * children can start dying.
 *
 * SA_RESTART:   automatically restart syscalls (like pause()) interrupted by
 *               the signal, instead of returning EINTR. Keeps main loop clean.
 * SA_NOCLDSTOP: only fire SIGCHLD for child death, not for stop/continue.
 *               Prevents spurious wakeups.
 *
 * SIGSTOP and SIGKILL cannot be caught or ignored — the kernel handles them
 * specially for all processes including PID 1.
 */
static void ksetup_signals(void)
{
	struct sigaction sa;

	/* SIGCHLD — reap zombies */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = ksigchld_handler;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);

	/* SIGTERM — reboot */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = ksigterm_handler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGTERM, &sa, NULL);

	/* SIGUSR1 — halt */
	sigaction(SIGUSR1, &sa, NULL);

	/* SIGUSR2 — poweroff */
	sigaction(SIGUSR2, &sa, NULL);

	/* SIGINT — Ctrl-Alt-Del (kernel sends this to PID 1) */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = ksigint_handler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sa, NULL);

	/* SIGHUP — ignore for now (busybox uses it to reload inittab) */
	signal(SIGHUP, SIG_IGN);
}

/* ============================================================================
 * Console and terminal
 * ============================================================================
 */

/*
 * ksetup_console — open /dev/console and bind fd 0/1/2 to it.
 *
 * After kmount_vfs() mounts devtmpfs the kernel populates /dev/console.
 * We open it and dup2 onto stdin/stdout/stderr so all subsequent writes
 * go to the physical console.
 *
 * Mirrors busybox console_init().
 */
static void ksetup_console(void)
{
	int fd = open("/dev/console", O_RDWR | O_NOCTTY);
	if (fd < 0)
	{
		/* devtmpfs may not be mounted yet — fall back to /dev/tty1 */
		fd = open("/dev/tty1", O_RDWR | O_NOCTTY);
	}
	if (fd >= 0)
	{
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
	}
}

/*
 * kset_sane_term — put the terminal into a known-good state.
 *
 * After a fresh boot terminal settings may be undefined. This mirrors
 * busybox's set_sane_term() exactly — same control chars, same flags.
 *
 * Called in the child process after fork() so it only affects the child's
 * terminal, not init itself.
 */
static void kset_sane_term(void)
{
	struct termios tty;

	if (tcgetattr(STDIN_FILENO, &tty) != 0)
		return;

	/* Control characters — standard POSIX defaults */
	tty.c_cc[VINTR] = 3;	/* Ctrl-C  */
	tty.c_cc[VQUIT] = 28;	/* Ctrl-\  */
	tty.c_cc[VERASE] = 127; /* DEL     */
	tty.c_cc[VKILL] = 21;	/* Ctrl-U  */
	tty.c_cc[VEOF] = 4;		/* Ctrl-D  */
	tty.c_cc[VSTART] = 17;	/* Ctrl-Q  */
	tty.c_cc[VSTOP] = 19;	/* Ctrl-S  */
	tty.c_cc[VSUSP] = 26;	/* Ctrl-Z  */

	/* Input: translate CR→NL, enable XON/XOFF */
	tty.c_iflag = ICRNL | IXON | IXOFF;

	/* Output: post-process, NL→CR+NL */
	tty.c_oflag = OPOST | ONLCR;

	/* Local: echo, canonical mode, signals */
	tty.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;

	/* Control: 8-bit, enable receiver, local line, hang-up-on-close */
	tty.c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
	tty.c_cflag |= CS8 | CREAD | HUPCL | CLOCAL;

	tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

/*
 * kopen_tty — open a tty device and redirect stdio to it.
 *
 * Called in the child process (after fork) before exec. Returns 1 on
 * success, 0 on failure. Mirrors busybox open_stdio_to_tty().
 *
 * setsid() makes the child a new session leader — it has no controlling
 * terminal yet. TIOCSCTTY then assigns the opened tty as the controlling
 * terminal for this session, enabling job control and Ctrl-C delivery.
 */
static int kopen_tty(const char *tty_name)
{
	int fd;

	/* Become session leader — detach from parent's controlling terminal */
	setsid();

	close(STDIN_FILENO);
	fd = open(tty_name, O_RDWR);
	if (fd < 0)
	{
		write(STDERR_FILENO, "init: can't open tty\n", 21);
		return 0;
	}

	/* Claim this tty as the controlling terminal for our new session */
	ioctl(fd, TIOCSCTTY, 0);

	if (fd != STDIN_FILENO)
	{
		dup2(fd, STDIN_FILENO);
		close(fd);
	}
	dup2(STDIN_FILENO, STDOUT_FILENO);
	dup2(STDIN_FILENO, STDERR_FILENO);

	kset_sane_term();
	return 1;
}

/* ============================================================================
 * Virtual filesystem mounts
 * ============================================================================
 */

/*
 * kmount_vfs — mount the five virtual filesystems required before any
 * service or program can run.
 *
 * Order matters:
 *   1. /proc and /sys first — many programs expect them unconditionally.
 *   2. /dev  — devtmpfs; the kernel populates device nodes automatically.
 *   3. /run  — tmpfs; Unix socket files for IPC go here.
 *   4. /tmp  — tmpfs; mode 1777 (sticky bit, world-writable).
 *
 * After mounting /dev we call ksetup_console() to rebind fd 0/1/2 to the
 * newly available /dev/console.
 *
 * Remounting / read-write at the end: the kernel mounts rootfs read-only
 * so fsck can run. We must remount rw before rcS writes any files.
 * Non-fatal: some embedded setups intentionally keep rootfs read-only.
 */
static void kmount_vfs(void)
{
	/* Create mount points — ignore EEXIST, fail on anything else */
#define MKDIR(path, mode)                             \
	do                                                \
	{                                                 \
		if (mkdir(path, mode) < 0 && errno != EEXIST) \
		{                                             \
			perror("init: mkdir " path);              \
			exit(1);                                  \
		}                                             \
	} while (0)

	MKDIR("/proc", 0755);
	MKDIR("/sys", 0755);
	MKDIR("/dev", 0755);
	MKDIR("/run", 0755);
	MKDIR("/tmp", 1777);

#undef MKDIR

	/* Mount — MS_NOSUID/MS_NOEXEC/MS_NODEV: standard security hardening */
	if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0)
		perror("mount /proc"), exit(1);

	if (mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0)
		perror("mount /sys"), exit(1);

	/* CONFIG_DEVTMPFS_MOUNT=y: the kernel already mounted devtmpfs at /dev
	 * before init starts, fully populated with device nodes.  Mounting
	 * devtmpfs again creates a NEW empty instance that hides all those nodes
	 * (/dev/gpiomem, /dev/tty1, /dev/console disappear).  Only mount it
	 * ourselves if the kernel did not (i.e. /dev/null does not exist yet). */
	if (access("/dev/null", F_OK) != 0)
	{
		if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID | MS_NOEXEC, "mode=755") < 0)
			perror("mount /dev"), exit(1);
	}

	/* /dev is live — rebind console before any further write() calls */
	ksetup_console();

	/* Open tty1 for on-screen logging. The display shows tty1; /dev/console
	 * is tty3 (console=tty3 in cmdline). O_NOCTTY: don't steal tty1 as our
	 * controlling terminal — kspawn_shell() does that explicitly later. */
	log_fd = open("/dev/tty1", O_WRONLY | O_NOCTTY);
	/* Non-fatal: if tty1 isn't available yet, logs still go to serial tty3. */

	if (mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") < 0)
		perror("mount /run"), exit(1);

	if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") < 0)
		perror("mount /tmp"), exit(1);

	/* Remount rootfs read-write so rcS can write files */
	if (mount(NULL, "/", NULL, MS_REMOUNT, NULL) < 0)
		perror("init: remount / rw (non-fatal)"); /* non-fatal */
}

/*
 * ksethostname — apply /etc/hostname (the custom init is not busybox init, so
 * nothing else does). Non-fatal: a missing file leaves the kernel default.
 */
static void ksethostname(void)
{
	char name[65];
	ssize_t n;
	int fd = open("/etc/hostname", O_RDONLY);

	if (fd < 0)
		return;
	n = read(fd, name, sizeof(name) - 1);
	close(fd);
	if (n <= 0)
		return;
	name[n] = '\0';
	name[strcspn(name, "\r\n")] = '\0';
	if (name[0] != '\0' && sethostname(name, strlen(name)) < 0)
		perror("init: sethostname (non-fatal)");
}

/* ============================================================================
 * Process spawning
 * ============================================================================
 */

/*
 * kchild_signals — put a freshly forked child back to a clean signal state.
 *
 * execv() already resets every CAUGHT handler to SIG_DFL for us (POSIX
 * exec: "signals set to be caught ... shall be set to the default action").
 * The two things that DO survive exec are an *ignored* disposition and the
 * blocked-signal mask, so those are the only two we have to clear:
 *   - SIGHUP is SIG_IGN in init (ksetup_signals) and would stay ignored.
 *   - the mask is normally empty, but clearing it costs nothing and keeps
 *     children sane if init is ever forked from inside a handler.
 */
static void kchild_signals(void)
{
	sigset_t mask;

	signal(SIGHUP, SIG_DFL);
	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);
}

/*
 * kspawn — fork + execv a binary. Returns child PID or -1 on error.
 *
 * The child gets a fresh session (setsid) and its stdio redirected to the
 * console so it has a controlling terminal from birth.
 *
 * _exit() in child (not exit()) avoids flushing shared stdio buffers.
 */
static pid_t kspawn(const char *path)
{
	pid_t pid = fork();

	if (pid < 0)
	{
		perror("init: fork");
		return -1;
	}

	if (pid == 0)
	{
		kchild_signals();

		/* New session + controlling terminal */
		kopen_tty("/dev/console");

		char *argv[] = {(char *)path, NULL};
		execv(path, argv);
		perror("init: execv");
		_exit(EXIT_FAILURE);
	}

	return pid; /* parent returns child PID */
}

/*
 * krun_sysinit — run /etc/init.d/rcS and wait for it to finish.
 *
 * rcS is synchronous: nothing starts until it completes. This is the same
 * guarantee busybox gives for SYSINIT actions — they must finish before
 * WAIT, ONCE, and RESPAWN actions begin.
 *
 * Called BEFORE ksetup_signals() to avoid a race between our manual
 * waitpid(pid) and the SIGCHLD handler's waitpid(-1). Without SIGCHLD
 * registered, the kernel won't fire the handler, so our blocking wait
 * is the only waitpid in flight — no conflict.
 *
 * rcS on BlackHand should:
 *   - Run hyperpixel4-init (DPI pin mux + LCD controller init + backlight)
 *   - modprobe snd_bcm2835
 *   - Start GPM (touchscreen daemon)
 */
static void krun_sysinit(void)
{
	pid_t pid = fork();

	if (pid < 0)
	{
		perror("init: fork rcS");
		return;
	}

	if (pid == 0)
	{
		kchild_signals();
		kopen_tty("/dev/console");

		char *argv[] = {"/bin/sh", "/etc/init.d/rcS", NULL};
		execv("/bin/sh", argv);
		perror("init: exec rcS");
		_exit(1);
	}

	/* Parent: block until rcS finishes */
	waitpid(pid, NULL, 0);
	klog("init: sysinit done\n");
}

/*
 * kspawn_shell — spawn an interactive shell on /dev/console.
 *
 * Mirrors busybox ASKFIRST behaviour: the shell is a normal child of init.
 * When it exits (user logs out), init's SIGCHLD handler reaps it and the main
 * loop respawns it, so typing "exit" cannot lock you out of the device.
 *
 * The leading '-' convention (like busybox uses "-/bin/sh") makes the
 * shell treat itself as a login shell. We achieve this by setting argv[0]
 * to "-sh".
 *
 * SECURITY: this spawns an UNAUTHENTICATED root shell on /dev/tty2, which
 * bypasses the app PIN. It must NEVER ship in a production image. It is
 * compiled out by default and only built when BLACKHAND_DEBUG_SHELL is
 * defined explicitly (BR2_PACKAGE_BLACKHAND_INIT_DEBUG_SHELL=y).
 */
#ifdef BLACKHAND_DEBUG_SHELL
static pid_t kspawn_shell(void)
{
	pid_t pid = fork();

	if (pid < 0)
	{
		perror("init: fork shell");
		return -1;
	}

	if (pid == 0)
	{
		kchild_signals();

		/* New session + controlling tty — open tty1, which is what the
		 * framebuffer displays. /dev/console = tty3 (console=tty3 in cmdline)
		 * so opening console would put the shell on a VT that is invisible. */
		if (!kopen_tty("/dev/tty2"))
			_exit(EXIT_FAILURE);

		/* argv[0] = "-sh" signals a login shell to the shell itself */
		char *argv[] = {"-sh", NULL};
		execv("/bin/sh", argv);
		perror("init: exec shell");
		_exit(EXIT_FAILURE);
	}

	klog("init: shell spawned\n");
	return pid;
}
#endif /* BLACKHAND_DEBUG_SHELL */

/*
 * kspawn_services — launch the BlackHand service manager.
 *
 * The service manager is a normal child of init. It reads
 * /etc/blackhand/services.conf and starts all services in order. init records
 * its PID so the SIGCHLD handler can notice its death (see krespawn_services).
 *
 * Never exits on failure — PID 1 exiting is an immediate kernel panic.
 *
 * SIGCHLD is blocked from before the fork until svcmgr_pid has been stored.
 * A manager that dies immediately (missing binary, bad services.conf, crash
 * on startup) otherwise delivers SIGCHLD while svcmgr_pid still holds -1 or
 * the previous PID: ksigchld_handler() reaps it, the PID does not match,
 * svcmgr_died is never set, and init never respawns — every service is left
 * unsupervised for the rest of the uptime, silently. This is the same race
 * start_service() closes in service_manager.c. kchild_signals() clears the
 * mask in the child, so the manager still starts unblocked.
 */
static void kspawn_services(void)
{
	sigset_t chld, prev;
	pid_t pid;

	sigemptyset(&chld);
	sigaddset(&chld, SIGCHLD);
	sigprocmask(SIG_BLOCK, &chld, &prev);

	pid = kspawn("/usr/bin/blackhand-svc-mgr");
	svcmgr_pid = (pid < 0) ? -1 : pid;

	sigprocmask(SIG_SETMASK, &prev, NULL); /* PID stored — safe to reap */

	klog(pid < 0 ? "init: ERROR -- service manager failed to spawn\n"
	             : "init: service manager spawned\n");
}

/*
 * kspawn_tty1 — start /usr/bin/bh-session (a tmux client) on tty1 so the UI
 * shows on the phone's own screen. The UI itself lives in the detached tmux
 * session rcS created, not in this process: this client may exit (detach, or
 * tmux server gone) and the main loop respawns it. bh-session re-creates the
 * session if it is missing, so a dead tmux server heals here too.
 *
 * kopen_tty() gives the child setsid() + TIOCSCTTY on tty1. The child sleeps
 * 1 s first, so a client that dies at once (no tmux, tty1 unusable) is
 * respawned at most once a second instead of spinning PID 1. The sleep is in
 * the child; PID 1 never blocks.
 *
 * SIGCHLD is blocked until tty1_pid is stored, same race as kspawn_services().
 */
static void kspawn_tty1(void)
{
	sigset_t chld, prev;
	pid_t pid;

	sigemptyset(&chld);
	sigaddset(&chld, SIGCHLD);
	sigprocmask(SIG_BLOCK, &chld, &prev);

	pid = fork();
	if (pid == 0)
	{
		char *argv[] = {"/usr/bin/bh-session", NULL};
		char *envp[] = {"TERM=linux", "LANG=C.UTF-8", "HOME=/root",
		                "PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL};

		kchild_signals(); /* clear ignored SIGHUP + blocked SIGCHLD */
		sleep(1);
		if (!kopen_tty("/dev/tty1"))
			_exit(EXIT_FAILURE);
		execve(argv[0], argv, envp);
		perror("init: exec bh-session");
		_exit(EXIT_FAILURE);
	}
	tty1_pid = (pid < 0) ? -1 : pid;

	sigprocmask(SIG_SETMASK, &prev, NULL); /* PID stored — safe to reap */

	if (pid < 0)
		perror("init: fork bh-session");
	else
		klog("init: tty1 client spawned\n");
}

/*
 * kmono_sec — seconds from a monotonic source, for measuring elapsed time.
 *
 * This device has no RTC: the wall clock starts near the epoch every boot and
 * leaps forward whenever it is later corrected. time() is therefore unusable
 * for durations — a forward jump past SVCMGR_WINDOW_SEC silently resets the
 * crash-loop window (the guard never trips, so a crash-looping manager spins
 * forever), and a backward jump makes `now - window_start` negative so the
 * window never resets at all and the guard trips early, leaving the system
 * permanently unsupervised. CLOCK_MONOTONIC has neither failure mode.
 */
static time_t kmono_sec(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0; /* cannot happen on Linux; 0 just widens the window */
	return ts.tv_sec;
}

/*
 * ksvcmgr_giveup — the crash-loop guard. Returns 1 once the manager has died
 * more than SVCMGR_MAX_DEATHS times inside SVCMGR_WINDOW_SEC seconds.
 * `now` is a parameter (not read inline) so selftest.c can drive it.
 */
static int ksvcmgr_giveup(time_t now)
{
    static RestartGuard guard;
    return restart_guard_exhausted(&guard, now);
}

/*
 * krespawn_services — restart the service manager after it died.
 *
 * Called from the main loop (never from the handler) when svcmgr_died is set.
 * Without this, one crash of the manager silently ends supervision of every
 * service for the rest of the uptime.
 *
 * Killing the old process group first is not optional: when the manager dies
 * its services keep running and are re-parented to init. No service calls
 * setsid(), so they all sit in the manager's process group (kspawn() made the
 * manager a group leader via setsid) and one kill(-pgid) reaches all of them.
 * Skip it and the new manager starts a SECOND copy of every service, which
 * then fights the first for its Unix socket and the audio device.
 *
 * Crash-loop guard: after SVCMGR_MAX_DEATHS deaths inside SVCMGR_WINDOW_SEC
 * we stop. Respawning forever would burn the CPU with no prospect of recovery.
 */

static void krespawn_services(void)
{
	int giveup = ksvcmgr_giveup(kmono_sec());

	if (svcmgr_pid > 0)
		kill(-(pid_t)svcmgr_pid, SIGKILL); /* orphaned services */
	svcmgr_pid = -1;

	if (giveup)
	{
		klog("init: FATAL -- service manager crash-looping, giving up\n");
		klog("init: no services are supervised; reboot the device\n");
		return;
	}

	klog("init: service manager died -- respawning\n");
	kspawn_services();
}

/* ============================================================================
 * Shutdown
 * ============================================================================
 */

/*
 * kunmount_all — flush and detach writable filesystems. Bounded: shutdown
 * must never hang, so every step has a hard limit.
 *
 * /boot is FAT32. Runtime logs use /var/log, but any boot/config changes
 * must still be flushed before poweroff because FAT has no journal.
 *
 * The ext4 root is REMOUNTED READ-ONLY instead of unmounted: PID 1 itself runs
 * from it, so unmounting it is impossible. Read-only is sufficient — it
 * commits the journal and blocks any further write. See DECISION-005.
 *
 * /proc, /sys, /dev, /run and /tmp are virtual — nothing to flush.
 */
#ifndef MNT_DETACH
#define MNT_DETACH 2 /* linux/fs.h; not exported by every libc's sys/mount.h */
#endif
static void kunmount_all(void)
{
	int i;

	sync();

	for (i = 0; i < 3; i++)
	{
		/* EINVAL/ENOENT = /boot was never mounted; nothing to do. */
		if (umount("/boot") == 0 || errno == EINVAL || errno == ENOENT)
			break;
		sleep(1);
	}
	if (i == 3)
	{
		/* Still busy after 3 s. MNT_DETACH removes it from the tree now and
		 * releases it when the last user closes — better than blocking the
		 * shutdown forever, and the sync() below still flushes the data. */
		klog("init: /boot busy -- lazy unmount\n");
		umount2("/boot", MNT_DETACH);
	}

	/* Root now carries several permanent writers (rcS's `dmesg -w` stream,
	 * a `tail -F | grep` errors filter, a 60 s log-cap rewriter, and until
	 * removed a 60 s chmod sweep), so MS_REMOUNT|MS_RDONLY can hit EBUSY
	 * exactly like /boot's umount above -- any of them still holding a file
	 * open for write blocks it. kill(-1, SIGKILL) earlier in kdo_shutdown()
	 * returns before those processes are actually reaped, so the first
	 * attempt can easily lose that race. Retry with the same bounded
	 * 3-attempt, 1 s-gap shape as /boot. There is no MNT_DETACH equivalent
	 * for the fs PID 1 itself runs from -- it cannot be unmounted, only
	 * left dirty -- so a still-failing final attempt is only logged. */
	for (i = 0; i < 3; i++)
	{
		if (mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL) == 0)
			break;
		sleep(1);
	}
	if (i == 3)
		klog("init: WARNING -- could not remount / read-only after 3 attempts\n");

	sync();
}

/*
 * ksave_clock — best-effort save of the wall clock to /data/.clock before
 * shutdown, so rcS can restore it at the next boot (this device has no RTC —
 * see kmono_sec()). Contract is fixed by the rcS restore side: a single
 * decimal seconds-since-epoch value plus a trailing newline.
 *
 * Must run BEFORE kunmount_all() remounts / read-only -- /data lives on root.
 *
 * rcS already saves this periodically; the normal shutdown path on this
 * device is pulling power, not this clean-shutdown path, so this is a small
 * improvement layered on that, not the primary mechanism. Best effort only:
 * never retries, never blocks shutdown, just logs on failure.
 *
 * Atomic: write to a temp file in the same directory, then rename() over the
 * real path. rename() within one filesystem is atomic, so a reader (rcS at
 * next boot) can never observe a truncated value.
 */
static void ksave_clock(void)
{
	char buf[32];
	int len, fd;

	len = snprintf(buf, sizeof(buf), "%ld\n", (long)time(NULL));
	if (len <= 0 || (size_t)len >= sizeof(buf))
		return; /* cannot happen: a time_t always fits in 32 bytes */

	fd = open("/data/.clock.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		klog("init: WARNING -- could not save clock to /data/.clock\n");
		return;
	}

	if (write(fd, buf, (size_t)len) != len)
		klog("init: WARNING -- short write saving clock\n");

	close(fd);

	if (rename("/data/.clock.tmp", "/data/.clock") < 0)
		klog("init: WARNING -- could not rename /data/.clock.tmp\n");
}

/*
 * SHUTDOWN_GRACE_SEC — how long kdo_shutdown() waits after SIGTERM before
 * SIGKILL.
 *
 * MUST stay >= IPC_TIMEOUT_SEC from blackhand-ipc/src/ipc.h (currently 20s).
 * Reasoning: a client (typically blackhand-ui) can be blocked inside a
 * synchronous send_msg()/recv_msg() call to a service when SIGTERM arrives.
 * recv_all()/send_all() in ipc.c retry on EINTR, so the signal does not
 * unblock the call early -- the client only regains control when the peer
 * answers or the socket's SO_RCVTIMEO/SO_SNDTIMEO (set to IPC_TIMEOUT_SEC)
 * fires. Only after that can the client's own SIGTERM handling run its
 * flush-notes/flush-drafts/finalise-recording cleanup. A grace shorter than
 * IPC_TIMEOUT_SEC (the old value was 5s, 15s short) gets that client
 * SIGKILLed before it ever gets a chance to clean up.
 *
 * Set to IPC_TIMEOUT_SEC + 2s: the extra 2s is for the client's own cleanup
 * work after its IPC call unblocks, plus scheduling slop -- it is not itself
 * bounded by anything else in this file, so it is a fixed margin rather than
 * a derived one. This is a hand-kept invariant, not enforced by the compiler:
 * ipc.h and init.c are separate Buildroot packages (blackhand-init does not
 * depend on blackhand-ipc's headers), so if IPC_TIMEOUT_SEC ever changes,
 * SHUTDOWN_GRACE_SEC must be reviewed and updated to match by hand. See
 * BUG-shutdown-grace in shared knowledge.
 */
#define SHUTDOWN_GRACE_SEC 22

/*
 * kdo_shutdown — kill all processes, save the clock, unmount cleanly, and
 * reboot/halt/poweroff.
 *
 * Mirrors busybox run_shutdown_and_kill_processes() + pause_and_low_level_reboot().
 *
 *   1. SIGTERM      — ask every process to exit cleanly.
 *   2. sleep(22)    — give them time to respond (UI drafts/notes flush,
 *                     daemon WAV finalisation) -- see SHUTDOWN_GRACE_SEC.
 *   3. SIGKILL      — kill whatever is still alive (frees /boot).
 *   4. ksave_clock  — best-effort, before root goes read-only.
 *   5. kunmount_all — sync, unmount /boot, remount / read-only (retried).
 *   6. reboot(2)    — hand over to the kernel.
 *
 * Worst-case duration: 22 s (SIGTERM grace, SHUTDOWN_GRACE_SEC) + up to 3 s
 * (/boot unmount retries) + up to 3 s (/ remount-ro retries) = ~28 s. Bounded
 * at every step; this never hangs, it just occasionally reboots slower.
 *
 * kill(-1, sig) sends the signal to every process except PID 1 itself. The
 * service manager has the default SIGTERM disposition, so step 1 stops it
 * before it can restart anything it sees dying.
 *
 * `sig` is the signal that requested the shutdown, using the busybox
 * convention: SIGUSR1 = halt, SIGUSR2 = power off, anything else = reboot.
 */
static void kdo_shutdown(int sig)
{
	int how = RB_AUTOBOOT; /* SIGTERM (reboot) and SIGINT (Ctrl-Alt-Del) */

	if (sig == SIGUSR1)
		how = RB_HALT_SYSTEM;
	else if (sig == SIGUSR2)
		how = RB_POWER_OFF;

	klog("init: shutdown -- terminating all processes\n");

	/* Let the kernel auto-reap from here on. Nothing is being supervised any
	 * more, and a handler firing for each dying process would cut short the
	 * sleeps in kunmount_all() (sleep(3) is not restarted by SA_RESTART),
	 * which is exactly the time /boot needs to become free.
	 *
	 * The shutdown signals need the same treatment for the same reason: a
	 * second `poweroff` over SSH, or Ctrl-Alt-Del, delivers SIGTERM/SIGUSR1/
	 * SIGUSR2/SIGINT to PID 1 while we are in one of those sleeps and cuts it
	 * short too — SA_RESTART does not cover sleep(). Shutdown is already
	 * running and cannot be started twice, so ignoring them is also correct. */
	signal(SIGCHLD, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	signal(SIGINT, SIG_IGN);

	kill(-1, SIGTERM);
	sync();
	/* Let UI drafts and daemon WAV finalisation finish before forced exit.
	 * See SHUTDOWN_GRACE_SEC above for why this must be >= IPC_TIMEOUT_SEC. */
	sleep(SHUTDOWN_GRACE_SEC);

	kill(-1, SIGKILL);

	ksave_clock(); /* before root goes read-only below */

	kunmount_all();

	klog(how == RB_AUTOBOOT ? "init: rebooting\n" : "init: powering down\n");

	/* Fork before calling reboot() — the kernel calls do_exit() inside
	 * reboot() which can panic if called directly by PID 1 on some kernels.
	 * Busybox init does this too (pause_and_low_level_reboot). */
	pid_t pid = fork();
	if (pid == 0)
	{
		reboot(how);
		_exit(0);
	}

	/* Parent hangs — we must never return from main() */
	for (;;)
		sleep(1);
}

/* ============================================================================
 * main
 * ============================================================================
 */

/*
 * main — PID 1 entry point. The kernel calls this directly.
 *
 * Order of operations mirrors busybox init_main():
 *
 *   1. kmount_vfs()      — virtual filesystems (prereq for everything)
 *   2. ksetup_console()  — already called inside kmount_vfs() after /dev mounts
 *   3. krun_sysinit()    — blocking rcS run (before SIGCHLD registered)
 *   4. ksetup_signals()  — register handlers (after blocking sysinit, before spawning)
 *   5. kspawn_services() — service manager
 *   6. for(;;) pause()   — sleep, wake on signal, check flags, repeat
 *
 * No interactive shell is spawned. kspawn_shell() exists only for local
 * bring-up and is compiled out unless BLACKHAND_DEBUG_SHELL is defined.
 *
 * main() must NEVER return. If PID 1 exits, the kernel panics immediately.
 */
int main(void)
{
	/* Safety check — only run as PID 1 */
	if (getpid() != 1)
	{
		write(STDERR_FILENO, "init: must be run as PID 1\n", 27);
		return 1;
	}

	/* 1. Mount virtual filesystems.
	 *    ksetup_console() is called inside to bind fd 0/1/2 to /dev/console.
	 *    log_fd is opened to /dev/tty1 here so klog() writes to the display. */
	kmount_vfs();

	/* Banner — printed to both serial console (tty3) and display (tty1) */
	klog("\n");
	klog("  ██████╗ ██╗      █████╗  ██████╗██╗  ██╗    ██╗  ██╗ █████╗ ███╗  ██╗██████╗\n");
	klog("  ██╔══██╗██║     ██╔══██╗██╔════╝██║ ██╔╝    ██║  ██║██╔══██╗████╗ ██║██╔══██╗\n");
	klog("  ██████╔╝██║     ███████║██║     █████╔╝     ███████║███████║██╔██╗██║██║  ██║\n");
	klog("  ██╔══██╗██║     ██╔══██║██║     ██╔═██╗     ██╔══██║██╔══██║██║╚████║██║  ██║\n");
	klog("  ██████╔╝███████╗██║  ██║╚██████╗██║  ██╗    ██║  ██║██║  ██║██║  ███║██████╔╝\n");
	klog("  ╚═════╝ ╚══════╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝    ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝   ╚═╝╚═════╝\n");
	klog("\n");
	klog("  BlackHand OS — booting...\n");
	klog("\n");

	/* 2. Set up environment */
	klog("init: setting environment\n");
	putenv("HOME=/");
	putenv("PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
	putenv("SHELL=/bin/sh");
	putenv("TERM=linux");
	putenv("USER=root");

	/* 3. Run sysinit script synchronously — BEFORE SIGCHLD handler is live
	 *    to avoid race between our waitpid(pid) and handler's waitpid(-1). */
	ksethostname();

	klog("init: running /etc/init.d/rcS...\n");
	krun_sysinit();

	/* 4. Register signal handlers — after blocking sysinit, before any spawning */
	klog("init: registering signal handlers\n");
	ksetup_signals();

	/* 5. Service manager */
	klog("init: spawning service manager\n");
	kspawn_services();

	/* 5b. tty1 client for the tmux session rcS already created (rcS is the
	 *     single creation point; bh-session would create it only if absent). */
	kspawn_tty1();

#ifdef BLACKHAND_DEBUG_SHELL
	/* 6. Bring-up rescue shell on tty2 — reach it with Alt+F2 (UI owns tty1).
	 *    Compiled out unless BR2_PACKAGE_BLACKHAND_INIT_DEBUG_SHELL=y. */
	klog("init: spawning debug shell on tty2 (Alt+F2)\n");
	debug_shell_pid = kspawn_shell();
#endif

	klog("init: boot complete — system ready\n");
	klog("\n");

	/* 7. Main loop — sleep until a signal arrives, check flags, repeat.
	 *
	 * pause() sleeps until any signal is delivered. SA_RESTART means the
	 * kernel automatically restarts pause() after the signal handler returns,
	 * UNLESS we set a flag the handler wants to act on.
	 *
	 * The flag check order follows busybox check_delayed_sigs().
	 */
	for (;;)
	{
		pause();

		if (ctrlaltdel_requested)
		{
			ctrlaltdel_requested = 0;
			klog("init: Ctrl-Alt-Del -- rebooting\n");
			kdo_shutdown(SIGINT); /* does not return */
		}

		if (shutdown_requested)
		{
			int sig = shutdown_requested;
			shutdown_requested = 0;
			kdo_shutdown(sig); /* does not return */
		}

		if (svcmgr_died)
		{
			svcmgr_died = 0;
			krespawn_services();
		}

		if (tty1_died)
		{
			tty1_died = 0;
			kspawn_tty1();
		}

#ifdef BLACKHAND_DEBUG_SHELL
		if (debug_shell_died)
		{
			debug_shell_died = 0;
			klog("init: debug shell exited — respawning\n");
			debug_shell_pid = kspawn_shell();
		}
#endif
	}

	return 0; /* unreachable — satisfies compiler */
}