pcfsh
-----

# Author
[Princeton Ferro](mailto:pferro@u.rochester.edu)

# Commands Supported
Supported commands are `cd`, `fg`, `bg`, `jobs`, and `exit`. These are implemented according to the POSIX standards defined in the manpages (except not for `cd`). Type `help` to get a list of these commands and their usage.

# How it works
1. user gives input
2. input has to be parsed and analyzed
    - parsing is done with a LL(1) grammar (see `parser.h` for the grammar)
    - semantic analyzer (see `analyzer.h`) processes the parse tree and simplifies it to a (sort of) "AST"
3. then `job_exec()` is called on the "AST", which figures out what programs to exec and how to redirect everything. A new object is created (`struct job`) representing the running job.
4. after the job finishes (if it is background, then this step happens immediately), a check is done on all `job` objects and some cleanup is done on finished jobs.
5. on process termination, a cleanup (via `atexit(3)`) is run on all jobs, and any child processes are sent `SIGKILL`

# Testing
run with `make run` or `make run_valgrind` to check memory leaks
