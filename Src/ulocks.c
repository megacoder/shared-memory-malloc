/* ulock.c: this program implements locks via semaphores for the usarena
 *   Author: Charles E. Campbell, Jr.
 *   Date:   Dec  6, 2005
 */

/* =====================================================================
 * Header Section: {{{1
 */

/* ---------------------------------------------------------------------
 * Includes: {{{2
 */
#define XSEM_H
#include <stdio.h>
#include "arena.h"
#include "ulocks.h"

/* ------------------------------------------------------------------------
 * Definitions: {{{2
 */
#define EAGAINMAX   10

/* =====================================================================
 * Functions: {{{1
 */

/* --------------------------------------------------------------------- */
/* usnewlock: this function allocates a lock from the usarena and {{{2
 * initializes it.  Locks are semaphore based.
 */
ulock_t usnewlock(usptr_t *usarena)
{
int     iarray;
int     ret;
ulock_t lock   = NULL;
union semun {
    int              val;
    struct semid_ds *buf;
    ushort          *array;
#ifdef __gnu_linux__
    struct seminfo  *__buf;
#endif
    } semun;


if(!usarena) {
    return NULL;
    }

/* find an unused semaphore.  set its value to zero */
semun.array = (ushort *) calloc((size_t) usarena->maxusers+1,sizeof(ushort));
ret         = semctl(usarena->semid,0,GETALL,semun);
if(ret < 0) {
    if(semun.array) free(semun.array);
    return NULL;
    }
for(iarray= 0; iarray < usarena->maxusers; ++iarray) {
    if(semun.array[iarray] == US_SEMUNUSED) {
        lock= (ulock_t) usmalloc(sizeof(USLock),usarena);
        usmemdesc(lock,"lock");
        if(!lock) {
            errno= ENOMEM;
            break;
            }
        free(semun.array);
        lock->lock     = iarray;
        lock->semid    = usarena->semid;
        lock->maxusers = usarena->maxusers;
        semun.val      = 0;
        ret            = semctl(usarena->semid,lock->lock,SETVAL,semun);
        if(ret < 0) {
            int keeperrno;
            keeperrno= errno;
            free(lock);
            lock  = NULL;
            errno = keeperrno;
            return NULL;
            }
        break;
        }
    }
if(iarray >= usarena->maxusers) {
    return NULL;
    }

return lock;
}

/* --------------------------------------------------------------------- */
/* usfreelock: this function frees all memory associated with the {{{2
 * specified lock.  Potential trouble will occur if the lock
 * is not a valid lock.  Null locks are ignored.  Does not release
 * the semaphore set associated with the usarena.
 */
void usfreelock(
  ulock_t  lock,  
  usptr_t *usarena)
{
int   ret;
union semun {
    int val;
    struct semid_ds *buf;
    ushort          *array;
#ifdef __gnu_linux__
    struct seminfo *__buf;
#endif
    } semun;


/* sanity checks */
if(!lock) {
    return;
    }
if(!usarena) {
    return;
    }
if(lock->lock < 0 || usarena->maxusers < lock->lock) {
    return;
    }

/* set semaphore to indicate that its unused.  If any processes were
 * waiting for this semaphore to go to zero...
 */
semun.val = US_SEMUNUSED;
ret        = semctl(usarena->semid,lock->lock,SETVAL,semun);
usfree(lock,usarena);

}

/* --------------------------------------------------------------------- */
/* ussetlock: this function atomically tests&sets a semaphore lock {{{2 */
int ussetlock(ulock_t lock)
{
int           eagaincnt= 0;
int           ret;
struct sembuf sops;


/* sanity checks */
if(!lock) {
    return -1;
    }
if(lock->lock < 0 || lock->maxusers < lock->lock) {
    return -1;
    }

sops.sem_flg= SEM_UNDO;   /* blocking and will leave semaphore available if process dies */
sops.sem_num= lock->lock; /* select semaphore by number                                  */
sops.sem_op = 0;          /* block until semaphore goes to zero                          */
do {                      /* ignore interrupts                                           */
    errno = 0;
    ret   = semop(lock->semid,&sops,1);
    if(errno == EAGAIN && ++eagaincnt > EAGAINMAX) break;
    } while(ret == -1 && (errno == EINTR || errno == EAGAIN));


return 0;
}

/* --------------------------------------------------------------------- */
/* uscsetlock: this function checks if the lock can be set with no wait {{{2
 *   Returns:  1=lock acquired  0=lock not acquired
 */
int uscsetlock(
  ulock_t  lock, 
  unsigned spins) /* spins=0: blocks until lock acquired  =1: no blocking */
{
int           eagaincnt= 0;
int           ret;
struct sembuf sops;


/* sanity checks */
if(!lock) {
    return -1;
    }
if(lock->semid < 0) {
    return -1;
    }
if(lock->lock < 0 || lock->maxusers < lock->lock) {
    return -1;
    }

sops.sem_num = lock->lock; /* select semaphore by number       */
sops.sem_op  = 0;          /* test if semaphore may go to zero */
if(spins > 0) {
    sops.sem_flg = SEM_UNDO|IPC_NOWAIT;
    ret          = semop(lock->semid,&sops,(unsigned)1);
    }
else {
    errno= 0;
    do { /* block until semaphore reaches zero.  Ignore interrupts. */
        sops.sem_flg = SEM_UNDO;
        ret          = semop(lock->semid,&sops,(unsigned)1);
        if(errno == EAGAIN && ++eagaincnt > EAGAINMAX) break;
        } while(ret == -1 && (errno == EINTR || errno == EAGAIN));
    }
if(ret < 0 && errno == EAGAIN) ret= 0;
else                           ret= 1;

return ret;
}

/* --------------------------------------------------------------------- */
/* uswsetlock: this function for a single-processor linux is the same as ussetlock {{{2 */
int uswsetlock(ulock_t lock,unsigned spins)
{
int ret;


ret= ussetlock(lock);

return ret;
}

/* --------------------------------------------------------------------- */
/* ustestlock: this function returns the instantaneous value of a lock {{{2 */
int ustestlock(ulock_t lock)
{
int ret;


/* sanity checks */
if(!lock) {
    return -1;
    }
if(lock->lock < 0 || lock->maxusers < lock->lock) {
    return -1;
    }

ret= semctl(lock->semid,lock->lock,GETVAL,0);

return ret;
}

/* --------------------------------------------------------------------- */
/* usunsetlock: this function releases a lock (ie. sets it to zero) {{{2 */
int usunsetlock(ulock_t lock)
{
int ret= -1;
union semun {
    int              val;
    struct semid_ds *buf;
    ushort          *array;
#ifdef __gnu_linux__
    struct seminfo  *__buf;
#endif
    } semun;


/* sanity checks */
if(!lock) {
    return -1;
    }
if(lock->lock < 0 || lock->maxusers < lock->lock) {
    return -1;
    }

/* set semaphore to zero.  This call will not block even if
 * the semaphore already is zero.
 */
semun.val = 0;
ret       = semctl(lock->semid,lock->lock,SETVAL,semun);


return ret;
}

/* =====================================================================
 * Modelines: {{{1
 * vim: sw=4 sts=4 et fdm=marker ts=4
 */
