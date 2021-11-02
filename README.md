# XV6 RISCV (Modified)

```
Original:
xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern RISC-V multiprocessor using ANSI C.

ACKNOWLEDGMENTS

xv6 is inspired by John Lions's Commentary on UNIX 6th Edition (Peer
to Peer Communications; ISBN: 1-57398-013-7; 1st edition (June 14,
2000)). See also https://pdos.csail.mit.edu/6.828/, which
provides pointers to on-line resources for v6.

The following people have made contributions: Russ Cox (context switching,
locking), Cliff Frey (MP), Xiao Yu (MP), Nickolai Zeldovich, and Austin
Clements.

We are also grateful for the bug reports and patches contributed by
Takahiro Aoyagi, Silas Boyd-Wickizer, Anton Burtsev, Ian Chen, Dan
Cross, Cody Cutler, Mike CAT, Tej Chajed, Asami Doi, eyalz800, Nelson
Elhage, Saar Ettinger, Alice Ferrazzi, Nathaniel Filardo, flespark,
Peter Froehlich, Yakir Goaron,Shivam Handa, Matt Harvey, Bryan Henry,
jaichenhengjie, Jim Huang, Matúš Jókay, Alexander Kapshuk, Anders
Kaseorg, kehao95, Wolfgang Keller, Jungwoo Kim, Jonathan Kimmitt,
Eddie Kohler, Vadim Kolontsov , Austin Liew, l0stman, Pavan
Maddamsetti, Imbar Marinescu, Yandong Mao, , Matan Shabtay, Hitoshi
Mitake, Carmi Merimovich, Mark Morrissey, mtasm, Joel Nider,
OptimisticSide, Greg Price, Jude Rich, Ayan Shafqat, Eldar Sehayek,
Yongming Shen, Fumiya Shigemitsu, Cam Tenny, tyfkda, Warren Toomey,
Stephen Tu, Rafael Ubal, Amane Uehara, Pablo Ventura, Xi Wang, Keiichi
Watanabe, Nicolas Wolovick, wxdao, Grant Wu, Jindong Zhang, Icenowy
Zheng, ZhUyU1997, and Zou Chang Wei.

The code in the files that constitute xv6 is
Copyright 2006-2020 Frans Kaashoek, Robert Morris, and Russ Cox.

ERROR REPORTS

Please send errors and suggestions to Frans Kaashoek and Robert Morris
(kaashoek,rtm@mit.edu). The main purpose of xv6 is as a teaching
operating system for MIT's 6.S081, so we are more interested in
simplifications and clarifications than new features.

BUILDING AND RUNNING XV6

You will need a RISC-V "newlib" tool chain from
https://github.com/riscv/riscv-gnu-toolchain, and qemu compiled for
riscv64-softmmu. Once they are installed, and in your shell
search path, you can run "make qemu".
```

## System Call:

Implemented a syscall `trace()` , invoked by `strace` during runtime.

Syntax: `strace <mask> <command>`

This returns a brief analysis of all system calls that are invoked during the runtime of a command.

## SCHEDULING ALGORITHMS:

The scheduler can be switched using the `SCHEDULER` flag. For example: If you want to set the schedulert to FCFS, then do `make qemu SCHEDULER=FCFS`. If no argument is provided, then the default scheduler (Round Robin) is used.

- ### FCFS: 

  - First, disable the preemptive scheduling for FCFS scheduling. For doing this, go to `trap.c`. Under `usertrap()` and `kerneltrap()` place the `yield()` section of code under `#ifdef` as follows:

    ```c
    #ifdef FCFS
    	//relevant code
    	yield()
    #endif
    ```

  - Go to `proc.h` , and under `struct proc` , define a new variable `start time`.
  - Go to `proc.c`. Under `allocproc()`, initialise this newly created variable as `p->create_time = ticks`.
  - Next, traverse the process table, and find a process with status `RUNNABLE`, which has the shortest start time.
  - Schedule this process, while acquiring and releasing the locks appropriately.

- ### PBS:

  - Under `struct proc` in `proc.h`, define six new variable `time_stopped` and `time_stopped_temp`, `times_chosen` , `static_priority` , `dynamic_priority` and `niceness`. Also, disable preemption as discussed above.
  - Whenever the process goes to sleep, it enters the `sleep()` function in `proc.c`. Over here, set `time_stopped_temp = ticks`. Now go to `wakeup()`. From here, we can find the time for which the process was sleeping. So, just do `time_stopped += (ticks - time_stopped_temp)`. 
  - The `waitx()` function already checks for the run-time under the variable `rtime`, so we need not implement is separately. Using `p->rtime` and `p->time_stopped` we can calculate niceness.
  - Now just scan the process table, and schedule the appropriate process according to the priority order <dynamic priority, number of times it was scheduled before, start time>. We can keep updating `dynamic_priority` , `niceness` and `times_chosen` here itself.
  - `setpriority()` can be easily implemented by making a stub in user space, and changing the appropriate files as we did in spec 1.

- ### MLFQ:

  - Make a`struct Queue` which supports the functions : `push()`, `pop()`, `front()`, `eraseq()`
  - Make an array of 5 elements (say , mlfq[5]) , where each element is of the form `struct Queue`.
  - Traverse the process table, and place each `RUNNABLE` process into the appropriate queues.
  - Under `trap.c` , `yield()` the process, when there's a timer interrupt , and it's queue time is over. 
  - Implement `ageing()`. The `eraseq()` function that we implemented earlier will be quite useful here.
  - If the process voluntarily relinquishes the control of CPU, it is removed from the queue. It is rescheduled to the same queue level later. This helps in avoiding useless wait time for the next process in same/lower level.



## Analysis(schedulertest):



|                 | **rtime** | **wtime** |
| --------------- | --------- | --------- |
| **Round Robin** | 18        | 114       |
| **FCFS**        | 33        | 40        |
| **PBS**         | 17        | 106       |
| **MLFQ**        | 19        | 172       |

(MLFQ was checked on 1 CPU, while others were checked on multiple CPU's)

FCFS has has the highest rtime, and lowest wtime as expected - as it spends a lot less time in context switching.

MLFQ has considerably high wtime due to two reasons - single CPU and also it takes a lot of time to decide which process to run next.

Round Robin also has a considerably high wtime because of regular context switching.

Round Robin and PBS have almost same wtime's and rtime's. This is because - although PBS is non-preemptive, updating niceness, and selecting process on basis of dynamic priority takes some time.



## Procdump(Modified)

Procdump now returns a more detailed set of analysis with improved formatting for MLFQ and PBS scheduling. Use `ctrl-p` to dump the output.



