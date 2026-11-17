# procmon – Process Management & Command Execution (POSIX / Linux)

A small C program that shows how an operating system **creates, executes,
supervises and terminates processes** using `fork()`, `execvp()` and `waitpid()`.

## Folder Structure
## Project Structure

```text
process-management-linux/
├── src/
│   └── procmon.c
├── tests/
│   ├── run_tests.sh
├── screenshots/
├── logs/
│   ├── process_log.txt
│   └── full_test_output.txt
├── docs/
│   ├── report.pdf
├── README.md
├── Makefile
└── .gitignore
```

### Directory Description

- **src/** – Contains the main C source code.
- **tests/** – Contains automated test scripts.
- **screenshots/** – Contains experimental screenshots and terminal outputs.
- **logs/** – Stores process execution logs and test execution records.
- **docs/** – Contains the technical report.
- **README.md** – Provides project overview, dependencies, build instructions, usage, and limitations.
- **Makefile** – Simplifies compilation and build operations.
- **.gitignore** – Specifies files excluded from Git version control.


## Features
1. Creates a child with `fork()`.
2. Child runs an external program with `execvp()`.
3. Parent waits with `waitpid()` and decodes the exit status.
4. Exec failure is detected (close-on-exec pipe) and reported with `strerror()`.
5. Prints PID / PPID of parent and child.
6. Timestamped (millisecond) lifecycle log, written to screen and a log file.
7. Distinguishes **normal exit** (`WIFEXITED`) from **signal termination** (`WIFSIGNALED`).
8. **Challenge:** timeout – after N seconds sends `SIGTERM` to the child's process
   group, and `SIGKILL` after a 2 s grace period.

## Requirements
Linux or macOS, `gcc`, `make`, `bash`. (`pstree` optional: `sudo apt install psmisc`.)
On Windows use WSL.

## Build
```bash
make
```

## Usage
```bash
./procmon [-t timeout_sec] [-l logfile] <command> [args...]
```
Examples:
```bash
./procmon ls -l /tmp               # normal termination
./procmon sh -c 'exit 3'           # exit status 3
./procmon no_such_program          # exec failure
./procmon sh -c 'kill -SEGV $$'    # killed by signal 11
./procmon -t 2 sleep 30            # timeout -> SIGTERM after 2 s
```
Default log file: `logs/process_log.txt`. `procmon` itself returns the child's
exit code, or `128+signal` if the child was killed by a signal, or `127` if exec failed.

## Run tests
```bash
make test
```
10 tests: normal exit, non-zero exit, exec failure, SIGSEGV, SIGKILL, no-argument
command, timeout, SIGTERM-ignoring child, finishing before timeout, invalid usage.

## Observing the process tree
Terminal 1: `./procmon sleep 60`
Terminal 2:
```bash
ps -o pid,ppid,pgid,stat,cmd --forest -C procmon,sleep
pstree -p -s $(pgrep -n procmon)
```

## Safety
The child is placed in its own process group; signals are sent only to that group
(`kill(-pid, ...)`). Unrelated processes are never signalled.

## Limitations / possible improvement
- Supervises one child at a time. *Improvement:* run several children in parallel
  and use `waitpid(-1, ...)` / `SIGCHLD` handling.
- Timeout uses 50 ms polling. *Improvement:* use `SIGCHLD` + `sigtimedwait()` or
  `pidfd`/`timerfd` for event-driven waiting.
- Not supported natively on Windows (needs WSL).
