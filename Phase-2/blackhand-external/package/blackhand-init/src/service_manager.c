/*
 * ============================================================================
 * BlackHand OS — service_manager.c
 * ============================================================================
 *
 * WHAT IS THIS FILE?
 * ------------------
 * The service manager is the first child process spawned by init (PID 1).
 * Its job is to start, monitor, and restart all BlackHand services. It owns
 * the entire service lifecycle — init.c deliberately delegates this work
 * here so that PID 1 stays minimal and a bug here cannot crash the system.
 *
 * This process is started by init as:
 *   /usr/bin/blackhand-svc-mgr
 *
 * WHAT IS A SERVICE?
 * ------------------
 * A service is a long-running background process that does one specific job
 * and never exits on its own. Think of it like a worker hired at boot time
 * who stays at their desk all day waiting for requests:
 *
 *   blackhand-audio   — owns all sound hardware via ALSA. Listens on
 *                        /run/bh-audio.sock for JSON-RPC commands (play,
 *                        pause, volume). Started first.
 *
 *   blackhand-storage — owns the SQLite database. Listens on
 *                        /run/bh-storage.sock for queries about contacts,
 *                        notes, alarms.
 *
 *   blackhand-modem   — owns the radio modem via AT commands. Listens on
 *                        /run/bh-modem.sock for call and SMS commands.
 *
 *   blackhand-ui      — owns the display. Renders the home screen and all
 *                        UI. Connects TO the other three sockets as a client
 *                        to request audio playback, data, and modem actions.
 *                        Started last — depends on the others being ready.
 *
 * WHAT DOES THIS FILE DO?
 * -----------------------
 *   1. PARSE CONFIG
 *      Reads /etc/blackhand/services.conf which lists every service, its
 *      binary path, and its restart policy. Fills the services[] array.
 *      Services are started in the order they appear in the file — audio
 *      first, ui last — so sockets are ready before clients connect.
 *
 *   2. START SERVICES
 *      Forks and execs each service binary in order. Stores each child's
 *      PID in its Service struct so we can identify it later.
 *
 *   3. MONITOR AND RESTART
 *      Registers a SIGCHLD handler. When any service crashes, the kernel
 *      sends SIGCHLD. The handler calls waitpid() to collect the dead PID,
 *      looks it up in the services[] array, and restarts the service if
 *      its restart policy is "always".
 *
 *   4. WAIT FOREVER
 *      After starting all services, enters for(;;){pause()} — identical
 *      to init.c. The signal handler does all ongoing work.
 *
 * RELATIONSHIP TO IPC (blackhand-ipc)
 * ------------------------------------
 * The service manager starts the processes that use IPC but has no direct
 * involvement in IPC itself. IPC is the communication layer between services
 * once they are running. The service manager only cares about process
 * lifecycle — starting, watching, restarting.
 *
 * The socket files that IPC uses (/run/bh-audio.sock etc) are created by
 * each service when it calls bind() on startup. The service manager does
 * not create them — it just ensures the service process that owns each
 * socket is running.
 *
 * RELATIONSHIP TO init.c
 * -----------------------
 * init.c spawns this process as its only direct child. init.c mounts /run
 * which is where socket files live. init.c also reaps this process if it
 * crashes (via its own SIGCHLD handler), though currently it does not
 * restart the service manager — that is a future improvement.
 *
 * The full process tree at runtime:
 *
 *   init (PID 1)
 *     └── service_manager
 *           ├── blackhand-audio
 *           ├── blackhand-storage
 *           ├── blackhand-modem
 *           └── blackhand-ui
 *
 * CONFIG FILE FORMAT
 * ------------------
 * /etc/blackhand/services.conf uses a simple INI-style format:
 *
 *   [service-name]
 *   exec    = /path/to/binary
 *   restart = always
 *
 * Lines starting with # are comments. Blank lines are ignored.
 * Services are started in the order they appear in the file.
 *
 * ============================================================================
 */

#include <stdio.h>    /* fopen, fgets, fclose, perror                       */
#include <stdlib.h>   /* exit, EXIT_FAILURE, setenv                         */
#include <unistd.h>   /* fork, execv, sleep, write, dup2, close             */
#include <fcntl.h>    /* open, O_RDWR                                       */
#include <signal.h>   /* sigaction, SIGCHLD, sig_atomic_t                   */
#include <sys/wait.h> /* waitpid, WNOHANG                                   */
#include <string.h>   /* memset, strncpy, strcspn, strcmp, strncmp, strchr  */
#include <errno.h>    /* errno                                              */
#include <time.h>     /* time, time_t                                       */

/*
 * MAX_SERVICES — maximum number of services the config file can define.
 * A fixed limit avoids dynamic memory allocation. 100 is far more than
 * BlackHand will ever need but uses only ~36KB of BSS memory.
 */
#define MAX_SERVICES 100

/*
 * CONFIG_PATH — fixed location of the service configuration file.
 * Lives in the overlay filesystem, deployed as part of the Buildroot image.
 */
#define CONFIG_PATH "/etc/blackhand/services.conf"

/*
 * Service — describes one service entry from the config file.
 *
 *   name              — human-readable identifier e.g. "blackhand-audio".
 *                       Used for logging so you can see which service died.
 *
 *   path              — absolute path to the service binary e.g.
 *                       "/usr/bin/blackhand-audio". Passed to execv().
 *
 *   restart_on_failure — 1 if the service should be restarted when it dies,
 *                        0 if it should be left dead. Set from "restart=always"
 *                        in the config file.
 *
 *   pid               — the PID of the currently running service process.
 *                        0 means not yet started or currently dead.
 *                        Updated by start_service() when the process spawns.
 *                        Used by find_by_pid() to match a dead PID to its
 *                        service entry when SIGCHLD fires.
 *
 * Fixed-size char arrays (not char pointers) avoid malloc. The service
 * manager runs from boot to shutdown — no dynamic allocation keeps it simple
 * and eliminates any risk of memory leaks.
 */
typedef struct
{
	char name[64];
	char path[256];
	int   restart_on_failure;
	pid_t pid;
	int   restart_count;
	time_t last_start_time;
	time_t next_restart_at;  /* 0 = not pending, >0 = restart after this time */
} Service;

/* Global service table — filled by parse_config(), used by everything else */
Service services[MAX_SERVICES];
int     num_services = 0;

/*
 * parse_config — read services.conf and populate the services[] array.
 *
 * Opens CONFIG_PATH and reads it line by line. Three line types are handled:
 *
 *   [service-name]    — starts a new service entry. Increments current index.
 *                       Initialises the new slot to safe defaults.
 *
 *   exec = /path      — sets the binary path for the current service entry.
 *
 *   restart = always  — sets restart_on_failure = 1 for the current entry.
 *
 * Blank lines and lines starting with '#' are skipped silently.
 *
 * strncpy is used instead of strcpy to prevent buffer overflow if the config
 * file contains an unexpectedly long value.
 *
 * strcspn strips the trailing newline/carriage-return from each line so
 * comparisons and copies work correctly.
 *
 * current starts at -1 so the first [header] increments it to 0 (slot 0).
 * Each subsequent [header] increments it again. At the end, num_services is
 * set to current + 1 which correctly counts all services found.
 *
 * exec and restart lines are only processed when current >= 0 — this guards
 * against malformed config files with key=value lines before any [header].
 */
static void parse_config(void)
{
	FILE *f = fopen(CONFIG_PATH, "r");
	char  line[256];
	int   current = -1;

	if (!f)
	{
		perror("service_manager: could not open " CONFIG_PATH);
		return;
	}

	while (fgets(line, sizeof(line), f))
	{
		/* strip trailing newline / carriage-return */
		line[strcspn(line, "\r\n")] = '\0';

		/* skip blank lines and comments */
		if (line[0] == '\0' || line[0] == '#')
			continue;

		if (line[0] == '[')
		{
			/* new service block — advance to the next array slot */
			current++;
			if (current >= MAX_SERVICES)
			{
				write(2, "service_manager: too many services in config\n", 45);
				break;
			}

			/* initialise slot to safe defaults before filling fields */
			services[current].pid              = 0;
			services[current].restart_on_failure = 0;
			services[current].restart_count    = 0;
			services[current].last_start_time  = 0;
			services[current].next_restart_at  = 0;
			services[current].name[0]          = '\0';
			services[current].path[0]          = '\0';

			/* extract name between '[' and ']' */
			char *end = strchr(line, ']');
			if (end)
			{
				*end = '\0'; /* terminate string at ']' */
				strncpy(services[current].name, line + 1,
				        sizeof(services[current].name) - 1);
			}
		}
		else if (current >= 0 && strncmp(line, "exec", 4) == 0)
		{
			/* extract binary path after '=' sign */
			char *value = strchr(line, '=');
			if (value)
			{
				value++; /* move past '=' */
				while (*value == ' ') value++; /* skip leading spaces */
				strncpy(services[current].path, value,
				        sizeof(services[current].path) - 1);
			}
		}
		else if (current >= 0 && strncmp(line, "restart", 7) == 0)
		{
			/* set restart policy — 1 if "always", 0 otherwise */
			char *value = strchr(line, '=');
			if (value)
			{
				value++;
				while (*value == ' ') value++;
				services[current].restart_on_failure =
					(strcmp(value, "always") == 0) ? 1 : 0;
			}
		}
	}

	num_services = (current >= 0) ? current + 1 : 0;
	fclose(f);
}

/*
 * start_service — fork and exec one service, store its PID.
 *
 * Takes a pointer (not a copy) to a Service struct so we can write the
 * child's PID back into service->pid. If we took the struct by value the
 * write would go into a local copy and the original would never be updated —
 * find_by_pid would never find the service when it dies.
 *
 * fork() creates a child process. In the child we build a minimal argv[]
 * (program name + NULL terminator as required by execv) and replace the
 * child with the service binary. In the parent we store the child's PID.
 *
 * If fork fails we log and return — one failed service should not stop
 * the others from starting. The service manager continues.
 *
 * _exit() is used in the child (not exit()) because exit() flushes stdio
 * buffers. Both parent and child share the same file descriptors immediately
 * after fork — flushing in the child could corrupt the parent's output.
 */
static void start_service(Service *service)
{
	pid_t pid = fork();

	if (pid == -1)
	{
		perror("service_manager: fork failed");
		return; /* log and continue — do not kill the whole service manager */
	}
	else if (pid == 0)
	{
		/* blackhand-ui must render on tty1 (the framebuffer's active VT).
		 * Without this redirect the UI inherits /dev/console = tty3
		 * (set by console=tty3 in cmdline.txt) and renders on a VT that
		 * is never visible on screen. */
		if (strcmp(service->name, "blackhand-ui") == 0)
		{
			int ttyfd = open("/dev/tty1", O_RDWR);
			if (ttyfd >= 0)
			{
				dup2(ttyfd, 0);
				dup2(ttyfd, 1);
				dup2(ttyfd, 2);
				if (ttyfd > 2) close(ttyfd);
			}
			setenv("TERM", "linux", 1);
		}
		else
		{
			/* Every other service has its stderr+stdout redirected to a
			 * per-service log file on the FAT32 boot partition, e.g.
			 * /boot/logs/blackhand-audio.log. That makes every daemon
			 * inspectable from any host that can read the SD card —
			 * no SSH, no ext4 driver. The redirect happens *before*
			 * execv so the new image inherits the redirected fds. */
			char logpath[128];
			snprintf(logpath, sizeof(logpath),
			         "/boot/logs/%s.log", service->name);
			int logfd = open(logpath,
			                 O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (logfd >= 0)
			{
				dup2(logfd, 1);
				dup2(logfd, 2);
				if (logfd > 2) close(logfd);
			}
		}

		/* child process — replace with the service binary */
		char *argv[] = { (char *)service->path, NULL };
		execv(service->path, argv);

		/* execv only returns on failure */
		perror("service_manager: execv failed");
		_exit(EXIT_FAILURE);
	}
	else
	{
		service->pid = pid;
		service->last_start_time = time(NULL);
		service->next_restart_at = 0;
		write(2, "service_manager: started ", 25);
		write(2, service->name, strlen(service->name));
		write(2, "\n", 1);
	}
}

/*
 * find_by_pid — look up a service by its current PID.
 *
 * Called from sigchld_handler after waitpid() returns a dead child's PID.
 * Scans the services[] array for an entry whose pid field matches.
 *
 * Returns a pointer to the live Service struct (not a copy) so the caller
 * can modify it — specifically to restart the service and update its pid.
 *
 * Returns NULL if the PID does not belong to any tracked service. This
 * happens when a temporary process (not in the config) dies and is re-
 * parented to us. The caller must check for NULL before dereferencing.
 */
static Service *find_by_pid(pid_t pid)
{
	for (int i = 0; i < num_services; i++)
	{
		if (services[i].pid == pid)
			return &services[i];
	}
	return NULL;
}

/*
 * sigchld_handler — reap zombie children and restart crashed services.
 *
 * The kernel invokes this automatically when any child process dies.
 * We do not call it directly.
 *
 * Three steps per dead child:
 *   1. waitpid() — acknowledge the death, remove the zombie from the table.
 *                  Returns the dead child's PID.
 *   2. find_by_pid() — identify which service this PID belongs to.
 *                      Returns NULL if it was not a tracked service.
 *   3. start_service() — restart the service if its policy is "always".
 *                         Updates service->pid with the new child's PID.
 *
 * The loop runs until waitpid() returns <= 0 because multiple children
 * can die simultaneously and signals are not queued — one SIGCHLD may
 * represent several deaths.
 *
 * write() is async-signal-safe. printf/fprintf are not — never use them
 * inside a signal handler.
 */
static void sigchld_handler(int sig)
{
	(void)sig;
	while (1)
	{
		int wstatus = 0;
		pid_t pid = waitpid(-1, &wstatus, WNOHANG);
		if (pid <= 0)
			break;

		Service *svc = find_by_pid(pid);
		if (svc == NULL)
			continue;

		svc->pid = 0;

		if (svc->restart_on_failure != 1)
			continue;

		time_t now = time(NULL);

		/* Reset backoff counter if the service ran for at least 10 seconds. */
		if (svc->last_start_time > 0 && (now - svc->last_start_time) >= 10)
			svc->restart_count = 0;

		/* Exponential backoff: 0 s, 1 s, 2 s, 4 s, 8 s, … capped at 30 s. */
		int delay = 0;
		if (svc->restart_count > 0)
		{
			delay = 1 << (svc->restart_count - 1);
			if (delay > 30) delay = 30;
		}
		svc->restart_count++;
		svc->next_restart_at = now + delay;

		write(2, "service_manager: ", 17);
		write(2, svc->name, strlen(svc->name));
		if (delay > 0)
			write(2, " died — scheduling restart with backoff\n", 40);
		else
			write(2, " died — restarting now\n", 23);
	}
}

/*
 * main — service manager entry point.
 *
 * Called by init via fork+exec. Runs as a normal process (not PID 1).
 * If this process crashes, init's SIGCHLD handler reaps it as a zombie.
 * Restarting the service manager from init is a future improvement.
 *
 * Startup sequence:
 *   1. parse_config()     — fill services[] from /etc/blackhand/services.conf
 *   2. validate           — abort if no services were found
 *   3. register SIGCHLD   — before spawning anything so no death is missed
 *   4. start all services — in config file order (audio first, ui last)
 *   5. pause loop         — wait for signals, let sigchld_handler do the rest
 */
int main(void)
{
	/* read the config file and populate the services array */
	parse_config();

	if (num_services == 0)
	{
		write(2, "service_manager: no services found in config, aborting\n", 56);
		exit(EXIT_FAILURE);
	}

	/* register SIGCHLD before spawning — ensures no death is missed */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigchld_handler;
	sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);

	/* start all services in config file order */
	for (int i = 0; i < num_services; i++)
		start_service(&services[i]);

	/* Process deferred restarts once per second. Using sleep(1) instead of
	 * pause() so the backoff timer is checked even when no new signals arrive. */
	for (;;)
	{
		sleep(1);
		time_t now = time(NULL);
		for (int i = 0; i < num_services; i++)
		{
			if (services[i].pid == 0 &&
			    services[i].restart_on_failure &&
			    services[i].next_restart_at > 0 &&
			    now >= services[i].next_restart_at)
			{
				start_service(&services[i]);
			}
		}
	}

	return 0; /* never reached */
}
