# file_sync_system

A C-based file synchronization system designed to monitor directories, sync changes (create, modify, delete), and provide a console interface for user interaction. The system is composed of three main components:
- manager.c – Central controller handling configuration, logging, process management, and inotify-based monitoring.

- worker.c – Executes actual synchronization operations (copy, delete, modify).

- console.c – Provides a command-line interface for users to control and query the system.

## Features
- Real-time directory monitoring using inotify

- Multi-process worker pool with dynamic task queueing

- Configurable source/target directories

- Persistent logging with time-stamped events

- Interactive command interface via named pipes (fss_in, fss_out)

- Error tracking and status updates per directory

- Full initial sync + incremental updates (create, modify, delete)

## Project Structure

├── manager.c    # Main daemon-like process <br>
├── worker.c     # Performs actual file operations <br>
├── console.c    # User command interface <br>
├── config.txt   # List of directories to initially sync <br>
├── Makefile     # Build instructions <br>
└── README.md    # This file <br>

## Usage 
- Create a file named config.txt listing source-target directory pairs: <br>
/source/dir1 /backup/dir1 <br>
/source/dir2 /backup/dir2
-Start the Manager <br> 
./manager -c config.txt -l manager.log -n 5 <br> 
  -c → path to configuration file <br>
  -l → path to log file <br>
  -n → max number of concurrent workers <br>

- Interact via Console <br>
Open another terminal and run: <br>
./fss_console -l console.log

- Available commands: <br>
  - add <source> <target> -> adds directory to be backed up and monitored
  - status <directory> -> prints information about a directory (last sync, errors, currently being monitored etc)
  - cancel <source> -> stops monitoring directory
  - sync <directory> -> syncs immediately a directory
  - shutdown -> shuts down workers, manager and console
