/*
 * This file contains the function definitions for the library interfaces
 * to the USLOSS system calls.
 */
#ifndef _LIBUSER_H
#define _LIBUSER_H

// Phase 3 -- User Function Prototypes
extern int  Spawn(char *name, int (*func)(char *), char *arg, int stack_size,
                  int priority, int *pid);
extern int  Wait(int *pid, int *status);
extern void Terminate(int status);
extern void GetTimeofDay(int *tod);
extern void CPUTime(int *cpu);
extern void GetPID(int *pid);
extern int  SemCreate(int value, int *semaphore);
extern int  SemP(int semaphore);
extern int  SemV(int semaphore);
extern int  SemFree(int semaphore);

// Phase 4 -- User Function Prototypes
extern int  Sleep(int seconds);
extern int  DiskRead(void *dbuff, int unit, int track, int first,
                     int sectors,int *status);
extern int  DiskWrite(void *dbuff, int unit, int track, int first,
                      int sectors,int *status);
extern int  DiskSize(int unit, int *sector, int *track, int *disk);
extern int  TermRead(char *buff, int bsize, int unit_id, int *nread);
extern int  TermWrite(char *buff, int bsize, int unit_id, int *nwrite);

// Interface to Phase 2 mailbox routines
// Currently commented out.  Not needed by phase3
// Needed by phase 5
// These are commented out -- the correct ones are
// in provided_prototypes.h
/*
extern int  Mbox_Create(int numslots, int slotsize, int *mboxID);
extern int  MboxRelease(int mbox);
extern int  MboxSend       (int mboxID, void *msgPtr, int msgSize);
extern int  MboxCondSend   (int mboxID, void *msgPtr, int msgSize);
extern int  MboxReceive    (int mboxID, void *msgPtr, int msgSize);
extern int  MboxCondReceive(int mboxID, void *msgPtr, int msgSize);
*/

// Phase 5 -- User Function Prototypes
extern void *VmInit(int mappings, int pages, int frames, int pagers);
extern void VmDestroy(void);

#endif
