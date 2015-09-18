/* ------------------------------------------------------------------------
   phase1.c

   University of Arizona
   Computer Science 452
   Fall 2015

   ------------------------------------------------------------------------ */

#include "phase1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "kernel.h"
#include "usloss.h"

/* ------------------------- Prototypes ----------------------------------- */
int sentinel (char *);
extern int start1 (char *);
void dispatcher(int reason, procPtr process);
void launch();
static void enableInterrupts();
void disableInterrupts();
static void checkDeadlock();
void static clockHandler(int dev, void *args);
void inKernelMode();
void dispatcher();
int isZapped();
int getpid();
void dump_processes();
int blockMe(int new_status);
int unblockProc(int pid);
int readCurStartTime();
void timeSlice();
void add(procPtr proc);
procPtr pop();
int findProcSlot();
short removeChild(void);
/* -------------------------- Globals ------------------------------------- */

/* Patrick's debugging global variable... */
int debugflag = 1;

/* the process table */
procStruct ProcTable[MAXPROC];

/* Process lists  */
static procPtr ReadyList;

/* current process ID */
procPtr Current;

/* the next pid to be assigned */
unsigned int nextPid = SENTINELPID;

/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
             Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup()
{
    int i;      /* loop index */
    int result; /* value returned by call to fork1() */

    /* initialize the process table */
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing process table, ProcTable[]\n");

    // initialize all procs in proc table
    for (i = 0; i < MAXPROC; i++) {
        ProcTable[i].nextProcPtr = NULL;
        ProcTable[i].childProcPtr = NULL;
        ProcTable[i].nextSiblingPtr = NULL;
        strcpy(ProcTable[i].name, "EMPTY");
        ProcTable[i].startArg[0] = '\0';
        ProcTable[i].pid = -1;
        ProcTable[i].ppid = -1;
        ProcTable[i].priority = -1;
        ProcTable[i].start_func = NULL; 
        ProcTable[i].stack = NULL;
        ProcTable[i].stackSize = -1;
        ProcTable[i].status = EMPTY;
        ProcTable[i].startTime = -1;
        ProcTable[i].zapped = -1;
        ProcTable[i].zappedWhileBlocked = -1;
        ProcTable[i].kids = -1;

        int j;
        for (j = 0; j < MAXPROC; j++) {
            ProcTable[i].zapList[j] = NULL;
        }
    }

    /* Initialize the Ready list, etc. */
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    
    ReadyList = NULL;

    /* Initialize the clock interrupt handler */
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler;

    /* startup a sentinel process */
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for sentinel\n");
    result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
                    SENTINELPRIORITY);
    if (result < 0) {
        if (DEBUG && debugflag) {
            USLOSS_Console("startup(): fork1 of sentinel returned error, ");
            USLOSS_Console("halting...\n");
        }
        USLOSS_Halt(1);
    }

    /* start the test process */
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for start1\n");
   
    result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
   
    if (result < 0) {
        USLOSS_Console("startup(): fork1 for start1 returned an error, ");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }

    USLOSS_Console("startup(): Should not see this message! ");
    USLOSS_Console("Returned from fork1 call that created start1\n");

    return;
} /* startup */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish()
{
    if (DEBUG && debugflag)
        USLOSS_Console("in finish...\n");
} /* finish */

/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*procCode)(char *), char *arg,
          int stacksize, int priority)
{
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): creating process %s\n", name);

    disableInterrupts();

    /* test if in kernel mode; halt if in user mode */
    inKernelMode();

    /* find an empty slot in the process table */
    int procSlot = findProcSlot();

    /* Return if stack size is too small, or if name or procCode is null*/
    if (stacksize < USLOSS_MIN_STACK) {
        if (DEBUG && debugflag)
            USLOSS_Console("Stack size too small\n");

        return -2;
    }

    if (name == NULL) {
        if (DEBUG && debugflag)
            USLOSS_Console("Name not set\n");

        return -1;
    }

    if (procCode == NULL) {
        if (DEBUG && debugflag)
            USLOSS_Console("Proc code not set\n");

        return -1;
    }

    if (strcmp("sentinel", name) == 0 && priority != SENTINELPRIORITY) {
        if (DEBUG && debugflag)
            USLOSS_Console("Sentinel must have sentinelpriority\n");

        return -1;
    }

    if (priority > MINPRIORITY && strcmp("sentinel", name) != 0) {
        if (DEBUG && debugflag)
            USLOSS_Console("Priority too small\n");

        return -1;
    }

    if (priority < MAXPRIORITY) {
        if (DEBUG && debugflag)
            USLOSS_Console("Priority too big\n");
    
        return -1;
    }

    if (procSlot == -1) {
        if (DEBUG && debugflag)
            USLOSS_Console("No space in proc table\n");

        return -1;
    }

    /* fill-in entry in process table */
    if ( strlen(name) >= (MAXNAME - 1) ) {
        USLOSS_Console("fork1(): Process name is too long.  Halting...\n");
        USLOSS_Halt(1);
    }

    // set the proc's name
    strcpy(ProcTable[procSlot].name, name);
    
    // set the proc's start function
    ProcTable[procSlot].start_func = procCode;

    // set the proc's args
    if ( arg == NULL )
        ProcTable[procSlot].startArg[0] = '\0';
    else if ( strlen(arg) >= (MAXARG - 1) ) {
        USLOSS_Console("fork1(): argument too long.  Halting...\n");
        USLOSS_Halt(1);
    }
    else
        strcpy(ProcTable[procSlot].startArg, arg);

    // set the rest of the proc's info
    ProcTable[procSlot].pid = nextPid;
    ProcTable[procSlot].priority = priority;
    ProcTable[procSlot].stack = malloc(stacksize);
    ProcTable[procSlot].stackSize = stacksize;
    ProcTable[procSlot].status = READY;
    ProcTable[procSlot].startTime = 0;
    ProcTable[procSlot].zapped = 0;
    ProcTable[procSlot].zappedWhileBlocked = 0;
    ProcTable[procSlot].kids = 0;

    // inc nextPid
    nextPid++;

    /*
     * Initialize context for this process, but use launch function pointer for
     * the initial value of the process's program counter (PC)
     */
    USLOSS_ContextInit(&(ProcTable[procSlot].state), USLOSS_PsrGet(),
                       ProcTable[procSlot].stack,
                       ProcTable[procSlot].stackSize,
                       launch);

    /* for future phase(s) */
    p1_fork(ProcTable[procSlot].pid);

    // set parents
    // sentinel and start1 have no parent
    if (strcmp("sentinel", name) == 0 || strcmp("start1", name) == 0) {
        ProcTable[procSlot].ppid = NOPARENT;
    }
    else {
        // set current to your parent this doesn't work, not sure why
        ProcTable[procSlot].ppid = Current->pid;

        // check if Current already has a child, if not set this fork as the child
        if (Current->childProcPtr == NULL) {
            Current->childProcPtr = &ProcTable[procSlot];
        }
        // if current already has children, set this child as a sibling of Current's
        // last child
        else {
            // start at the first child and look until a siblingptr is null
            procPtr child;
            for (child = Current->childProcPtr; child->nextSiblingPtr != NULL; child = child->nextSiblingPtr) {
              child->nextSiblingPtr = &ProcTable[procSlot];
            }
        }
    }

    // inc kids
    if (Current != NULL) {
        Current->kids++;
    }

    // call dispatcher 
    if (strcmp("sentinel", name) != 0) {
      dispatcher(0, &(ProcTable[procSlot]));
    }
    else {
        add(&(ProcTable[procSlot]));
    }

    return ProcTable[procSlot].pid;
} /* fork1 */

/* ------------------------------------------------------------------------
   Name - inKernelMode
   Purpose - Check if the current mode is Kernel. If not call USLOSS_Halt
             If the 0th bit of PSR is 1, we are in kernel mode. If not,
             call USLOSS_Halt
   Parameters - None
   Returns - void, but if USLOSS_Halt is called it will not return at all.
   Side Effects - Could end program if not in kernel mode, else, does nothing
   ------------------------------------------------------------------------ */
void inKernelMode()
{
  if ( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
    //not in kernel mode
    USLOSS_Console("Kernel Error: Not in kernel mode");
    USLOSS_Halt(1);
  }
} /* inKernelMode */

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
    int result;

    if (DEBUG && debugflag)
        USLOSS_Console("launch(): started\n");

    /* Enable interrupts */
    enableInterrupts();

    /* Call the function passed to fork1, and capture its return value */
    result = Current->start_func(Current->startArg);

    if (DEBUG && debugflag)
        USLOSS_Console("Process %d returned to launch\n", Current->pid);

    quit(result);

} /* launch */

/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
             -1 if the process was zapped in the join
             -2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int *code)
{
    disableInterrupts();

    // Make sure current process has child
    if (Current->childProcPtr == NULL) {
        return -2;
    }

    // Iteratively search for a child that has quit and return its pid
    procPtr currProc = Current->childProcPtr;

    // check if its direct child has quit, if so swap that child and its next sibling and
    // return its pid
    if (currProc->status == QUIT) {
        short returnPid = currProc->pid;
        Current->childProcPtr = currProc->nextSiblingPtr;
        
        Current->kids--;
        return returnPid;
    }

    // otherwise go through the siblings
    while (currProc->nextSiblingPtr != NULL) {
        if (currProc->nextSiblingPtr->status == QUIT) {
            short returnPid = currProc->nextSiblingPtr->pid;
            currProc->nextSiblingPtr = currProc->nextSiblingPtr->nextSiblingPtr;
            
            Current->kids--;
            return returnPid;
        } else {
            currProc = currProc->nextSiblingPtr;
        }
    }
    short quitChild = removeChild();

    if (quitChild != -1) 
      return quitChild;

    // Otherwise, No children have quit, block
    Current->status = JOIN_BLOCK;
    dispatcher(1, NULL);

    // will run again when a child has quit, look for child
    int childID = removeChild();
    procPtr child = &(ProcTable[childID % 50]);

    *code = child->quitStatus;
    return childID;
} /* join */

/* ------------------------------------------------------------------------
   Name - removeChild
   Purpose - Helper function for join()
             remove a child that has quit and return its pid
   Parameters - none
   Returns - pid of the quit process
             or -1 if no child has quit
   Side Effects - removes the child from the parents linked list of children
------------------------------------------------------------------------ */
short removeChild(void) {
  procPtr currProc = Current->childProcPtr;

  // check if its direct child has quit, if so swap that child and its next sibling and
  // return its pid
  if (currProc->status == QUIT) {
    short returnPid = currProc->pid;
    Current->childProcPtr = currProc->nextSiblingPtr;
    return returnPid;
  }
  
  // otherwise go through the siblings
  while (currProc->nextSiblingPtr != NULL) {
    if (currProc->nextSiblingPtr->status == QUIT) {
      short returnPid = currProc->nextSiblingPtr->pid;
      currProc->nextSiblingPtr = currProc->nextSiblingPtr->nextSiblingPtr;
      return returnPid;
    } else {
      currProc = currProc->nextSiblingPtr;
    }
  }

  // no child quit, return -1
  return -1;
}

/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int code)
{
    // check to make sure current doesn't have children
    if (Current->childProcPtr != NULL) {
        USLOSS_Halt(1);
    }

    // set quitStatus
    Current->quitStatus = code;

    // set the current's status to quit
    Current->status = QUIT;
    p1_quit(Current->pid);

    // if the quitting process has a parent, make it the first child of the parent
    if (Current->ppid != NOPARENT) {
      // find the parent
      procPtr parent = &ProcTable[Current->ppid % 50];

      // find the child in the proc
      procPtr nextIsChild;

      // if quit child isn't already the first child
      if (parent->childProcPtr->pid != Current->pid) {
        for (nextIsChild = parent->childProcPtr; nextIsChild->nextSiblingPtr != NULL; nextIsChild = nextIsChild->nextSiblingPtr) {
          if (nextIsChild->nextSiblingPtr->pid == Current->pid) {
            // swap the parents child ptr with the quitting process
            procPtr oldChild = parent->childProcPtr;
            procPtr newChild = nextIsChild->nextSiblingPtr;
            parent->childProcPtr = newChild;
            nextIsChild->nextSiblingPtr = newChild->nextSiblingPtr;
            newChild->nextSiblingPtr =oldChild;
            break;
          }
        }  
      }
    }
    // call dispatcher
    dispatcher(2, Current);
} /* quit */


/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - reason -
                        0 - called from fork1
                        1 - called from join/zap
                        2 - called from quit
                        3 - called from clock interrupt
                process - Will be NULL or Current except for fork1, 
                        where it is the childProcess that has just been
                        created.
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(int reason, procPtr process)
{
  // check if current is null
  if (Current == NULL) {
    // add start1
    add(process);

    // execute next process on readyList
    procPtr nextProcess = pop();
    p1_switch(-1, nextProcess->pid);

    // change current  to the new process and start its timer
    Current = nextProcess;
    Current->startTime = USLOSS_Clock();

    // context switch
    enableInterrupts();
    USLOSS_ContextSwitch(NULL, &(nextProcess->state));  
  }
  else {dump_processes();
    if (reason == 0) {
      // fork1 Condition
      if (DEBUG && debugflag)
        USLOSS_Console("dispatcher(): called from fork1\n");

      // execute parent or child, whichever has higher priority
      //  if same priority, execute child
      if (Current->priority < process->priority) {
        // Continue to execute parent, put child on readyList
        add(process);
        enableInterrupts();
      }
      else {
        // execute child, add parent to readylist
        add(Current);
        p1_switch(Current->pid, process->pid);

        // change current  to the new process and start its timer
        procPtr old = Current;
        Current = process;
        Current->startTime = USLOSS_Clock();

        enableInterrupts();
        USLOSS_ContextSwitch(&(old->state), &(Current->state));
      }
    }
    else if (reason == 1) {
      // join Condition
      if (DEBUG && debugflag)
        USLOSS_Console("dispatcher(): called from join()\n");

      // execute next process on readyList
      procPtr nextProcess = pop();
      p1_switch(Current->pid, nextProcess->pid);

      // change current  to the new process and start its timer
      procPtr old = Current;
      Current = nextProcess;
      Current->startTime = USLOSS_Clock();

      enableInterrupts();
      USLOSS_ContextSwitch(&(old->state), &(Current->state));
    }
    else if (reason == 2) {
      // quit Condition
      if (DEBUG && debugflag)
        USLOSS_Console("dispatcher(): called from quit() for process with pid %d\n", Current->pid);

      // check to see if the parent is join blocked and add parent back to readylist
      int parentIndex = (Current->ppid) % 50;
      procPtr parent = &(ProcTable[parentIndex]);
    
      // if parent was joined blocked
      if (parent->status == JOIN_BLOCK) {
        // set parent back to ready and add it back to the ready list
        parent->status = READY;
        add(parent);
      }

      // check to see if there are processes zapping this process
      int zapi = 0;
      procPtr currProc = Current->zapList[zapi];
      while (currProc != NULL) {
        currProc->status = READY;
        add(currProc);  

        zapi++;
        currProc = Current->zapList[zapi];
      }

      // begin next process
      procPtr nextProcess = pop();
      p1_switch(Current->pid, nextProcess->pid);

      // change current  to the new process and start its timer
      procPtr old = Current;
      Current = nextProcess;
      Current->startTime = USLOSS_Clock();

      enableInterrupts();
      USLOSS_ContextSwitch(&(old->state), &(Current->state));
    }
    else if (reason == 3) {
      // clock Condition
      if (DEBUG && debugflag)
        USLOSS_Console("dispatcher(): called from clock\n");
      
      // Add the incomplete process to the readyList and context switch
      add(Current);

      procPtr nextProcess = pop();
      p1_switch(Current->pid, nextProcess->pid);

      // change current  to the new process and start its timer
      procPtr old = Current;
      Current = nextProcess;
      Current->startTime = USLOSS_Clock();

      enableInterrupts();
      USLOSS_ContextSwitch(&(old->state), &(Current->state));
    }
  } 
} /* dispatcher */


/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
             processes are blocked.  The other is to detect and report
             simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
                   and halt.
   ----------------------------------------------------------------------- */
int sentinel (char *dummy)
{
    if (DEBUG && debugflag)
        USLOSS_Console("sentinel(): called\n");
    while (1)
    {
        checkDeadlock();
        USLOSS_WaitInt();
    }
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void checkDeadlock()
{
  int numReady = 0; // how many procs are left as ready
  int numActive = 0; // number of processes still running
  int i; // loop variable

  // go through the proc table and count processes running and process ready
  for (i = 0; i < 50; i++) {
    if (ProcTable[i].status == READY) {
      numReady++;
      numActive++;
    }

    if (ProcTable[i].status == JOIN_BLOCK || ProcTable[i].status == ZAP_BLOCK) {
      numActive++;
    }
  }

  // check for deadlock
  if (numReady == 1) {
    // not deadlock, just sentinel left, time to quit
    if (numActive == 1) {
      USLOSS_Console("All processes completed.\n");
      USLOSS_Halt(0);
    }
    // deadlock
    else {
      if (DEBUG && debugflag)
        USLOSS_Console("Found deadlock\n");

      USLOSS_Halt(1);
    }
  }
  // lots of people left ready just return
  else {
    return;
  }
} /* checkDeadlock */

/*
 * Enables the interrupts 
 */
static void enableInterrupts(int dev, void *args)
{
  /* turn the interrupts ON iff we are in kernel mode */
  if ( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
    //not in kernel mode
    USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
    USLOSS_Console("disable interrupts\n");
    USLOSS_Halt(1);
  } else {
    /* We ARE in kernel mode */
    USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT); 
  }
}

/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
    /* turn the interrupts OFF iff we are in kernel mode */
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("disable interrupts\n");
        USLOSS_Halt(1);
    } else
        /* We ARE in kernel mode */
        USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT );
} /* disableInterrupts */

/*
 * Clock handler
 */
static void clockHandler(int dev, void *arg) {
    if (DEBUG && debugflag)
        USLOSS_Console("clock handler\n");

    timeSlice();
}

/* ------------------------------------------------------------------------
   Name - void add
   Purpose - This function takes in a procPtr of the process that you wish
             to add to the ReadyList and puts the process in the readylist 
             in its appropriate spot. That spot is right before the next 
             group of priorities. This insures that the newly added process
             is the last to execute in its group of priorities.
    
             To illistrate:

             Item to add's priority: 4
             ReadyList of priorities: ReadyList v
                                                1 - 1 - 2 - 3 - 5 - 6

             ReadyList after add:     ReadyList v
                                                1 - 1 - 2 - 3 - 4 - 5 - 6
                          
   Parameters - procPtr of process you wish to add
   Returns - nothing
   Side Effects - A new item is added to the readylist
   ----------------------------------------------------------------------- */
void add(procPtr proc) {
    if (DEBUG && debugflag)
        USLOSS_Console("Adding process with pid %d and priority %d\n", proc->pid, proc->priority);

    // check if the ready list is empty
    if (ReadyList == NULL) {
        ReadyList = proc;
    }
    // check if proc's priority is greater than the heads
    else if (proc->priority < ReadyList->priority) {
        // switch the current head with the new proc
        proc->nextProcPtr = ReadyList;
        ReadyList = proc;
    }
    // else the ready list is already built and the new proc has a lower priority than the head
    else {
        // start at the head of the readylist
        procPtr next;

        // go through the readylist looking for your spot
        for (next = ReadyList; next->nextProcPtr != NULL; next = next->nextProcPtr) {
            // check if the next priority is greater than yours
            if (next->nextProcPtr->priority > proc->priority) {
                break;
            }
        }

        // swap next's nextprocptr with the proc to be added
        proc->nextProcPtr = next->nextProcPtr;
        next->nextProcPtr = proc;
    }
}

/* ------------------------------------------------------------------------
   Name - procPtr pop()
   Purpose - This function removes and returns the head of the readylist
             and sets the new head to the old head's next process.           
   Parameters - nothing
   Returns - nothing
   Side Effects - The head of the readylist changes to the next item in 
                  the readylist.
   ----------------------------------------------------------------------- */
procPtr pop() {
    // swap head of the readylist with the next item in line
    procPtr temp = ReadyList;
    ReadyList = ReadyList->nextProcPtr;

    // change the next procPtr on temp to nothing
    temp->nextProcPtr = NULL;

    // return the old head of the readylist
    return temp;
}

/* ------------------------------------------------------------------------
   Name - int findProcSlot
   Purpose - This function finds the next available free slot in the
             process table and returns it. It also adjusts nextPid to be
             the correct PID for that table slot.         
   Parameters - nothing
   Returns - nothing
   Side Effects - nothing
   ----------------------------------------------------------------------- */
int findProcSlot() {
    int procSlot = -1;
    int i; // loop variable
    int startPid = nextPid;

    // find the next available slot in the table between startPid and MAXPROC
    for (i = (startPid % MAXPROC); i < MAXPROC; i++) {
        if (ProcTable[i].status == EMPTY) {
            procSlot = i;
            break;
        }
        else {
            nextPid++;
        }
    }

    // if there was no free slot in the previous loop
    if (procSlot == -1) {
        // try to find a free slot between 0 and startPid
        for (i = 0; i < (startPid % MAXPROC); i++) {
            if (ProcTable[i].status == EMPTY) {
                procSlot = i;
                break;
            }
            else {
                nextPid++;
            }
        }
    }

    // return the free space or -1 if no free space
    return procSlot;
}

/* ------------------------------------------------------------------------
   Name - isZapped
   Purpose - check if the current process has been zapped by another process
   Parameters - void
   Returns - 0 if the process has not been zapped
              1 if the process is currently zapped
   Side Effects - none
   ----------------------------------------------------------------------- */
int isZapped() {
    return Current->zapped;
}

/* ------------------------------------------------------------------------
   Name - zap
   Purpose - The process calling zap will block until the process at the
             designated pid quits          
   Parameters - short pid of the process will be waited on to quit
   Returns - 0 if/when the process that is zapped quits
             -1 if the process calling zap is already zapped
   Side Effects - Blocks the process calling zap.
   ----------------------------------------------------------------------- */
int zap(int pid)
{
  // verify the calling proces is not zapped
  if (Current->zapped) {
    return -1;
  }

  procPtr zappedProc = &ProcTable[pid];

  // Add calling process to zappedProc's zapList
  int index = 0;
  while (zappedProc->zapList[index] != NULL) {
    index++;
  }
  zappedProc->zapList[index] = Current;

  //Change zapped Boolean value
  zappedProc->zapped = 1;

  // Zap block calling process
  Current->status = ZAP_BLOCK;

  // zappedWhileBlocked status
  if (zappedProc->status == JOIN_BLOCK || zappedProc->status == ZAP_BLOCK) {
    zappedProc->zappedWhileBlocked = 1;
  }

  dispatcher(1, NULL);
  return 0;
}

int getpid() {
    return Current->pid;
}

void dump_processes() {
    USLOSS_Console("PID	Parent	Priority	Status		# Kids	Name\n");

    int i;
    for (i = 0; i < MAXPROC; i++) {
        USLOSS_Console("%3d	", ProcTable[i].pid);
        USLOSS_Console("%4d	", ProcTable[i].ppid);
        USLOSS_Console("%5d		", ProcTable[i].priority);

        if (Current->pid == ProcTable[i].pid) {
            USLOSS_Console("RUNNING		");
        }
        else if (ProcTable[i].status == EMPTY) {
            USLOSS_Console("EMPTY		");
        }
        else if (ProcTable[i].status == READY) {
            USLOSS_Console("READY		");
        }
        else if (ProcTable[i].status == QUIT) {
            USLOSS_Console("QUIT		");
        }
        else if (ProcTable[i].status == JOIN_BLOCK) {
            USLOSS_Console("JOIN_BLOCK	");
        }
        else if (ProcTable[i].status == ZAP_BLOCK) {
            USLOSS_Console("ZAP_BLOCK	");
        }

	USLOSS_Console("%3d	", ProcTable[i].kids);
        USLOSS_Console("%s\n", ProcTable[i].name);
    }
}

int blockMe(int new_status) {
    // check to see if it this function is allowed to be called
    if (Current->zappedWhileBlocked != 0) {
        return -1;
    }

    if (new_status < 10) {
        USLOSS_Console("new_status must be greater than or equal to 10.\n");
        USLOSS_Halt(1);
    }

    // it is allowed to be blocked, change stus and return
    Current->status = new_status;
    return 0;
}

int unblockProc(int pid) {
    // get the actual process from the pid
    procPtr proc = &ProcTable[pid % 50];

    // check to see if it is allowed to unblock
    if (Current->pid == pid) {
        return -2;
    }

    if (proc->status == EMPTY) {
        return -2;
    }

    if (proc->status != JOIN_BLOCK && proc->status != ZAP_BLOCK) {
        return -2;
    }

    if (proc->status >= 10) {
        return -2;
    }

    if (Current->zapped != 0) {
        return -1;
    }

    // the process can be unblocked so unblock it
    proc->status = READY;
    add(proc);
    return 0;
}

int readCurStartTime() {
    return Current->startTime;
}

void timeSlice() {
    // compare times
    int dif = USLOSS_Clock() - readCurStartTime();

    // if the current has been running for its allowed time slice
    if (dif >= TIMESLICE) {
        dispatcher(3, NULL);
    }
}
