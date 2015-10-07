#define DEBUG2 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mailSlot  mailSlot;
typedef struct mboxProc *mboxProcPtr;
typedef struct process process;

struct mailbox {
    int       mboxID;
    int       numSlots;
    int       numSlotsUsed;
    int       slotSize;
    slotPtr   headPtr;
    slotPtr   endPtr;
    int       blockStatus;
    // other items as needed...
};

struct mailSlot {
    int       mboxID;
    int       status;
    slotPtr   nextSlot;
    char      message[MAX_MESSAGE];
    int       size;
    // other items as needed...
};

struct process {
    int       pid;
    int       blockStatus;
    char      message[MAX_MESSAGE];
    int       size;
    int       mboxID;
    int       timeAdded;     // determines this process's 'spot in the line'
};

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psrValues {
    struct psrBits bits;
    unsigned int integerPart;
};

// for mailbox.blockStatus
#define NOT_BLOCKED     0
#define SENDBLOCK       11
#define RECEIVEBLOCK    12
