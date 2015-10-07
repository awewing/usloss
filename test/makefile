
TARGET = libphase2.a
ASSIGNMENT = 452phase2
CC = gcc
AR = ar

COBJS = phase2.o p1.o
CSRCS = ${COBJS:.o=.c}

# When using your phase1 library:
#PHASELIB = phase1

# When using one of Patrick's phase1 libraries:
#PHASE1LIB = patrickphase1debug
PHASE1LIB = patrickphase1

HDRS = message.h

INCLUDE = ./usloss/include 

CFLAGS = -Wall -g -std=gnu99 -I${INCLUDE} -I.

UNAME := $(shell uname -s)

ifeq ($(UNAME), Darwin)
	CFLAGS += -D_XOPEN_SOURCE
endif

LDFLAGS = -L. -L./usloss/lib

PHASE2 = /home/cs452/fall15/phase2

ifeq ($(PHASE2), $(wildcard $(PHASE2)))
	LDFLAGS += -L$(PHASE2)
endif

TESTDIR=testcases
TESTS = test00 test01 test02 test03 test04 test05 test06 test07 test08 \
        test09 test10 test11 test12 test13 test14 test15 test16 test17 \
        test18 test19 test20 test21 test22 test23 test24 test25 test26 \
        test27 test28 test29 test30 test31 test32 test33 test34 test35 \
        test36 test37 test38 test39 test40 test41 test42 test43 test44

LIBS = -l$(PHASE1LIB) -lphase2 -lusloss

$(TARGET):	$(COBJS)
		$(AR) -r $@ $(COBJS) 

$(TESTS):	$(TARGET)
	$(CC) $(CFLAGS) -I. -c $(TESTDIR)/$@.c
	$(CC) $(LDFLAGS) -o $@ $@.o $(LIBS)

clean:
	rm -f $(COBJS) $(TARGET) test??.o test?? core term*.out

phase2.o:	message.h

submit: $(CSRCS) $(HDRS) Makefile
	tar cvzf phase2.tgz $(CSRCS) $(HDRS) Makefile
