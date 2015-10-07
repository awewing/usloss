/*------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

  ------------------------------------------------------------------------ */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "message.h"
#include "p1.c"
/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);

void block(int mboxID, int block, int size, char *message, int *realSize);
int unblock(int mboxID, int block, int size, char *message, int *realSize);
int getSlot();
int sendToSlot(mailbox *mbox, void *msg_ptr, int msg_size);
void removeSlot(int mboxID);
int waitDevice(int type, int unit, int *status);
static void nullsys(systemArgs *args);
static void clockHandler2(int dev, void *args);
static void diskHandler(int dev, void *args);
static void terminalHandler(int dev, void *args);
static void syscallHandler(int dec, void *args);
static void enableInterrupts();
static void disableInterrupts();
void check_kernel_mode(char* name);
/* -------------------------- Globals ------------------------------------- */
void (*sys_vec[MAXSYSCALLS])(systemArgs *args);

int debugflag2 = 0;

// the mail boxes 
mailbox clockBox;
mailbox termBoxes[USLOSS_TERM_UNITS];
mailbox diskBoxes[USLOSS_DISK_UNITS];

mailbox MailBoxTable[MAXMBOX]; // mail box table
mailSlot MailSlots[MAXSLOTS];  // slot table
process processTable[MAXPROC]; // proc table

int clockTicks = 0;

/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): at beginning\n");

    // check we are in kernel
    check_kernel_mode("start1");

    // Disable interrupts
    disableInterrupts();

    // Initialize the mail box table, slots, & other data structures.
    for (int i = 0; i < MAXMBOX; i++) {
        MailBoxTable[i].mboxID = -1;
        MailBoxTable[i].numSlots = -1;
        MailBoxTable[i].numSlotsUsed = -1;
        MailBoxTable[i].slotSize = -1;
        MailBoxTable[i].headPtr = NULL;
        MailBoxTable[i].endPtr = NULL;
        MailBoxTable[i].blockStatus = 1;
    }

    for (int i = 0; i < MAXSLOTS; i++) {
        MailSlots[i].mboxID = -1;
        MailSlots[i].status = -1;
        MailSlots[i].nextSlot = NULL;
        MailSlots[i].message[0] = '\0';
        MailSlots[i].size = -1;
    }

    for (int i = 0; i < MAXPROC; i++) {
        processTable[i].pid = -1;
        processTable[i].blockStatus = NOT_BLOCKED;
        processTable[i].message[0] = '\0';
        processTable[i].size = -1;
        processTable[i].mboxID = -1;
        processTable[i].timeAdded = -1;
    }

    // Initialize USLOSS_IntVec and system call handlers,
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler2;
    USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;
    USLOSS_IntVec[USLOSS_TERM_INT] = terminalHandler;
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscallHandler;

    // allocate mailboxes for interrupt handlers.  Etc... 
    clockBox = MailBoxTable[MboxCreate(0, sizeof(long))];

    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        diskBoxes[i] = MailBoxTable[MboxCreate(0, sizeof(long))];
    }

    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        termBoxes[i] = MailBoxTable[MboxCreate(0, sizeof(long))];
    }

    // intialize sys_vec
    for (int i = 0; i < MAXSYSCALLS; i++) {
        sys_vec[i] = nullsys;
    }

    // all done creating stuff, re enable interrupts
    enableInterrupts();

    // Create a process for start2, then block on a join until start2 quits
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): fork'ing start2 process\n");
    int kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
    int status;
    if ( join(&status) != kid_pid ) {
        USLOSS_Console("start2(): join returned something other than ");
        USLOSS_Console("start2's pid\n");
    }

    return 0;
} /* start1 */


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
                mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size)
{
    check_kernel_mode("MboxCreate");
    disableInterrupts();

    // make sure they are requesting a positive number of slots
    if (slots < 0) {
        return -1;
    }

    // check slot_size isn't too big or too small
    if (slot_size > MAX_MESSAGE || slot_size < 0) {
        return -1;
    }

    // id of the mailbox we will use
    int ID = -1;

    // go through the table of mail boxes looking for an open one
    for (int i = 0; i < MAXMBOX; i++) {
        if (MailBoxTable[i].mboxID == -1) {
            ID = i;
            break;
        }
    }

    // if it made it all the way through the loop without finding a box, then the boxtable is full
    if (ID == -1) {
        return -1;
    }

    // initialize Values
    mailbox *mbox = &(MailBoxTable[ID]);
    mbox->mboxID = ID;
    mbox->numSlots = slots;
    mbox->numSlotsUsed = 0;
    mbox->headPtr = NULL;
    mbox->slotSize = slot_size;
    mbox->blockStatus = NOT_BLOCKED;

    enableInterrupts();
    return ID;
} /* MboxCreate */

int MboxRelease(int mailboxID) {
    check_kernel_mode("MboxRelease");
    disableInterrupts();

    // get the mailbox
    mailbox *mbox = &(MailBoxTable[mailboxID]);

    // check to make sure the mail box is in use
    if (mbox->mboxID == -1) {
        return -1;
    }

    // update that this mail box is about to be deleted
    MailBoxTable[mailboxID].mboxID = -1;
    
    // unblock all processes blocked on this mailbox
    int unblockID = -1; // the index of the proccess to unblock

    // go through the entire proc list and find the process to be unblocked
    for (int i = 0; i < MAXPROC; i++) {
        // find a process blocked on this mailbox
        if (processTable[i].mboxID == mailboxID) {
            // if this is the first process to be found
            if (unblockID == -1) {
                // set the unblockid to this new processes index in the table
                unblockID = i;
            }
            // otherwise
            else {
                // compare the start times of the old process and this new found one
                if (processTable[unblockID].timeAdded > processTable[i].timeAdded) {
                    // if the new found process was blocked before the last found process, swap
                    unblockID = i;
                }
            }
        }
    }

    // if a blocked person was found, release them
    if (unblockID != -1) {
        unblockProc(processTable[unblockID].pid);

        // remove their info from the procTable
        processTable[unblockID].pid = -1;
        processTable[unblockID].blockStatus = NOT_BLOCKED;
        processTable[unblockID].message[0] = '\0';
        processTable[unblockID].size = -1;
        processTable[unblockID].mboxID = -1;
        processTable[unblockID].timeAdded = -1;
    }

    // if there are still more processes to be unblocked
    while (unblockID != -1) {
        // set unblockID back to -1;
        unblockID = -1;

        // go through the entire proc list and find the process to be unblocked
        for (int i = 0; i < MAXPROC; i++) {
            // find a process blocked on this mailbox
            if (processTable[i].mboxID == mailboxID) {
                // if this is the first process to be found
                if (unblockID == -1) {
                    // set the unblockid to this new processes index in the table
                    unblockID = i;
                }
                // otherwise
                else {
                    // compare the start times of the old process and this new found one
                    if (processTable[unblockID].timeAdded > processTable[i].timeAdded) {
                        // if the new found process was blocked before the last found process, swap
                        unblockID = i;
                    }
                }
            }
        }

        // if a blocked person was found, release them
        if (unblockID != -1) {            
            unblockProc(processTable[unblockID].pid);

            // remove their info from the procTable
            processTable[unblockID].pid = -1;
            processTable[unblockID].blockStatus = NOT_BLOCKED;
            processTable[unblockID].message[0] = '\0';
            processTable[unblockID].size = -1;
            processTable[unblockID].mboxID = -1;
            processTable[unblockID].timeAdded = -1;
        }
    }
    
    // null out all slots used by the mailbox
    slotPtr pre = NULL;
    slotPtr slot = mbox->headPtr;
    while (slot != NULL) {
        slot->mboxID = -1;
        slot->status = -1;
        slot->message[0] = '\0';
        slot->size = -1;

        if (pre != NULL) {
            pre->nextSlot = NULL;
        }

        pre = slot;
        slot = slot->nextSlot;
    }

    // null out the removed mailbox
    MailBoxTable[mailboxID].mboxID = -1;
    MailBoxTable[mailboxID].numSlots = -1;
    MailBoxTable[mailboxID].numSlotsUsed = -1;
    MailBoxTable[mailboxID].slotSize = -1;
    MailBoxTable[mailboxID].headPtr = NULL;
    MailBoxTable[mailboxID].endPtr = NULL;
    MailBoxTable[mailboxID].blockStatus = 1;

    // check if zapped
    if (isZapped()) {
        return -3;
    }

    enableInterrupts();
    return 0;
}

/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
    check_kernel_mode("MboxSend");
    disableInterrupts();

    // get the mail box
    mailbox *mbox = &(MailBoxTable[mbox_id]);

    // check message size isn't too big
    if (msg_size > MAX_MESSAGE) {
        enableInterrupts();
        return -1;
    }
    else if (msg_size > mbox->slotSize) {
        enableInterrupts();
        return -1;
    }

    // check if mailbox exists at first
    if (mbox->mboxID == -1) {
        enableInterrupts();
        return -1;
    }

    // check for 0-slot mbox
    if (mbox->numSlots == 0) {
        // if there was no one waiting on this mailbox, block
        if (!unblock(mbox->mboxID, RECEIVEBLOCK, msg_size, msg_ptr, NULL)) {
            block(mbox->mboxID, SENDBLOCK, msg_size, msg_ptr, NULL);
        }

        // checked to make sure process wasn't zapped
        if (isZapped()) {
            enableInterrupts();
            return -3;
        }

        // check to make mailbox still exists
        if (mbox->mboxID == -1) {
            enableInterrupts();
            return -3;
        }

        enableInterrupts();
        return 0;
    }

    // check if no free slots 
    if (mbox->numSlots == mbox->numSlotsUsed) {
        block(mbox->mboxID, SENDBLOCK, msg_size, NULL, NULL);
    
        // checked to make sure process wasn't zapped
        if (isZapped()) {
            enableInterrupts();
            return -3;
        }

        // check to make mailbox still exists
        if (mbox->mboxID == -1) {
            enableInterrupts();
            return -3;
        }
    }

    // write to that slot
    sendToSlot(mbox, msg_ptr, msg_size);
    
    // unblock people waiting on this mailbox, insuring sent message isn't too big
    if (unblock(mbox->mboxID, RECEIVEBLOCK, msg_size, NULL, NULL) == 2) {
        enableInterrupts();
        return -1;
    }

    // may not be needed
    // checked to make sure process wasn't zapped
    if (isZapped()) {
        enableInterrupts();
        return -3;
    }

    enableInterrupts();
    return 0;
} /* MboxSend */

/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size)
{
    check_kernel_mode("MboxReceive");
    disableInterrupts();

    // get the mailbox
    mailbox *mbox = &(MailBoxTable[mbox_id]);

    // check message size isn't too big
    if (msg_size > MAX_MESSAGE) {
        enableInterrupts();
        return -1;
    }

    // check if mailbox exists at first
    if (mbox->mboxID == -1) {
        enableInterrupts();
        return -1;
    }

    // check for 0-slot mbox
    if (mbox->numSlots == 0) {
        int size = -1;
        int *sPtr = &size;

        // if there was no one waiting on this mailbox, block
        if (!unblock(mbox->mboxID, SENDBLOCK, msg_size, msg_ptr, sPtr)) {
            block(mbox->mboxID, RECEIVEBLOCK, msg_size, msg_ptr, sPtr);
        }

        // checked to make sure process wasn't zapped
        if (isZapped()) {
            enableInterrupts();
            return -3;
        }

        // check to make mailbox still exists
        if (mbox->mboxID == -1) {
            enableInterrupts();
            return -3;
        }

        enableInterrupts();
        return size;
    }

    // check if no messages
    if (mbox->numSlotsUsed == 0) {
        block(mbox_id, RECEIVEBLOCK, msg_size, NULL, NULL);
    
        // no longer blocked
        // checked to make sure process wasn't zapped
        if (isZapped()) {
            enableInterrupts();
            return -3;
        }

        // check to make sure mailbox still exists
        if (mbox->mboxID == -1) {
            enableInterrupts();
            return -3;
        }
    }

    // check message size too small
    if (mbox->headPtr->size > msg_size) {
        enableInterrupts();
        return -1;
    }

    // get the size of the message to save for later
    int size = mbox->headPtr->size;

    // copy information from slot to msg_ptr and remove that slot
    memcpy(msg_ptr, mbox->headPtr->message, msg_size);
    removeSlot(mbox_id);

    // unblock send blocked people
    unblock(mbox_id, SENDBLOCK, msg_size, NULL, NULL);

    // may not be needed
    // checked to make sure process wasn't zapped
    if (isZapped()) {
        enableInterrupts();
        return -3;
    }

    enableInterrupts();
    return size; 
} /* MboxReceive */

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {
    check_kernel_mode("MboxSend");
    disableInterrupts();

    // get the mail box
    mailbox *mbox = &(MailBoxTable[mbox_id]);

    // check message size isn't too big
    if (msg_size > MAX_MESSAGE) {
        enableInterrupts();
        if (debugflag2 && DEBUG2) {
            USLOSS_Console("MboxCondSend: size too big for max\n");
        }
        return -1;
    }
    else if (msg_size > mbox->slotSize) {
        enableInterrupts();
        if (debugflag2 && DEBUG2) {
            USLOSS_Console("MboxCondSend: size too big for box\n");
        }
        return -1;
    }

    // check if mailbox exists at first
    if (mbox->mboxID == -1) {
        enableInterrupts();
        if (debugflag2 && DEBUG2) {
            USLOSS_Console("MboxCondSend: mbox doesn't exist\n");
        }
        return -1;
    }

    // check for 0-slot mbox
    if (mbox->numSlots == 0) {
        // if there was no one waiting on this mailbox, leave
        if (!unblock(mbox->mboxID, RECEIVEBLOCK, msg_size, msg_ptr, NULL)) {
            enableInterrupts();
            return -2;
        }

        // checked to make sure process wasn't zapped
        if (isZapped()) {
            enableInterrupts();
            return -3;
        }

        // check to make mailbox still exists
        if (mbox->mboxID == -1) {
            enableInterrupts();
            return -3;
        }

        enableInterrupts();
        return 0;
    }

    // check if no free slots 
    if (mbox->numSlots == mbox->numSlotsUsed) {
        enableInterrupts();
        return -2;
    }

    // write to that slot, but if no slots available return -2
    if (sendToSlot(mbox, msg_ptr, msg_size) == -1) {
        enableInterrupts();
        return -2;
    }

    // unblock people waiting on this mailbox, insuring sent message isn't too big
    if (unblock(mbox->mboxID, RECEIVEBLOCK, msg_size, NULL, NULL)) {
        enableInterrupts();
        return -1;
    }

    // checked to make sure process wasn't zapped
    if (isZapped()) {
        enableInterrupts();
        return -3;
    }

    enableInterrupts();
    return 0;
}

int MboxCondReceive(int mbox_id, void *msg_ptr, int msg_size) {
    check_kernel_mode("MboxCondReceive");
    disableInterrupts();

    // get the mailbox
    mailbox *mbox = &(MailBoxTable[mbox_id]);

    // check message size isn't too big
    if (msg_size > MAX_MESSAGE) {
        enableInterrupts();
        return -1;
    }

    // check if mailbox exists at first
    if (mbox->mboxID == -1) {
        enableInterrupts();
        return -1;
    }

    // check for 0-slot mbox
    if (mbox->numSlots == 0) {
        int size = -1;
        int *sPtr = &size;

        // if there was no one waiting on this mailbox, leave
        if (!unblock(mbox->mboxID, SENDBLOCK, msg_size, msg_ptr, sPtr)) {
            enableInterrupts();
            return -2;
        }

        // checked to make sure process wasn't zapped
        if (isZapped()) {
            enableInterrupts();
            return -3;
        }

        // check to make mailbox still exists
        if (mbox->mboxID == -1) {
            enableInterrupts();
            return -3;
        }

        enableInterrupts();
        return size;
    }

    // check for mailbox empty
    if (mbox->numSlotsUsed == 0) {
        enableInterrupts();
        return -2;
    }

    // check message size too small
    if (mbox->headPtr->size > msg_size) {
        enableInterrupts();
        return -1;
    }

    // get the size of the message to save for later
    int size = mbox->headPtr->size;

    // copy information from slot to msg_ptr and remove that slot
    memcpy(msg_ptr, mbox->headPtr->message, msg_size);
    removeSlot(mbox_id);

    // unblock send blocked people
    unblock(mbox_id, SENDBLOCK, msg_size, NULL, NULL);

    // checked to make sure process wasn't zapped
    if (isZapped()) {
        enableInterrupts();
        return -3;
    }

    enableInterrupts();
    return size;
}

void block(int mboxID, int block, int size, char message[], int *realSize) {
    // get the correct process from the process table
    process *proc = &processTable[getpid() % 50];

    // add info to that process
    proc->pid = getpid();
    proc->blockStatus = block;
    proc->mboxID = mboxID;
    proc->timeAdded = USLOSS_Clock();
    proc->size = size;

    // if a zero slot message box wants to send a message
    if (message != NULL && block == SENDBLOCK) {
        memcpy(proc->message, message, size);
    }

    // actually block
    blockMe(block);

    if (message != NULL && block == RECEIVEBLOCK) {
        memcpy(message, processTable[getpid() % 50].message, size);
        *realSize = processTable[getpid() % 50].size;
    }

    if (block == RECEIVEBLOCK) {
        // empty out this slot in the processTable
        processTable[getpid() % 50].pid = -1;
        processTable[getpid() % 50].blockStatus = -1;
        processTable[getpid() % 50].message[0] = '\0';
        processTable[getpid() % 50].size = -1;
        processTable[getpid() % 50].mboxID = -1;
        processTable[getpid() % 50].timeAdded = -1;
    }
}

// returns 0 if nothing unblocked
// returns 1 if something was unblocked
int unblock(int mboxID, int block, int size, char message[], int *realSize) {
    // multiple processes may be blocked on the same mailbox, we only want to unblock one of them
    // this keeps track of it. Start at -1 incase no one is blocked on that mailbox
    int unblockID = -1;

    // go through the entire proc list and find the process to be unblocked
    for (int i = 0; i < MAXPROC; i++) {
        // find a process blocked on this mailbox and make sure they are the same type of block
        if (processTable[i].mboxID == mboxID && processTable[i].blockStatus == block) {
            // if this is the first process to be found
            if (unblockID == -1) {
                // set the unblockid to this new processes index in the table
                unblockID = i;
            }
            // otherwise
            else {
                // compare the start times of the old process and this new found one
                if (processTable[unblockID].timeAdded > processTable[i].timeAdded) {
                    // if the new found process was blocked before the last found process, swap
                    unblockID = i;
                }
            }
        }
    }

    // check to see if anyone needs to be unblocked
    if (unblockID != -1) {
        // save their pid
        int pid = processTable[unblockID].pid;

        // check if a receiver wants to get a message thats too big
        int retVal = 1;
        if (block == RECEIVEBLOCK && size > processTable[unblockID].size) {
            retVal++;
        }

        // if a zero slot message box
        if (message != NULL && block == SENDBLOCK) {
            memcpy(message, processTable[unblockID].message, size);
            *realSize = processTable[unblockID].size;
        }
        else if (message != NULL && block == RECEIVEBLOCK) {
            memcpy(processTable[unblockID].message, message, size);
            processTable[unblockID].size = size;
        }

        if (block == SENDBLOCK) {
            // empty out this slot in the processTable
            processTable[unblockID].pid = -1;
            processTable[unblockID].blockStatus = -1;
            processTable[unblockID].message[0] = '\0';
            processTable[unblockID].size = -1;
            processTable[unblockID].mboxID = -1;
            processTable[unblockID].timeAdded = -1;
        }

        // unblock the process
        unblockProc(pid);

        return retVal;
    }
    else {
        return 0;
    }
}

/* ------------------------------------------------------------------------
   Name - getSlot
   Purpose - find next open mail slot
   Parameters - none
   Returns - the address to the next open mail slot
   ----------------------------------------------------------------------- */
int getSlot() {
    int i; // loop variable

    // find a free slot in the slot array
    for (i = 0; i < MAXSLOTS; i++) {
        if (MailSlots[i].status != 1) {
            break;
        }
    }

    // if it got to this point without finding a free slot, halt
    if (i == MAXSLOTS) {
        return -1;
    }

    // return the free slot index
    return i;
} /* mailSlot */

/*
 * Helper function for send
 *  - put message in a slot
 */
int sendToSlot(mailbox *mbox, void *msg_ptr, int msg_size)
{
    // get a free slot from the table
    int slotID = getSlot();

    // make sure we didn't run out of slots
    if (slotID == -1) {
        return -1;
    }

    // get the slot to insert in
    slotPtr slot = &MailSlots[slotID];

    // assign the slot to a mailbox
    // if mailbox is empty, set this slot as the first slot in the box
    if (mbox->headPtr == NULL) {
        mbox->headPtr = slot;
    }
    // otherwise adjust who the old endptr's next is
    else {
        mbox->endPtr->nextSlot = slot;
    }

    // set the box's endptr to this new slot and inc the mailbox's size
    mbox->endPtr = slot;

    // put info in the slot
    slot->mboxID = mbox->mboxID;
    slot->status = 1;
    slot->nextSlot = NULL;
    memcpy(slot->message , msg_ptr, msg_size);
    slot->size = msg_size;

    // increment numSlotsUsed
    mbox->numSlotsUsed++;

    return 0;
}

void removeSlot(int mboxID) {
    // get the mailbox and slot
    mailbox *mbox = &(MailBoxTable[mboxID]);
    slotPtr slot = mbox->headPtr;

    // remove this slot from the mailbox
    mbox->headPtr = slot->nextSlot;

    // if slot was also the endptr, set endptr to null
    if (mbox->headPtr == NULL) {
        mbox->endPtr = NULL;
    }

    // null out the slot
    slot->mboxID = -1;
    slot->status = -1;
    slot->nextSlot = NULL;
    slot->message[0] = '\0';

    // dec how many slots it is using
    mbox->numSlotsUsed--;
}

int waitDevice(int type, int unit, int *status) {
    mailbox *mbox;

    switch (type) {
        case USLOSS_CLOCK_DEV :
            mbox = &clockBox;
            break;
        case USLOSS_DISK_INT :
            mbox = &diskBoxes[unit];
            break;
        case USLOSS_TERM_INT :
            mbox = &termBoxes[unit];
            break;
    }

    if (debugflag2 && DEBUG2) {
        USLOSS_Console("waitDevice(): receiving from %d\n", mbox->mboxID);
    }

    //notify p1.c that there is another process waiting on a device, then receive/block
    addProcess();
    MboxReceive(mbox->mboxID, status, sizeof(long));
    releaseProcess();

    if (debugflag2 && DEBUG2) {
        USLOSS_Console("waitDevice(): received %s from mailbox %d\n", status, mbox->mboxID);
    }

    if (isZapped()) {
        return -1;
    }

    return 0;
}

static void nullsys(systemArgs *args) {
    USLOSS_Console("nullsys(): Invalid syscall %d. Halting...\n", args->number);
    USLOSS_Halt(1);
}

static void clockHandler2(int dev, void *arg) {
    long unit = (long) arg;
    int clockResult;

    // check if dispatcher should be called
    if (readCurStartTime() >= 80000) {
        timeSlice();
    }

    // inc that a clock interrupt happened
    clockTicks++;

    USLOSS_DeviceInput(dev, unit, &clockResult);

    
    // every fith interrupt do a conditional send to its mailbox
    if (clockTicks % 5 == 0) {

        if (debugflag2 && DEBUG2) {
            USLOSS_Console("clockHandler2: sending message %s to mbox %d\n", clockResult, clockBox.mboxID);
        }

        int sendResult = MboxCondSend(clockBox.mboxID, &clockResult, sizeof(clockResult));

        if (debugflag2 && DEBUG2) {
            USLOSS_Console("clockHandler2: send returned %d\n", sendResult);
            USLOSS_Halt(1);
        }
    }
}

static void diskHandler(int dev, void *arg) {
    long unit = (long) arg;
    int diskResult;

    // check for valid values
    if (dev != USLOSS_DISK_DEV || unit < 0 || unit > USLOSS_DISK_UNITS) {
        USLOSS_Console("diskHandler(): Bad values\n");
        USLOSS_Halt(1);
    }

    // make sure our box still exists
    if (diskBoxes[unit].mboxID == -1) {
        USLOSS_Console("Disk mailbox does not exist, unit = %d\n", unit);
        USLOSS_Halt(1); // might need to reutn instead
    }

    USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &diskResult);
    MboxCondSend(diskBoxes[unit].mboxID, &diskResult, sizeof(diskResult));
}

static void terminalHandler(int dev, void *arg) {

    long unit = (long) arg;

    if (debugflag2 && DEBUG2) {
        USLOSS_Console("terminalHandler(): dev = %d\n", dev);
        USLOSS_Console("terminalHandler(): unit = %d\n", unit);
    }
    int termResult;

    // check for valid values
    if (dev != USLOSS_TERM_DEV || unit < 0 || unit > USLOSS_TERM_UNITS) {
        USLOSS_Console("termHandler(): Bad values\n");
        USLOSS_Halt(1);
    }

    // make sure our box still exists
    if (termBoxes[unit].mboxID == -1) {
        USLOSS_Console("Term mailbox does not exist, unit\n");
        USLOSS_Halt(1); // might need to reutn instead
    }

    int result = USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &termResult);

    // if (debugflag2 && DEBUG2) {
    //     USLOSS_Console("terminalHandler(): sending now from dev %d to mbox %d value %s\n", dev, termBoxes[unit].mboxID, termResult);
    // }
    MboxCondSend(termBoxes[unit].mboxID, &termResult, sizeof(termResult));

    if (result != USLOSS_DEV_OK) {
        USLOSS_Console("termHandler(): USLOSS_DeviceInput is not ok.\n");
        USLOSS_Halt(1);
    }
}

static void syscallHandler(int dev, void *args) {
    // get args
    systemArgs *sysPtr = (systemArgs *) args;

    // check if valid dev
    if (dev != USLOSS_SYSCALL_INT) {
        USLOSS_Console("syscallHandler(): Bad call\n");
        USLOSS_Halt(1);
    }

    // check if valid range of args
    if (sysPtr->number < 0 || sysPtr->number >= MAXSYSCALLS) {
        USLOSS_Console("syscallHandler(): sys number %d is wrong.  Halting...\n", sysPtr->number);
        USLOSS_Halt(1);
    }
    
    USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
    sys_vec[sysPtr->number](sysPtr);
}

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
static void disableInterrupts()
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
 * Checks if we are in Kernel mode
 */
void check_kernel_mode(char *name) {
    if ((USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0) {
        USLOSS_Console("%s(): Called while in user mode by process %d. Halting...\n", name, getpid());
        USLOSS_Halt(1);
    }
}
