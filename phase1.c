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
void dispatcher(void);
void launch();
static void enableInterrupts();
void disableInterrupts();
static void checkDeadlock();
void static clockHandler(int dev, void *args);
void inKernelMode();

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
        ProcTable[i].name[0] = '\0';
        ProcTable[i].startArg[0] = '\0';
        ProcTable[i].pid = -1;
        ProcTable[i].ppid = -1;
        ProcTable[i].priority = -1;
        ProcTable[i].start_func = NULL; 
        ProcTable[i].stack = NULL;
        ProcTable[i].stackSize = 0;
        ProcTable[i].status = EMPTY;
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
    int procSlot = -1;

    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): creating process %s\n", name);

    /* test if in kernel mode; halt if in user mode */
    inKernelMode();

    /* find an empty slot in the process table */
    int i;
    for (i = 0; i < MAXPROC; i++) {
        if (ProcTable[i].status == EMPTY) {
            procSlot = i;
            break;
        }
    }

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

    if (priority > MINPRIORITY && strcmp("sentinel", name) != 0) {
        if (DEBUG && debugflag)
            USLOSS_Console("Priority too small\n");

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
    ProcTable[procSlot].stack = malloc(stacksize * sizeof(char));
    ProcTable[procSlot].stackSize = stacksize;
    ProcTable[procSlot].status = READY;
    ProcTable[procSlot].startTime = USLOSS_Clock();

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

    // sentinel and start1 exempt
    if (strcmp("sentinel", name) != 0 && strcmp("start1", name) != 0) {
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
            procPtr child = Current->childProcPtr;
            for (child; child->nextSiblingPtr != NULL; child = child->nextSiblingPtr) {}

            child->nextSiblingPtr = &ProcTable[procSlot];
        }
    }

    /* More stuff to do here... */

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
  if (!USLOSS_PSR_CURRENT_MODE)
    USLOSS_Halt(1);

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
    // Make sure current process has child
    if (Current->childProcPtr == NULL) {
        return -2;
    }

    // Iteratively search for a child that has quit and return its pid
    procPtr currProc = Current->childProcPtr;

    if (currProc->status == QUIT) {
      Current->childProcPtr = currProc->nextSiblingPtr;
    }

    while (currProc->nextSiblingPtr != NULL) {
        if (currProc->nextSiblingPtr->status == QUIT) {
            short returnID = currProc->nextSiblingPtr->pid;
            currProc->nextSiblingPtr == currProc->nextSiblingPtr->nextSiblingPtr
            return returnID;
        } else {
            currProc = currProc->nextSiblingPtr;
        }
    }

    // No children have quit, remove parent from ready list and block
    removeFromReadyList();
    Current.status = JOINBLOCKED;

} /* join */

/* ------------------------------------------------------------------------
   Name - removeFromReadyList
   Purpose - Removes the current process from the ready list
   Parameters - Nothing
   Returns - Nothing
   Side Effects - will change process's nextProcPtr and alter readyList
   ------------------------------------------------------------------------ */
void removeFromReadyList()
{
  procPtr currProc = ReadyList;
  while (currProc->nextProcPtr->pid != Current->pid) {
    currProc = currProc->nextProcPtr;
    if (currProc == NULL) {
      if (DEBUG && debugflag)
        USLOSS_Console("current process is not on readylist");
      USLOSS_Halt(1);
    }
  }
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

    Current->status = QUIT;

    p1_quit(Current->pid);
} /* quit */


/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - pid or zero if we are popping a proc off the readylist
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(short pid)
{
  // TODO: if pid 0
    // pop process
    //change it's nextptr
  // TODO: if pid is a pid
    // call readylist.add()
    procPtr nextProcess;

    p1_switch(Current->pid, nextProcess->pid);
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
} /* checkDeadlock */

/*
 * Enables the interrupts TODO write this
 */
static void enableInterrupts(int dev, void *args)
{

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

    // compare times
}
