#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase5.h>
#include <usyscall.h>
#include <libuser.h>
#include <vm.h>
#include <string.h>

/*
 * phase5.c
 *
 * Date: 11/23/15
 * Authors: Alex Ewing, Andre Takagi
 */

// Extern functions (in libuser.c I think)
extern void mbox_create(sysargs *args_ptr);
extern void mbox_release(sysargs *args_ptr);
extern void mbox_send(sysargs *args_ptr);
extern void mbox_receive(sysargs *args_ptr);
extern void mbox_condsend(sysargs *args_ptr);
extern void mbox_condreceive(sysargs *args_ptr);

// Globals
int debugflag5 = 0;

int vmOn = 0; // if the vm has already been started

// counters
int numPages = 0;
int numFrames = 0;
int numPagers = 0;

int faultBox; // fault Mailbox for pagers

int pagerPids[4];

FTE *frameTable;
FTE *freeFrames;

void *region;

VmStats  vmStats;
static Process processes[MAXPROC];
FaultMsg faults[MAXPROC]; /* Note that a process can have only
                           * one fault at a time, so we can
                           * allocate the messages statically
                           * and index them by pid. */

// Function Prototypes
static void FaultHandler(int type, int offset);
static void vmInit(sysargs *args);
static void vmDestroy(sysargs *args);

/*
 *----------------------------------------------------------------------
 *
 * start4 --
 *
 * Initializes the VM system call handlers. 
 *
 * Results:
 *      MMU return status
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
int start4(char *arg) {
    int pid;
    int result;
    int status;

    /* to get user-process access to mailbox functions */
    systemCallVec[SYS_MBOXCREATE]      = mboxCreate;
    systemCallVec[SYS_MBOXRELEASE]     = mboxRelease;
    systemCallVec[SYS_MBOXSEND]        = mboxSend;
    systemCallVec[SYS_MBOXRECEIVE]     = mboxReceive;
    systemCallVec[SYS_MBOXCONDSEND]    = mboxCondsend;
    systemCallVec[SYS_MBOXCONDRECEIVE] = mboxCondreceive;

    /* user-process access to VM functions */
    systemCallVec[SYS_VMINIT]    = vmInit;
    systemCallVec[SYS_VMDESTROY] = vmDestroy;

    result = Spawn("Start5", start5, NULL, 8*USLOSS_MIN_STACK, 2, &pid);
    if (result != 0) {
        console("start4(): Error spawning start5\n");
        Terminate(1);
    }
    result = Wait(&pid, &status);
    if (result != 0) {
        console("start4(): Error waiting for start5\n");
        Terminate(1);
    }
    Terminate(0);
    return 0; // not reached

} /* start4 */

/*
 *----------------------------------------------------------------------
 *
 * VmInit --
 *
 * Stub for the VmInit system call.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      VM system is initialized.
 *
 *----------------------------------------------------------------------
 */
static void vmInit(sysargs *args) {
    if (debugflag4) {
        USLOSS_Console("process %d: vmInit started\n", getpid());
    }

    CheckMode();

    // get sysarg variables
    int mappings = (long) args->arg1;
    int pages    = (long) args->arg2;
    int frames   = (long) args->arg3;
    int pagers  = (long) args->arg4;

    long addr = vmInitReal(mappings, pages, frames, pagers);
    args->arg1 = (void *) addr;

    // check bad input
    if (addr == -1) {
        args->arg4 = (void *) -1L;
    }

    // set vmOn to true
    vmOn = 1;

    setUserMode();
} /* vmInit */

/*
 *----------------------------------------------------------------------
 *
 * vmDestroy --
 *
 * Stub for the VmDestroy system call.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      VM system is cleaned up.
 *
 *----------------------------------------------------------------------
 */

static void vmDestroy(sysargs *args) {
    if (debugflag4) {
        USLOSS_Console("process %d: vmDestroy started\n", getpid());
    }

    CheckMode();

    // if the vm hasn't been init'd yet, do nothing
    if (vmOn == 0) {
        return;
    }

    vmDestroyReal();

    // set vmOn to false
    vmOn = 0;

    setUserMode();
} /* vmDestroy */

/*
 *----------------------------------------------------------------------
 *
 * vmInitReal --
 *
 * Called by vmInit.
 * Initializes the VM system by configuring the MMU and setting
 * up the page tables.
 *
 * Results:
 *      Address of the VM region.
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
void *vmInitReal(int mappings, int pages, int frames, int pagers) {
    if (debugflag4) {
        USLOSS_Console("process %d: vmInit real\n", getpid());
    }

    CheckMode();

    // check bad input
    if (mappings < 0 || pages < 0 || frames < 0 || pagers < 0 || pagers > MAXPAGERS) {
        return -1;
    }

    // check for duplicate intialization
    if (vmOn == 1) {
        return -1;
    }

    // set global variables
    numPages = pages;
    numFrames = frames;
    numPagers = pagers;

    int status;
    int dummy;

    // start the vm
    status = USLOSS_MmuInit(mappings, pages, frames);
    if (status != USLOSS_MMU_OK) {
        USLOSS_Console("vmInitReal: couldn't init MMU, status %d\n", status);
        return -1;
        //abort(); this was in the skeleton but what is this?
    }

    // set the inturrupt handler
    int_vec[USLOSS_MMU_INT] = FaultHandler;

    // set the region
    region = USLOSS_MmuRegion(&dummy);

    // Initialize page tables.

    // Create the fault mailbox.
    faultBox = MboxCreate(MAXPROC, sizeof(FaultMsg));

    // Fork the pagers.
    pagerTable = malloc(sizeof(int) * pagers);
    for (int i = 0; i < pagers; i++) {
        char name[10];
        sprintf(name, "Pager %d", i);
        pagerPids[i] = fork1(name, Pager, NULL, 4 * USLOSS_MIN_STACK, PAGER_PRIORITY);
    }

    // Zero out, then initialize, the vmStats structure
    memset((char *) &vmStats, 0, sizeof(VmStats));
    vmStats.pages = pages;
    vmStats.frames = frames;

    // Initialize other vmStats fields.
    vmStats.pages = pages;
    vmStats.frames = frames;

    return USLOSS_MmuRegion(&dummy);
} /* vmInitReal */

/*
 *----------------------------------------------------------------------
 *
 * vmDestroyReal --
 *
 * Called by vmDestroy.
 * Frees all of the global data structures
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The MMU is turned off.
 *
 *----------------------------------------------------------------------
 */
void vmDestroyReal(void) {
    if (debugflag4) {
        USLOSS_Console("process %d: vmDestroy started\n", getpid());
    }

    CheckMode();

    // end mmu
    USLOSS_MmuDone();

    // Kill the pagers
    for (int i = 0; i < numPagers; i++) {
        zap(pagerPids[i]);
    }

    // Print vm statistics
    PrintStats();

    // free stuff that we malloc'd

} /* vmDestroyReal */

/*
 *----------------------------------------------------------------------
 *
 * PrintStats --
 *
 *      Print out VM statistics.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Stuff is printed to the USLOSS_Console.
 *
 *----------------------------------------------------------------------
 */
void PrintStats(void) {
     USLOSS_Console("VmStats\n");
     USLOSS_Console("pages:          %d\n", vmStats.pages);
     USLOSS_Console("frames:         %d\n", vmStats.frames);
     USLOSS_Console("diskBlocks:     %d\n", vmStats.diskBlocks);
     USLOSS_Console("freeFrames:     %d\n", vmStats.freeFrames);
     USLOSS_Console("freeDiskBlocks: %d\n", vmStats.freeDiskBlocks);
     USLOSS_Console("switches:       %d\n", vmStats.switches);
     USLOSS_Console("faults:         %d\n", vmStats.faults);
     USLOSS_Console("new:            %d\n", vmStats.new);
     USLOSS_Console("pageIns:        %d\n", vmStats.pageIns);
     USLOSS_Console("pageOuts:       %d\n", vmStats.pageOuts);
     USLOSS_Console("replaced:       %d\n", vmStats.replaced);
} /* PrintStats */

/*
 *----------------------------------------------------------------------
 *
 * FaultHandler
 *
 * Handles an MMU interrupt. Simply stores information about the
 * fault in a queue, wakes a waiting pager, and blocks until
 * the fault has been handled.
 *
 * Results:
 * None.
 *
 * Side effects:
 * The current process is blocked until the fault is handled.
 *
 *----------------------------------------------------------------------
 */
static void FaultHandler(int type /* MMU_INT */, int arg  /* Offset within VM region */) {
    int cause;

    assert(type == MMU_INT);
    cause = MMU_GetCause();
    assert(cause == MMU_FAULT);
    vmStats.faults++;

    /*
     * Fill in faults[pid % MAXPROC], send it to the pagers, and wait for the
     * reply.
     */
} /* FaultHandler */

/*
 *----------------------------------------------------------------------
 *
 * Pager 
 *
 * Kernel process that handles page faults and does page replacement.
 *
 * Results:
 * None.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */
static int Pager(char *buf) {
    while(1) {
        /* Wait for fault to occur (receive from mailbox) */
        /* Look for free frame */
        /* If there isn't one then use clock algorithm to
         * replace a page (perhaps write to disk) */
        /* Load page into frame from disk, if necessary */
        /* Unblock waiting (faulting) process */
    }
    return 0;
} /* Pager */
