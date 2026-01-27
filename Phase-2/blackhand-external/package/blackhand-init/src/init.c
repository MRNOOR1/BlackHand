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
