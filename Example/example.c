/* example.c: this program illustrates a way to use the shared memory memory allocator
 *   Author: Charles E. Campbell, Jr.
 *   Date:   Oct 14, 2008
 */

/* =====================================================================
 * Header Section: {{{1
 */

/* ---------------------------------------------------------------------
 * Includes: {{{2
 */
#include <stdio.h>
#include <unistd.h>
#include "arena.h"
#include "ulocks.h"

/* ------------------------------------------------------------------------
 * Definitions: {{{2
 */

/* ------------------------------------------------------------------------
 * Typedefs: {{{2
 */
typedef struct UsInfo_str usinfo;

/* ------------------------------------------------------------------------
 * Local Data Structures: {{{2
 */
struct UsInfo_str {
	ulock_t lock;
	char   *data;
	int     proccount;
	};

/* ------------------------------------------------------------------------
 * Global Data: {{{2
 */
usptr_t *us_arena = NULL;

/* ------------------------------------------------------------------------
 * Explanation: {{{2
 */

/* ------------------------------------------------------------------------
 * Prototypes: {{{2
 */
int main( int, char **);                  /* example.c */
static void StartArena(char *);           /* example.c */
static void InitInfo(usptr_t *,usinfo *); /* example.c */
static void StopArena(usptr_t *);         /* example.c */

/* ========================================================================
 * Functions: {{{1
 */

/* --------------------------------------------------------------------- */
/* main: it all starts here! {{{2 */
int main(
  int    argc,
  char **argv)
{
printf("main(argc=%d,argv<%s %s...>)\n",
  argc,
  argv[0],
  (argc > 1)? argv[1] : "-missing-");
if(argc > 1) {
	StartArena(argv[1]);
	printf("sleeping for ten seconds\n");
	sleep(10);
	StopArena(us_arena);
	}

return 0;
}

/* --------------------------------------------------------------------- */
/* StartArena: this function starts the shared memory arena {{{2
 *             You may wish to have arena_name be somewhere
 *             readily accessable and temporary, such as /tmp/...
 *             or /var/tmp/...
 */
static void StartArena(char *arena_name)
{
int            done         = 0;
unsigned long  shm_memsize  = 800000000L;
unsigned long  expectshmfsz;
struct stat    arenastat;
usinfo        *info         = NULL;

printf("|StartArena()\n");

/* set up the us_arena */
expectshmfsz= usconfig(CONF_INITSIZE, shm_memsize); /* request arena size be shm_memsize            */
usconfig(CONF_INITUSERS,16);                        /* allow up to 16 users/locks                   */
usconfig(CONF_ATTACHADDR,0x40000000);               /* if this doesn't work for you, try 0x50000000 */
#ifndef __linux__
usconfig(CONF_CHMOD, arena, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP ); /* set permissions for multiple user access */
#endif
us_arena= usinit(arena_name);
if(!us_arena) {
	perror("(StartArena)");
	printf("|return StartArena: stat(<%s>) failure\n",arena_name);
	return;
   }

while(!done) {
	info= (usinfo *) usgetinfo(us_arena); /* attempt to join a pre-existing arena */

	if(info) { /* looks like we have a pre-existing arena */
        /* sanity check -- make sure mmap'd shared memory file size is what we're expecting! */
        if(stat(arena_name,&arenastat) == -1) {
            perror("(joinArenaUser)");
            printf("|return StartArena : stat(<%s>) failure\n",arena_name);
            return;
            }
        if(arenastat.st_size != expectshmfsz) {
			fprintf(stderr,"(StartArena) arena filesize is %lu bytes; expecting %lu bytes!\n",
              arenastat.st_size,
              expectshmfsz);
            printf("|return StartArena : arena filesize mismatch\n");
            return;
            }
        ussetlock(info->lock);   /* arena's info locked   */
        ++info->proccount;       /* arena's info locked   */
        usunsetlock(info->lock); /* arena's info unlocked */
		done= 1;
		printf("|proccount now %d\n",info->proccount);
		}

	else { /* create the info structure being put into the arena */
		info= uscalloc((size_t) 1,sizeof(usinfo),us_arena);
		if(!info) {
			perror("(StartArena-uscalloc)");
            printf("|return StartArena : uscalloc failure\n");
            return;
			}
		/* allocate the arena lock structure */
		info->lock= usnewlock(us_arena);
		if(!uscsetlock(info->lock,1)) {
			perror("(StartArena-uscsetlock)");
            printf("|return StartArena : uscsetlock failure\n");
			return;
			}
        info->proccount = 1;                          /* arena's info locked */
        done            = uscasinfo(us_arena,0,info); /* arena locked        */
        if(done) {                                    /* arena locked        */
            InitInfo(us_arena,info);                  /* arena locked        */
            usunsetlock(info->lock);                  /* unlocking arena     */
			printf("|unlocked arena's info->lock\n");
        	printf("|proccount now %d\n",info->proccount);
			}
		else { /* handling a possible race condition with some other process - try again */
			printf("|possible race condition, try again\n");
            usfreelock(info->lock,us_arena);         /* unlocking arena      */
            usfree(info,us_arena);
			}
		}
	} /* while not done loop */

printf("|return StartArena\n");
}

/* --------------------------------------------------------------------- */
/* InitInfo: this function initializes the info block, immediately common to all arena users {{{2 */
static void InitInfo(usptr_t *us_arena,usinfo *info)
{
printf("|InitInfo(%susarena,%sinfo)\n",
  us_arena? "" : "null ",
  info?    "" : "null ");

info->data= uscalloc((size_t) 1,(size_t) 100,us_arena);
sprintf(info->data,"Process#%d",info->proccount);

printf("||return InitInfo\n");
}

/* --------------------------------------------------------------------- */
/* StopArena: terminate process' attachment to the arena {{{2 */
static void StopArena(usptr_t *us_arena)
{
usinfo *info;

printf("|StopArena(%susarena)\n",us_arena? "" : "null ");

if(us_arena) {
	info= usgetinfo(us_arena);
	if(info) {
        ussetlock(info->lock);   /* arena locked    */
        --info->proccount;       /* arena locked    */
        usunsetlock(info->lock); /* unlocking arena */
		printf("||proccount now %d\n",info->proccount);
		if(info->proccount == 0) {
			printf("||free'ing us_arena\n");
			usfreearena(us_arena);
			us_arena = NULL;
			info    = NULL;
			}
		}
	}

printf("|return StopArena\n");
}

/* ===================================================================== */
/* Modelines: {{{1
 * vim: fdm=marker
 */
