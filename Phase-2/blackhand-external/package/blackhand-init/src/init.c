/*
 * Black Hand init (PID 1).
 * Responsibilities:
 * - mount /proc, /sys, /dev, /run, /tmp
 * - set up signal handlers (SIGCHLD) and reap zombies
 * - spawn the service manager as a child process
 * - stay alive as PID 1 (never exit)
 *
 * Example skeleton:
 * int main(void) {
 *     if (getpid() != 1) return 1;
 *     setup_signals();
 *     mount_vfs();
 *     spawn_service("/usr/bin/blackhand-svc-mgr");
 *     for (;;) { pause(); }
 * }
 */

#include <stdio.h>	   // fprintf, perror,c
#include <stdlib.h>	   // exit,c
#include <unistd.h>	   // fork, execv, pause, getpid,c
#include <signal.h>	   // sigaction, SIGCHLD, SIGTERM,c
#include <sys/wait.h>  // waitpid, WNOHANG,c
#include <sys/mount.h> // mount(),c
#include <sys/stat.h>  // mkdir,c
#include <string.h>	   // memset,c
#include <errno.h>	   // errno, EEXIST,c

// ── private functions ────────────────────────────────────
volatile sig_atomic_t shutdown_requested = 0; // flag for graceful shutdown
// reap all zombie children — called from SIGCHLD handler
static void ksigchld_handler(int sig)
{
	(void)sig;
	while (1)
	{
		// this is the some intresting shit. this function is invoked by kernal when there is a dead child telling it hey,
		// what do you think is it dead and this function confirms and says yeh get rid of it.
		// waitpid returns the zombies pid and when there is nothing it returns zero.
		pid_t pid = waitpid(-1, NULL, WNOHANG);
		if (pid <= 0)
			break; // no more zombies
		write(2, "reaped child\n", 13);
	}
}

// catch SIGTERM for graceful shutdown
static void ksigterm_handler(int ksig)
{ // only react to SIGTERM (optional check)
	// set a global shutdown flag
	if (ksig == SIGTERM)
	{
		shutdown_requested = 1;
	}
	// is this correct?
}

// register both signal handlers with sigaction()
// this is the function that tells the kernal who to call for zombie handler
// sigchld is like telling the handler hey this is the child do you want to ...
static void ksetup_signals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = ksigterm_handler;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);
}

// mount /proc /sys /dev /run /tmp
// call mkdir() first to ensure mount points exist
// check return value of each mount()
static void kmount_vfs(void)
{
	if (mkdir("/proc", 0755) < 0 && errno != EEXIST)
		exit(1);
	if (mkdir("/sys", 0755) < 0 && errno != EEXIST)
		exit(1);
	if (mkdir("/dev", 0755) < 0 && errno != EEXIST)
		exit(1);
	if (mkdir("/run", 0755) < 0 && errno != EEXIST)
		exit(1);
	if (mkdir("/tmp", 1777) < 0 && errno != EEXIST)
		exit(1);

	if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0)
		perror("mount /proc"), exit(1);

	if (mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0)
		perror("mount /sys"), exit(1);

	if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=755") < 0)
		perror("mount /dev"), exit(1);

	if (mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") < 0)
		perror("mount /run"), exit(1);

	if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") < 0)
		perror("mount /tmp"), exit(1);
}
// fork + exec a service binary at path
// return child PID on success, -1 on failure
static pid_t kspawn(const char *path)
{
	pid_t pid = fork();
	if (pid == -1)
	{
		perror("cant fork");
		_exit(EXIT_FAILURE);
	}
	else if (pid == 0)
	{
		char *argv[] = {(char *)path, NULL};
		execv(path, argv);
		perror("cant exec");
		_exit(EXIT_FAILURE);
	}
	else
	{
		return pid;
	}
}

// start the service manager — first thing spawned
static void kspawn_services(void)
{
	const char *svc_mgr_path = "/usr/bin/blackhand-svc-mgr";
	pid_t pid = kspawn(svc_mgr_path);
	if (pid < 0)
	{
		fprintf(stderr, "Failed to spawn service manager: %s\n", strerror(errno));
		exit(1);
	}
}

// ── main ─────────────────────────────────────────────────

// 1. check getpid() == 1
// 2. setup_signals()
// 3. mount_vfs()
// 4. spawn_services()
// 5. for(;;) { pause(); }
int main(void)
{
	if (getpid() != 1)
	{
		write(2, "not PID 1, aborting\n", 20);
		return 1;
	}
	ksetup_signals();
	kmount_vfs();
	kspawn_services();

	for (;;)
	{
		pause();
		if (shutdown_requested)
		{
			// clean shutdown goes here later
			break;
		}
	}
}
