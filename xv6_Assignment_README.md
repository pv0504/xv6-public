# xv6 Assignment

This repository contains modifications to the MIT xv6 operating system
as part of the Operating Systems course assignments.

------------------------------------------------------------------------

# Implemented Features

- Process History
- Block / Unblock System Calls
- File Permissions (`chmod`)
- Signal Handling
- Dynamic Priority Process Scheduling

------------------------------------------------------------------------

## 1. Process History

Added a new system call:

```c
gethistory();
```

It displays information about recently exited processes, including:

- PID
- Process name
- Memory usage
- Start time
- End time
- Total runtime

------------------------------------------------------------------------

## 2. Block / Unblock System Calls

Added two new shell commands:

```sh
block <syscall_number>
unblock <syscall_number>
```

### Features

- Only the shell can block or unblock system calls.
- Blocked system calls immediately return `-1`.
- Child shells inherit the blocked syscall mask.
- A child shell cannot unblock a syscall blocked by its parent.
- Blocking is enforced only on child shells created after the block
  operation.
- Blocking of critical system calls such as `fork` and `exit` is
  disallowed.

### Example

```sh
$ block 22
$ history
$ sh
```

Child shell:

```sh
$ history
```

------------------------------------------------------------------------

## 3. File Permissions (`chmod`)

New command:

```sh
chmod <file> <mode>
```

### Permission Mapping

  Mode   Permission

  ------ ------------

  0      ---
  1      --x
  2      -w-
  3      -wx
  4      r--
  5      r-x
  6      rw-
  7      rwx

### Features

- Added a `mode` field to both `struct inode` and `struct dinode`.
- Newly created files are initialized with permission `7` (`rwx`).
- Permission changes are persisted to disk.
- Execute permission is enforced inside `exec()`.
- Invalid permission values are rejected.

------------------------------------------------------------------------

## 4. Signal Handling

### Features

- Added a `signal()` system call for registering custom signal
  handlers.
- Each process maintains:
  - `signal_handler`
  - `handler_pending`
- Registered handlers are invoked when a signal is delivered.
- Added kernel and user-space support through the syscall interface.

### Testing

Run the following programs to verify the implementation:

```bash
test1
test2
```



------------------------------------------------------------------------

## 5. Dynamic Priority Process Scheduling

### Features

- Added `custom_fork(start_later, exec_ticks)`.
- Added `scheduler_start()`.
- Supports delayed process execution.
- Dynamic priority is computed as:

```text
Priority = Base Priority − (ALPHA × CPU Ticks) + (BETA × Waiting Time)
```

- CPU-intensive processes gradually lose priority.
- Waiting processes gradually gain priority, reducing starvation.
- Ties are broken using the smaller PID.
- Supports execution limits using `exec_ticks`.
- Records:
  - Turnaround Time (TAT)
  - Waiting Time (WT)
  - Response Time (RT)
  - Context Switches (CS)

### Scheduling Metrics

  Metric   Description

  -------- ---------------------------------------

  TAT      End Time − Creation Time
  WT       Total waiting time in the ready queue
  RT       First CPU allocation − Creation Time
  CS       Number of context switches

### Example

```c
int pid = custom_fork(1, 50);
scheduler_start();
```

The child waits until `scheduler_start()` is invoked and is allowed to
execute for at most 50 CPU ticks.



### Testing

Run:

```bash
test_sched
```



------------------------------------------------------------------------

# Build

```bash
make clean
make
```

# Run

```bash
make qemu
```

or

```bash
make qemu-nox
```

# Testing

Verify that:

- Process history is recorded correctly.
- System call blocking behaves as expected.
- File permissions are enforced.
- Signal handlers execute correctly.
- Delayed processes execute only after `scheduler_start()`.
- Scheduling statistics (TAT, WT, RT, CS) are displayed on process
  exit.
