
#include <stdio.h>
#include <usloss.h>
#include <phase1.h>

int XXp1(char *);
int XXp2(char *);
int XXp3(char *);

int start1(char *arg)
{
  int pid1, kid_status;

  printf("start1(): started\n");

  pid1 = fork1("XXp1", XXp1, "XXp1", USLOSS_MIN_STACK, 5);
  pid1 = join(&kid_status);
  printf("XXp1 done; returning...\n");
  return 0; /* so gcc will not complain about its absence... */
} /* start1 */

int XXp1(char *arg)
{
  int i, pid[10], result[10];

  printf("XXp1(): creating children\n");
  for (i = 0; i < 3; i++)
    pid[i] = fork1("XXp2", XXp2, "XXp2", USLOSS_MIN_STACK, 3);

  printf("XXp1(): creating zapper child\n");
  pid[i] = fork1("XXp3", XXp3, "XXp3", USLOSS_MIN_STACK, 4);

  printf("XXp1(): unblocking children\n");
  for (i = 0; i < 3; i++)
    result[i] = unblockProc(pid[i]);

  for (i = 0; i < 3; i++)
    printf("XXp1(): after unblocking %d, result = %d\n", pid[i], result[i]);

  quit(0);
  return 0;
} /* XXp1 */


int XXp2(char *arg)
{
  int result;

  printf("XXp2(): started, pid = %d, calling blockMe\n", getpid());
  result = blockMe(11);
  printf("XXp2(): pid = %d, after blockMe, result = %d\n", getpid(), result);
  printf("XXp2(): pid = %d, isZapped() = %d\n", getpid(), isZapped());
  quit(0);
  return 0;
} /* XXp2 */

int XXp3(char *arg)
{
  int result;

  printf("XXp3(): started, pid = %d, calling zap on pid 5\n", getpid());
  result = zap(5);
  printf("XXp3(): after call to zap, result of zap = %d\n", result);
  return 0;
} /* XXp3 */
