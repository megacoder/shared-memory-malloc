/* usarena.c: this program mimics some of SGI's usinit - usmalloc etc
 * functionality.  It allocates a shared memory usarena from which routines
 * herein can allocate via usmalloc uscalloc usrealloc usfree etc.
 *
 * Author:  Charles E. Campbell, Jr.
 * Date:    Oct 19, 2005
 */

/* =====================================================================
 * Header Section: {{{1
 */

/* ---------------------------------------------------------------------
 * Includes: {{{2
 */
#define USINTERNAL
#include "arena.h"

/* ---------------------------------------------------------------------
 * Definitions: {{{2
 */
#define EAGAINMAX   10
#define SHMSTART    0x000
#if !defined(LOCK_EX)
# define	LOCK_SH		0x01		/* shared file lock */
# define	LOCK_EX		0x02		/* exclusive file lock */
# define	LOCK_NB		0x04		/* don't block when locking */
# define	LOCK_UN		0x08		/* unlock file */
#endif

/* stralloc: allocates new memory for and copies a string into the new mem */
#define stralloc(ptr,string,fail_msg) {                                  \
	ptr= (char *) calloc((size_t) strlen(string) + 1,sizeof(char));      \
	if(!ptr) fprintf(stderr,"%s: <%s>\n",fail_msg,string);               \
    else     strcpy(ptr,string);                                         \
	}

/* ------------------------------------------------------------------------
 * Data: {{{2
 */
USArena *usarena= NULL;

/* ------------------------------------------------------------------------
 * Prototypes: {{{2
 */
static void userror(usptr_t *,int,int); /* usarena.c */

/* ========================================================================
 * Functions: {{{1
 */

/* --------------------------------------------------------------------- */
/* usconfig: this function {{{2 */
ptrdiff_t usconfig(int cmd,...)
{
va_list   args;
ptrdiff_t ret          = -1;
usoffset  user_memsize;
usoffset  arena_memsize;


if(!usarena) { /* initialize and allocate default usarena configuration */
    /* default values = CONF_INITIALIZE */
    usarena             = (USArena *) calloc((size_t) 1,sizeof(USArena));
    usarena->filename   = NULL;
    usarena->memsize    = (size_t) 65536L + sizeof(USArenaShare);
    usarena->maxusers   = 8;
    usarena->permission = S_IRUSR|S_IWUSR|S_IXUSR; /* by default, only the user will have read+write+exe permission */
    usarena->mempool    = NULL;
    usarena->memattach  = NULL;
    }

/* sanity check */
if(!usarena) {
    return (ptrdiff_t) NULL;
    }

/* perform requested command */
switch(cmd) {
case CONF_INITIALIZE:   /* CONF_INITIALIZE                                                                     */
    /* does nothing, really -- initialization already done above */
    break;

case CONF_INITSIZE:     /* CONF_INITSIZE,segmentsize    -- in bytes) (default=65536)          --               */
    /* Each usarena mmap'd mempool will also have a copy of the USArenaShare */
    va_start(args,cmd);
    user_memsize     = (va_arg(args,size_t) + 7)&(~0x7);
    arena_memsize    = (sizeof(USArenaShare) + 7)&(~0x7);
    usarena->memsize = (user_memsize + arena_memsize + 8)&(~0x7);
    va_end(args);
    ret= usarena->memsize;
    break;

case CONF_INITUSERS:    /* CONF_INITUSERS,maxusers      -- qty semaphores & locks (default=8) --               */
    ret= usarena->maxusers;
    va_start(args,cmd);
    usarena->maxusers= va_arg(args,int);
    va_end(args);
    if(usarena->maxusers <= 0) usarena->maxusers= 1; /* gotta have one semaphore */
    break;

case CONF_GETSIZE:      /* CONF_GETSIZE                 -- returns usarena size in bytes      --               */
    ret= (ptrdiff_t) usarena->memsize;
    break;

case CONF_GETUSERS:     /* CONF_GETUSERS                -- returns qty users                  --               */
    ret= (ptrdiff_t) usarena->maxusers;
    break;

case CONF_LOCKTYPE:     /* CONF_LOCKTYPE,locktype       --                                    -- not supported */
    break;

case CONF_ARENATYPE:    /* CONF_ARENATYPE,US_SHAREDONLY -- no memory map file                 --               */
    break;

case CONF_CHMOD:        /* CONF_CHMOD,permission        -- for usarena&lock files             --               */
    ret= usarena->permission;
    va_start(args,cmd);
    usarena->permission= va_arg(args,unsigned int);
    va_end(args);
    break;

case CONF_ATTACHADDR:   /* CONF_ATTACHADDR,address      --                                    --               */
    ret= (ptrdiff_t) usarena->memattach;
    va_start(args,cmd);
    usarena->memattach= va_arg(args,void *);
    va_end(args);
    break;

case CONF_AUTOGROW:     /* CONF_AUTOGROW,int            --                                    -- not supported */
    break;

case CONF_AUTORESV:     /* CONF_AUTORESV,int            --                                    -- not supported */
    break;

case CONF_HISTON:       /* CONF_HISTON,usptr_t*         -- enables semaphore history logging  -- not supported */
    break;

case CONF_HISTOFF:      /* CONF_HISTOFF,usptr_t*        -- disables semaphore history logging -- not supported */
    break;

case CONF_HISTSIZE:     /* CONF_HISTSIZE,int            -- maxqty of history records          -- not supported */
    break;

case CONF_HISTFETCH:    /* CONF_HISTFETCH,usptr_t*      --                                    -- not supported */
    break;

case CONF_HISTRESET:    /* CONF_HISTRESET,usptr_t*      --                                    -- not supported */
    break;

case CONF_STHREADIOOFF: /* CONF_STHREADIOOFF            --                                    -- not supported */
    break;

case CONF_STHREADIOON:  /* CONF_STHREADIOON             --                                    -- not supported */
    break;

default:
    break;
    }

return ret;
}

/* --------------------------------------------------------------------- */
/* usinit: initializes a shared usarena from which related or unrelated {{{2
 * processes may share and allocate memory, semaphores, and locks.
 * Use usconfig() to specify initial configuration.
 */
usptr_t *usinit(const char *filename)
{
int          ibin;
int          fd;
size_t       size;
struct stat  filestat;
USArenaShare arenashare;
usoffset     memsize;
usoffset     minsize;
usoffset     ichunk;
usoffset     zero;


/* sanity checks */
if(!filename) {
    return NULL;
    }

if(!usarena) { /* assume we're attempting to join a pre-existing arena */
    usarena= (USArena *) calloc((size_t) 1,sizeof(USArena));
    stralloc(usarena->filename,filename,"usinit arena filename");
    }

if(usarena->filename && strcmp(usarena->filename,filename)) {
    free(usarena->filename);
    stralloc(usarena->filename,filename,"(usinit) filename");
    }
else if(!usarena->filename) {
    stralloc(usarena->filename,filename,"(usinit) filename");
    }
if(!usarena->filename) {
    return NULL;
    }

/***********************************************************
 * Determine if the requested usarena file already exists: *
 *   If the usarena file exists, join.                     *
 *   If not, create.                                       *
 ***********************************************************/
if(stat(usarena->filename,&filestat)) { /* usarena file does not exist. Create a new usarena */

    /* check on mempool size */
    minsize = ((sizeof(USArenaShare)+8)&(~0x7)) + 8;
    size    = usarena->memsize;
    if(size < minsize) size= minsize;

    fd= open(usarena->filename,O_CREAT|O_RDWR|O_EXCL,usarena->permission);
    if(fd == -1) {
        userror(usarena,fd,1);
        return NULL;
        }

    /* place an advisory lock on the opened file */
    if(flock(fd,LOCK_EX)) {
        userror(usarena,fd,2);
        return NULL;
        }

    /* go to last-byte-in-buffer/file and write a byte - an arcane thing needed to get that file defined */
    if(lseek(fd,size-1,SEEK_SET) == -1) {
        userror(usarena,fd,2);
        return NULL;
        }
    if(write(fd,"",1) != 1) {
        userror(usarena,fd,3);
        return NULL;
        }

    /* mmap the file */
    usarena->mempool= mmap(usarena->memattach,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t) 0);
    if(usarena->mempool == MAP_FAILED) {
        userror(usarena,fd,3);
        return NULL;
        }
    usarena->memsize= size;

    /* semaphores: get an IPC key */
    usarena->key= ftok(usarena->filename,0);
    if(usarena->key == -1) {
        userror(usarena,fd,4);
        return NULL;
        }

    /* semaphores: allocate and initialize
     *  I wish the name was "maxlocks" rather than "maxusers", but its there for
     *  compatibility.  There will be maxusers+1 locks (semaphores) allocated
     *  and initialized; the last one will be used by the usmalloc-uscalloc-usfree
     *  routines.
     */
    if(usarena->maxusers >= 0) {
        union semun {
            int val;
            struct semid_ds *buf;
            ushort          *array;
#ifdef __gnu_linux__
            struct seminfo *__buf;
#endif
            } semset;
        int iarray;

        /* attempt to create a new semaphore set */
        usarena->semid= semget(usarena->key,usarena->maxusers+1,IPC_CREAT|IPC_EXCL|usarena->permission);
        if(usarena->semid == -1) {
            /* semaphore set associated with key already exists, but the file didn't.
             * So, remove the semaphore set and then get a new one.  Creates a semaphore
             * set with "maxusers" semaphores.
             */
            perror("(usinit warning) semget() error!");
            usarena->semid= semget(usarena->key,usarena->maxusers+1,IPC_CREAT|usarena->permission);
            if(usarena->semid == -1) {
                userror(usarena,fd,4);
                return NULL;
                }
            if(semctl(usarena->semid,0,IPC_RMID,0) != -1) {
                usarena->semid= semget(usarena->key,usarena->maxusers+1,IPC_CREAT|IPC_EXCL|usarena->permission);
                }
            if(usarena->semid == -1) {
                userror(usarena,fd,4);
                return NULL;
                }
            }

        /* set all semaphores to US_SEMUNUSED except for the last one */
        semset.array= (ushort *) calloc((size_t) usarena->maxusers+1,sizeof(ushort));
        if(!semset.array) {
            userror(usarena,fd,5);
            return NULL;
            }
        for(iarray= 0; iarray < usarena->maxusers; ++iarray) semset.array[iarray]= US_SEMUNUSED;
        semset.array[usarena->maxusers]= 0;

        if(semctl(usarena->semid,0,SETALL,semset) == -1) {
            free(semset.array);
            userror(usarena,fd,5);
            return NULL;
            }
        free(semset.array);
        }

    /* The shared memory pool:
     *                    +-------------------------------+
     *  usarena->mempool->|USArenaShare                   |
     *  usarena->base   ->|memory available for allocation|
     *                    +-------------------------------+
     *  Instead of pointers, I'm using offsets from the base
     *  in the chunks.  Different processes have the file
     *  mmap'd to different addresses, so I can't rely on
     *  pointers.  Each process will have its own USArena
     *  base from which the offsets apply to the allocation
     *  memory.
     *
     *  Smallest chunksize is 8 bytes.
     *  Add sizeof(USArenaShare) bytes to hold free bin table.
     *  First 8 bytes reserved to allow a chunk#0 to be "illegal"
     *  usarena begins with an ArenaShare, so the free-bin table has offset for key,memsize,maxusers
     */
    memsize       = (sizeof(USArenaShare) + 7)&(~0x3);
    usarena->bin  = (USFreeBin *) (usarena->mempool + arena_bin_offset);
    usarena->base = usarena->mempool + memsize; /* beginning of allocatable memory */
    usarena->info = 0;

    /* initialize usarena USFreeBins  - first 8 bytes are wasted so ichunk=0 can be used as
     * not-a-chunk.  Done by marking those 8 bytes as "inuse".
     */
    for(ibin= 0; ibin < USMAXFREEBIN; ++ibin) usarena->bin[ibin].hd= usarena->bin[ibin].tl= 0;
    zero                  = 0;
    memsize               = usarena->memsize - memsize - (usoffset) 8;
    ibin                  = ushashsize(memsize);
    ichunk                = 8;
    usarena->bin[ibin].hd = usarena->bin[ibin].tl= ichunk;
    setnxtchunk(ichunk,zero);
    setprvchunk(ichunk,zero);
    setsize(ichunk,memsize);
    setfree(ichunk);
    usarena->memsize= memsize + (usoffset) 8;

    /* initialize USArenaShare */
    arenashare.memattach= usarena->mempool;
    arenashare.key      = usarena->key;
    arenashare.memsize  = usarena->memsize;
    arenashare.maxusers = usarena->maxusers;
    arenashare.info     = 0;
    memcpy(arenashare.bin,usarena->bin,USMAXFREEBIN*sizeof(USFreeBin));

    /* copy USArenaShare to beginning of mmap'd memory pool */
    memcpy(usarena->mempool,&arenashare,sizeof(USArenaShare));

    /* unlock the advisory lock */
    flock(fd,LOCK_UN);
    }

else { /* usarena exists -- join it */
    if(usadd(usarena) == -1) {
        return NULL;
        }
    }

return usarena;
}

/* --------------------------------------------------------------------- */
/* userror: this function handles error cleanup {{{2
 *   Called by usinit() and usadd()
 */
static void userror(usptr_t *usarena,int fd,int progress)
{
int errnokeep;

errnokeep= errno;
if(progress > 0) {
    perror("(usinit)");
    if(progress >= 5) semctl(usarena->semid,0,IPC_RMID,0);
    if(progress >= 4) munmap(usarena->mempool,usarena->memsize);
    if(progress >= 3) flock(fd,LOCK_UN);
    if(progress >= 2) close(fd);
    if(progress >= 1) unlink(usarena->filename);
    }
else if(progress < 0) {
    perror("(usadd)");
    if(progress <= -3) flock(fd,LOCK_UN);
    if(progress <= -2) close(fd);
    }
}

/* --------------------------------------------------------------------- */
/* usadd: this function allows processes to "add" themselves as users of an usarena {{{2
 *   Returns: -1 failure
 *             0 success
 */
int usadd(usptr_t *usarena)
{
void        *memattach= NULL;
int          fd;
size_t       size;
struct stat  filestat;
USArenaShare arenashare;
usoffset     memsize;


fd= open(usarena->filename,O_CREAT|O_RDWR,usarena->permission);
if(fd == -1) {
    userror(usarena,fd,-1);
    return -1;
    }

/* place an advisory lock on the file first thing */
if(flock(fd,LOCK_EX)) {
    userror(usarena,fd,-2);
    return -1;
    }

/* set up attach point to keep pointers consistent, unless user has specified an attach point */
if(!usarena->memattach) { /* originating process has stored its attach point as first bytes in file */
    if(!read(fd,&memattach,sizeof(memattach))) {
        userror(usarena,fd,-3);
        return -1;
        }
    }
else {
    memattach= usarena->memattach;
    }

/* do memory mapping */
if(stat(usarena->filename,&filestat)) { /* usarena file does not exist. can't add on to it! */
    userror(usarena,fd,-3);
    return -1;
    }
size= filestat.st_size;
usarena->mempool = mmap(memattach,size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,(off_t) 0);
if(usarena->mempool == MAP_FAILED) {
    userror(usarena,fd,-3);
    return -1;
    }

/* copy bytes into USArenaShare and initialize usarena */
memcpy(&arenashare,usarena->mempool,sizeof(USArenaShare));
usarena->key      = arenashare.key;
usarena->memsize  = arenashare.memsize;
usarena->maxusers = arenashare.maxusers;
usarena->info     = arenashare.info;
memsize           = (sizeof(USArenaShare) + 7)&(~0x3);
usarena->base     = usarena->mempool + memsize;
usarena->bin      = (USFreeBin *) (usarena->mempool + arena_bin_offset);

/* semaphores: obtain access - do a semget() */
if(usarena->maxusers > 0) {
    usarena->semid= semget(arenashare.key,usarena->maxusers,IPC_CREAT|usarena->permission);
    if(usarena->semid == -1) {
        userror(usarena,fd,-3);
        return -1;
        }
    }

/* unlock the advisory lock */
flock(fd,LOCK_UN);

return 0;
}

/* --------------------------------------------------------------------- */
/* usarenalockinit: this function initializes the usarena semaphore {{{2
 * to zero (ie. unlocked but ready for business).
 */
int usarenalockinit(usptr_t *usarena)
{
int ret= -1;
union semun {
    int val;
    struct semid_ds *buf;
    ushort          *array;
#ifdef __gnu_linux__
    struct seminfo *__buf;
#endif
    } semun;


if(usarena && usarena->semid != -1 && usarena->maxusers >= 0) {
    semun.val = 0;
    ret       = semctl(usarena->semid,usarena->maxusers,SETVAL,semun);
    }

return ret;
}

/* --------------------------------------------------------------------- */
/* usarenalock: this function locks the USArenaShare portion of the usarena {{{2
 *   No sanity checks taken to facilitate speed
 */
int usarenalock(usptr_t *usarena)
{
int           eagaincnt = 0;
int           ret;
struct sembuf sops;


sops.sem_flg= SEM_UNDO;          /* blocking and will leave semaphore available if process dies */
sops.sem_num= usarena->maxusers; /* select semaphore by number                                  */
sops.sem_op = 0;                 /* block until semaphore goes to zero                          */
do {
    errno = 0;
    ret   = semop(usarena->semid,&sops,1);
    if(errno == EAGAIN && ++eagaincnt > EAGAINMAX) break;
    } while(ret == -1 && (errno == EINTR || errno == EAGAIN));


return ret;
}

/* --------------------------------------------------------------------- */
/* usarenaunlock: this function unlocks the USArenaShare portion of the usarena {{{2 */
int usarenaunlock(usptr_t *usarena)
{
int ret;
union semun {
    int              val;
    struct semid_ds *buf;
    ushort          *array;
#ifdef __gnu_linux__
    struct seminfo  *__buf;
#endif
    } semun;


/* set semaphore to zero.  This call will not block even if
 * the semaphore already is zero.
 */
semun.val = 0;
ret       = semctl(usarena->semid,usarena->maxusers,SETVAL,semun);


return ret;
}

/* --------------------------------------------------------------------- */
/* usfreearena: this function free's an arena, un-mmaps it, and {{{2
 * releases associated semaphores.
 */
void usfreearena(usptr_t *usarena)
{
int ret= -1;


if(usarena) {
    if(usarena->mempool && usarena->memsize > 0) {
        ret= munmap(usarena->mempool,usarena->memsize);
        }
    if(usarena->semid >= 0) {
        ret= semctl(usarena->semid,0,IPC_RMID,0);
        }
    }

}


/* =====================================================================
 * Modelines: {{{1
 * vim: sw=4 sts=4 et fdm=marker ts=4
 */
