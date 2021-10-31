# Modified xv6-riscv

## Installation

Assuming you already have the folder installed (for evaluation), run the xv6 shell:

Default:

```sh
make qemu
```

Scheduler specified (First come First Served and the default Round Robin)

```sh
make qemu SCHEDULER=$OPTION
```

### Spec 1

-   Added `sys_trace` to `sysproc.c`. this assigns the mask

        ```c
        uint64

    sys_trace(void)
    {
    if (argint(0, &myproc()->tracemask) < 0)
    return -1;

        return 0;

    }


        ```

-   Now add the implementation in `strace.c` and link in `Makefile` (add $U/\_strace\ to U_Progs)

        ```c
        #include "kernel/param.h"
        #include "kernel/types.h"
        #include "kernel/stat.h"
        #include "user/user.h"

        int main(int argc, char *argv[]) {
        int i;
        char *nargv[MAXARG];

        if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
            fprintf(2, "Usage: %s mask command\n", argv[0]);
            exit(1);
        }

        if (trace(atoi(argv[1])) < 0) {
            fprintf(2, "%s: trace failed\n", argv[0]);
            exit(1);
        }

        for(i = 2; i < argc && i < MAXARG; i++){
        	nargv[i-2] = argv[i];
        }
        exec(nargv[0], nargv);
        exit(0);

    }

    ```

    ```

-   Added `tracemask` to the proc struct `proc.h` and initialised the mask in `fork()` function in `proc.c`. Then added `syscall_names` and `syscallnum` array to make the selection of the syscall in `syscall()`. The modified `syscall()` to acquire the required output.

    ```c
    void syscall(void)
    {
        struct proc *p = myproc();

        int num = p->trapframe->a7;
        int arr[syscallnum[num]];

        for (int i = 0; i < syscallnum[num]; i++)
            arr[i] = argraw(i);

        if(num > 0 && num < NELEM(syscalls) && syscalls[num])
        {
            p->trapframe->a0 = syscalls[num]();
            if (p->mask & 1 << num)
            {
                printf("%d: syscall %s (", p->pid, syscall_names[num]);
                for (int i = 0; i < syscallnum[num]; i++)
                    printf("%d ", arr[i]);
                printf("\b) -> %d\n", argraw(0));
            }
        }
        else
        {
            printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
            p->trapframe->a0 = -1;
        }
    }
    ```

### Spec 2

#### **FCFS**

-   Added `ctime` in the `struct proc` intialized in allocProc by ticks. Then proceeded to initialize rtime, trtime, etime and other attributes of proc struct. The added the flag checker in `Makefile`

    ```makefile
    ifndef SCHEDULER
        SCHEDULER:=RR
    endif

    CFLAGS+="-D$(SCHEDULER)"
    ```

-   Modified `scheduler()` to account fo rthe added scehduling algorithm (FCFS).

-   Simply loop through all procs int he table and compare their ctime (creation time) then run the lowest ctime holding proc.

-   Also disable yield when FCFS is selected as it was specified for non-preemption (in the document).

        ```c
        #ifdef FCFS
        struct proc *firstComeProc = 0;
        for (p = proc; p < &proc[NPROC]; p++)
        {
          acquire(&p->lock);
          if (p->state == RUNNABLE)
          {
            if (!firstComeProc || firstComeProc->ctime > p->ctime)
            {
              if (firstComeProc)
                release(&firstComeProc->lock);
              firstComeProc = p;
              continue;
            }
          }
          release(&p->lock);
        }
        // As long as firstproc contains a process
        if (firstComeProc)
        {
          firstComeProc->num_runs++;
          firstComeProc->state = RUNNING;
          c->proc = firstComeProc;
          swtch(&c->context, &firstComeProc->context); //context switching
          c->proc = 0;
          release(&firstComeProc->lock);
        }

    #else

    ```



    ```

#### **PBS**

-   Added a variable `priority` in `struct proc` in `proc.h`In `allocproc` in `proc.c`, gave the default value as 60. Specifically initialized teh `priority` attribute for `struct proc` as 60 as specified in the document.

-   Modified teh scheduler to now include `PBS`.

    ```c
    #ifdef PBS
    struct proc *priorityProc = 0;
    int dp = 120;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      int niceness = 5;

      if(p->num_runs > 0 && p->sleeptime + p->rtime != 0)
      {
        niceness = (int)((p->sleeptime * 10) / (p->sleeptime + p->rtime));
      }
      int proc_dp = p->priority - niceness + 5 < 100 ? (p->priority - niceness + 5) : 100;
      proc_dp = proc_dp < 100 ? proc_dp: 100;
      proc_dp = 0 > proc_dp ? 0:proc_dp;

      if (p->state == RUNNABLE)
      {
        if (!priorityProc || proc_dp < dp ||
        (dp==proc_dp &&
        (p->num_runs < priorityProc->num_runs || (p->num_runs == priorityProc->num_runs && p->ctime < priorityProc->ctime ))))
        {
            if(priorityProc)
              release(&priorityProc->lock);

            dp = proc_dp;
            priorityProc = p;
            continue;
        }
      }
      release(&p->lock);
    }
    if (priorityProc)
    {
      priorityProc->num_runs++;
      priorityProc->starttime = ticks;
      priorityProc->state = RUNNING;
      priorityProc->rtime = 0;
      priorityProc->sleeptime = 0;

      c->proc = priorityProc;
      swtch(&c->context, &priorityProc->context);
      c->proc = 0;
      release(&priorityProc->lock);
    }
    #else
    ```

    ```

    ```

````
-   As specified in the document

    ```md
    niceness = Int(10 \* sleeptime / (sleeptime + rtime))
    ```

    And, dp

    ```md
    DP = max(0, min(SP − niceness + 5, 100))
    ```

    So, we calculate the priorities dynamically and then if the priority number is lower,  we run that process.

-   `allocproc()` initialized variables

    ```c
    p->ctime = ticks;
  p->pid = allocpid();
  p->state = USED;
p->rtime = 0;
p->trtime = 0;
 p->num_runs = 0;
 p->time_wait = 0;
 p->priority = 60;
p->etime = 0;
p->total_wait = 0;

    ```

-   Added user implementation of set_priority

    `set_priority.c`

    ```c
    #include "kernel/param.h"
    #include "kernel/types.h"
    #include "kernel/stat.h"
    #include "user/user.h"

    int
    main(int argc, char *argv[])
    {
        int priority, pid;
  if(argc < 3){
    fprintf(2,"Usage: nice pid priority\n");
    exit(1);
  }

  priority = atoi(argv[1]);
  pid = atoi(argv[2]);

  if (priority < 0 || priority > 100){
    fprintf(2,"Invalid priority (0-100)!\n");
    exit(1);
  }
  set_priority(priority, pid);
  exit(1);
    }
    ```

    `sysproc.c`

    ```c
    uint64 sys_set_priority()
    {
        int pid, priority;
        if(argint(0, &priority) < 0)
            return -1;
        if(argint(1, &pid) < 0)
            return -1;

        return set_priority(priority, pid);
    }
    ```

#### MLFQ

-   MLFQ used 5 priority queues as specified, as as specified, teh intiiated process gets pushed to te highest priority, now, all processes in higher priority must be executed before any lower priority process is run. Preemption (forcefull putting a process in wait state) is used if ta process exceeds its queue's time slice and it is placed ina  lower queue. But this means that processes that relinquish control like in the given exacmple of IO processes can relinquish control right as the time slice is used up but stay at the same priority level. This way, a process can use this strategy to make sure it's priority never goes down and gets more cpu time despite its higher number of runs.

### Spec 3

-   In the struct proc, added a variable, total run time, `total_rtime` which calculates the total running time of the process in the struct proc
-   Calculate the waiting time, `wtime`, from the precalculated variables in the process
-   Changed the code in procdump to print the values when it is scheduling for PBS differently.

```c
#ifdef PBS
    int wtime = ticks - p->ctime - p->total_rtime;
    printf("%d %s %s\t%d\t%d\t%d", p->pid, state, p->name, p->total_rtime, wtime, p->num_runs);
    printf("\n");
#else
````

### Benchmark

It Ran on 3 CPUs. The performance analysis for the 3 scheduling methods are as follows:

-   **RR**: Average rtime 38, wtime 144
-   **FCFS**: Average rtime 85, wtime 114
-   **PBS**: Average rtime 45, wtime 108

---
