/* ulocks.h: 
 *   Author: Charles E. Campbell, Jr.
 *   Date:   Dec  5, 2005
 */
#ifndef __ULOCKS_H__
# define __ULOCKS_H__
# ifndef __USE_SVID
#  define __USE_SVID
# endif
# include <sys/sem.h>
# include <errno.h>

/* ---------------------------------------------------------------------
 * Enumerations: {{{1
 */
enum SEM_enum {
    SEMAVAIL = 0, /* semaphore -> shared memory page is available for use */
    SEMLOCK  = 1  /* semaphore -> shared memory page is locked            */
	};

/* ---------------------------------------------------------------------
 * Typedefs: {{{1
 */
typedef struct USLock_str  USLock;
typedef USLock            *ulock_t;
#ifdef __linux__
typedef unsigned short     ushort;
#endif

/* ---------------------------------------------------------------------
 * Structures: {{{1
 */
struct USLock_str {
	unsigned lock;
	unsigned semid;
	unsigned maxusers;
	};

/* ---------------------------------------------------------------------
 * Prototypes: {{{1
 */
ulock_t usnewlock(usptr_t *);         /* ulocks.c */
void usfreelock( ulock_t, usptr_t *); /* ulocks.c */
int ussetlock(ulock_t);               /* ulocks.c */
int uscsetlock( ulock_t, unsigned);   /* ulocks.c */
int uswsetlock(ulock_t,unsigned);     /* ulocks.c */
int ustestlock(ulock_t);              /* ulocks.c */
int usunsetlock(ulock_t);             /* ulocks.c */
#endif  /* __ULOCKS_H__ */

/* ---------------------------------------------------------------------
 * Modelines: {{{1
 *  fdm=marker
 */
