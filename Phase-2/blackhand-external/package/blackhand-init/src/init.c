/* ============================================================================
 * BlackHand OS — init.c  (PID 1)
 * ============================================================================
 *
 * Rewritten to follow BusyBox init patterns:
 *   - setsid() + TIOCSCTTY for proper tty/session management
 *   - Correct signal handler ordering (registered before any spawning)
 *   - waitpid(-1) reaping loop handles multiple simultaneous child deaths
 *   - kspawn_shell() waits for the shell (like ASKFIRST) or runs as respawn
 *   - PID 1 never returns — infinite pause loop with safe shutdown path
 *   - shutdown_requested handled with actual reboot(3) call
 *
 * Boot sequence:
 *   kernel executes /sbin/blackhand-init
 *     → kmount_vfs()           mount proc/sys/dev/run/tmp
 *     → ksetup_console()       bind stdin/stdout/stderr to /dev/console
 *     → ksetup_signals()       register SIGCHLD + SIGTERM + SIGINT handlers
 *     → krun_sysinit()         run /etc/init.d/rcS synchronously (no SIGCHLD race)
 *     → kspawn_shell()         interactive shell on console (like busybox ASKFIRST)
 *     → kspawn_services()      service manager takes over
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

/* ============================================================================
 * Global state
 * ============================================================================
 */

/*
 * shutdown_requested — set by signal handler, checked in main loop.
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
		/* In a fuller implementation you would walk an action list here
		 * (like busybox's mark_terminated) and schedule respawns.
		 * For now we just reap and log. */
		write(STDERR_FILENO, "init: reaped child\n", 19);
	}
}

/*
 * ksigterm_handler — graceful shutdown on SIGUSR1 (halt), SIGTERM (reboot),
 * SIGUSR2 (poweroff). Mirrors busybox halt_reboot_pwoff().
 *
 * Sets a flag rather than acting directly — signal handlers should do as
 * little as possible. The main loop calls kdo_shutdown() when it sees the flag.
 */
static void ksigterm_handler(int sig)
{
	(void)sig;
	shutdown_requested = 1;
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

	if (mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") < 0)
		perror("mount /run"), exit(1);

	if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") < 0)
		perror("mount /tmp"), exit(1);

	/* Remount rootfs read-write so rcS can write files */
	if (mount(NULL, "/", NULL, MS_REMOUNT, NULL) < 0)
		perror("init: remount / rw (non-fatal)"); /* non-fatal */
}

/* ============================================================================
 * Process spawning
 * ============================================================================
 */

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
		/* Child: reset all signal handlers to defaults, unblock signals.
		 * Mirrors busybox reset_sighandlers_and_unblock_sigs(). */
		signal(SIGCHLD, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGUSR1, SIG_DFL);
		signal(SIGUSR2, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGHUP, SIG_DFL);
		sigset_t mask;
		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

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
		/* Child: reset signals and get a tty */
		signal(SIGCHLD, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		sigset_t mask;
		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

		kopen_tty("/dev/console");

		char *argv[] = {"/bin/sh", "/etc/init.d/rcS", NULL};
		execv("/bin/sh", argv);
		perror("init: exec rcS");
		_exit(1);
	}

	/* Parent: block until rcS finishes */
	waitpid(pid, NULL, 0);
	write(STDERR_FILENO, "init: sysinit done\n", 19);
}

/*
 * kspawn_shell — spawn an interactive shell on /dev/console.
 *
 * Mirrors busybox ASKFIRST behaviour: the shell is a normal child of init.
 * When it exits (user logs out), init's SIGCHLD handler reaps it.
 *
 * For a respawn-on-exit loop you would track the shell PID and re-spawn
 * in the main loop, exactly as busybox does for RESPAWN actions. That
 * extension is left as a TODO — the current behaviour is "spawn once".
 *
 * The leading '-' convention (like busybox uses "-/bin/sh") makes the
 * shell treat itself as a login shell. We achieve this by setting argv[0]
 * to "-sh".
 */
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
		/* Child: reset all signal handlers */
		signal(SIGCHLD, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGUSR1, SIG_DFL);
		signal(SIGUSR2, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGHUP, SIG_DFL);
		sigset_t mask;
		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

		/* New session + controlling tty — open tty1, which is what the
		 * framebuffer displays. /dev/console = tty3 (console=tty3 in cmdline)
		 * so opening console would put the shell on a VT that is invisible. */
		if (!kopen_tty("/dev/tty1"))
			_exit(EXIT_FAILURE);

		/* argv[0] = "-sh" signals a login shell to the shell itself */
		char *argv[] = {"-sh", NULL};
		execv("/bin/sh", argv);
		perror("init: exec shell");
		_exit(EXIT_FAILURE);
	}

	write(STDERR_FILENO, "init: shell spawned\n", 20);
	return pid;
}

/*
 * kspawn_services — launch the BlackHand service manager.
 *
 * The service manager is a normal child of init. It reads
 * /etc/blackhand/services.conf and starts all services in order.
 * If it crashes, init reaps it via SIGCHLD. Restarting it is a TODO
 * (track its PID, respawn in main loop — same busybox RESPAWN pattern).
 *
 * If fork itself fails we cannot start services, so we hang in pause()
 * rather than exiting (which would kernel panic).
 */
static void kspawn_services(void)
{
	const char *path = "/usr/bin/blackhand-svc-mgr";
	pid_t pid = kspawn(path);
	if (pid < 0)
	{
		write(STDERR_FILENO, "init: FATAL — service manager failed to spawn\n", 46);
		/* Don't exit — PID 1 exiting = kernel panic */
		for (;;)
			pause();
	}
	write(STDERR_FILENO, "init: service manager spawned\n", 30);
}

/* ============================================================================
 * Shutdown
 * ============================================================================
 */

/*
 * kdo_shutdown — kill all processes and reboot/halt.
 *
 * Mirrors busybox run_shutdown_and_kill_processes() + pause_and_low_level_reboot().
 *
 * Step 1: SIGTERM — ask processes to exit cleanly.
 * Step 2: sync()  — flush all pending disk writes.
 * Step 3: sleep(1) — give processes time to respond.
 * Step 4: SIGKILL — kill anything still alive.
 * Step 5: sync() again — flush any writes from dying processes.
 * Step 6: reboot(2) — ask the kernel to halt/reboot/poweroff.
 *
 * kill(-1, sig) sends the signal to every process except PID 1 itself.
 *
 * We use RB_AUTOBOOT (reboot) as the default. A production implementation
 * would distinguish halt/reboot/poweroff based on which signal arrived,
 * exactly as busybox halt_reboot_pwoff() does with RB_HALT_SYSTEM /
 * RB_AUTOBOOT / RB_POWER_OFF.
 */
static void kdo_shutdown(void)
{
	write(STDERR_FILENO, "init: shutdown — terminating all processes\n", 43);

	kill(-1, SIGTERM);
	sync();
	sleep(1);

	kill(-1, SIGKILL);
	sync();
	sleep(1);

	write(STDERR_FILENO, "init: rebooting\n", 16);

	/* Fork before calling reboot() — the kernel calls do_exit() inside
	 * reboot() which can panic if called directly by PID 1 on some kernels.
	 * Busybox init does this too (pause_and_low_level_reboot). */
	pid_t pid = fork();
	if (pid == 0)
	{
		reboot(RB_AUTOBOOT);
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
 *   5. kspawn_shell()    — interactive console shell
 *   6. kspawn_services() — service manager
 *   7. for(;;) pause()   — sleep, wake on signal, check flags, repeat
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

	/* 1. Mount virtual filesystems (also calls ksetup_console inside) */
	kmount_vfs();

	write(STDERR_FILENO, "init: BlackHand OS starting\n", 28);

	/* 2. Set up environment */
	putenv("HOME=/");
	putenv("PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
	putenv("SHELL=/bin/sh");
	putenv("TERM=linux");
	putenv("USER=root");

	/* 3. Run sysinit script synchronously — BEFORE SIGCHLD handler is live
	 *    to avoid race between our waitpid(pid) and handler's waitpid(-1). */
	krun_sysinit();

	/* 4. Register signal handlers — after blocking sysinit, before any spawning */
	ksetup_signals();

	/* 5. Interactive shell on console */
	kspawn_shell();

	/* 6. Service manager */
	kspawn_services();

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
			write(STDERR_FILENO, "init: Ctrl-Alt-Del — rebooting\n", 31);
			kdo_shutdown(); /* does not return */
		}

		if (shutdown_requested)
		{
			shutdown_requested = 0;
			kdo_shutdown(); /* does not return */
		}
	}

	return 0; /* unreachable — satisfies compiler */
}