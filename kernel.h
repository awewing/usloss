/* Patrick's DEBUG printing constant... */
#define DEBUG 0

typedef struct procStruct procStruct;

typedef struct procStruct * procPtr;

typedef struct quitNode quitNode;

typedef struct quitNode * quitNodePtr;

struct procStruct {
   procPtr         nextProcPtr;       /* readyList next */
   procPtr         childProcPtr;
   procPtr         nextSiblingPtr;
   char            name[MAXNAME];     /* process's name */
   char            startArg[MAXARG];  /* args passed to process */
   USLOSS_Context  state;             /* current context for process */
   short           pid;               /* process id */
   short           ppid;              /* parent process id */
   int             priority;
   int (* start_func) (char *);       /* function where process begins -- launch */
   char           *stack;
   unsigned int    stackSize;
   int             status;            /* READY, BLOCKED, QUIT, etc. */
   int             startTime;
   int             zapped;          /* if the process was zapped */
   int             zappedWhileBlocked;/*if the process was zapped while it was blocked */
   int             kids;
   procPtr         zapList[MAXPROC];
   quitNodePtr     quitList;         /* used to comunicate between quit child and parent*/
   /* other fields as needed... */
};

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psr_values {
   struct psrBits bits;
   unsigned int integerPart;
};

struct quitNode {
   quitNodePtr     nextNode;
   int             quitCode;
   short           pid;
};

/* Some useful constants.  Add more as needed... */
#define NO_CURRENT_PROCESS NULL
#define MINPRIORITY 5
#define MAXPRIORITY 1
#define SENTINELPID 1
#define SENTINELPRIORITY (MINPRIORITY + 1)
#define EMPTY -1
#define NOPARENT -2
#define READY 0
#define QUIT 1
#define JOIN_BLOCK 2
#define ZAP_BLOCK 3
#define TIMESLICE 80000
