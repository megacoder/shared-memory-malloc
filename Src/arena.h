/* arena.h: header file for usarena functions
 * Author:	Charles E. Campbell, Jr.
 * Date:	Oct 26, 2005
 */

/* ---------------------------------------------------------------------
 * Includes: {{{1
 */
#ifndef __USARENA_H__
# define __USARENA_H__
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <string.h>
# include <stdarg.h>
# include <fcntl.h>
# include <unistd.h>
# include <sys/file.h>
# include <sys/ipc.h>
# include <sys/sem.h>
# include <sys/mman.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <errno.h>

/* ------------------------------------------------------------------------
 * Typedefs: {{{1
 */
typedef struct USArena_str      USArena;       /* cec preferred format    */
typedef struct USArena_str      usptr_t;       /* forced by compatibility */
typedef struct USArenaShare_str USArenaShare;
typedef struct USFreeBin_str    USFreeBin;
typedef unsigned long           usoffset;
typedef unsigned char           usbase;

#if defined(__gnu_linux__) && !defined(_PTRDIFF_T_DEFINED)
# define NEED_PTRDIFF_T
#endif
#ifdef NEED_PTRDIFF_T
# ifdef __x86_64
typedef long int ptrdiff_t;
# else
typedef int ptrdiff_t;
# endif
#endif

/* ------------------------------------------------------------------------
 * Definitions: {{{1
 */
# define CONF_INITIALIZE   0  /* CONF_INITIALIZE              -- set up default arena               -- new command   */
# define CONF_INITSIZE     1  /* CONF_INITSIZE,segmentsize    -- in bytes) (default=65536)          --               */
# define CONF_INITUSERS    2  /* CONF_INITUSERS,maxusers      -- qty semaphores & locks (default=8) --               */
# define CONF_GETSIZE      3  /* CONF_GETSIZE                 -- returns arena size in bytes        --               */
# define CONF_GETUSERS     4  /* CONF_GETUSERS                -- returns qty users                  --               */
# define CONF_LOCKTYPE     5  /* CONF_LOCKTYPE,locktype       --                                    -- not supported */
# define CONF_ARENATYPE    6  /* CONF_ARENATYPE,US_SHAREDONLY -- no memory map file                 --               */
# define CONF_CHMOD        7  /* CONF_CHMOD,permission        -- for arena&lock files               --               */
# define CONF_ATTACHADDR   8  /* CONF_ATTACHADDR,address      --                                    --               */
# define CONF_AUTOGROW     9  /* CONF_AUTOGROW,int            --                                    -- not supported */
# define CONF_AUTORESV     10 /* CONF_AUTORESV,int            --                                    -- not supported */
# define CONF_HISTON       11 /* CONF_HISTON,usptr_t*         -- enables semaphore history logging  -- not supported */
# define CONF_HISTOFF      12 /* CONF_HISTOFF,usptr_t*        -- disables semaphore history logging -- not supported */
# define CONF_HISTSIZE     13 /* CONF_HISTSIZE,int            -- maxqty of history records          -- not supported */
# define CONF_HISTFETCH    14 /* CONF_HISTFETCH,usptr_t*      --                                    -- not supported */
# define CONF_HISTRESET    15 /* CONF_HISTRESET,usptr_t*      --                                    -- not supported */
# define CONF_STHREADIOOFF 16 /* CONF_STHREADIOOFF            --                                    -- not supported */
# define CONF_STHREADIOON  17 /* CONF_STHREADIOON             --                                    -- not supported */

# define USMAXFREEBIN     156 /* free chunks are in bins [0,155]                                                     */
# define MINCHUNKSIZE	   16 /* free chunk overhead == 16 bytes                                                     */
# define MAXCHUNKSIZE	  sizeof(unsigned long)

# ifdef SEMVMX
#  define US_SEMUNUSED	  SEMVMX
# else
#  define US_SEMUNUSED	  (((ushort) ~0)>>1)
# endif

# ifdef USINTERNAL
#  define getsizebgn(ichunk)          (((usoffset *)(usarena->base+ichunk   ))[ 0]&(~0x1))
#  define getsizeend(ichunk,sz)       ((sz)? ((usoffset *)(usarena->base+ichunk+sz))[-1] : 0)
#  define getnxtchunk(ichunk)         (((usoffset *)(usarena->base+ichunk   ))[ 1])
#  define getprvchunk(ichunk)         (((usoffset *)(usarena->base+ichunk   ))[ 2])
#  define getstatus(ichunk)           (((usoffset *)(usarena->base+ichunk   ))[ 0]&(0x1))

#  define setnxtchunk(ichunk,nxt)     (((usoffset *)(usarena->base+ichunk   ))[ 1]= nxt)
#  define setprvchunk(ichunk,prv)     (((usoffset *)(usarena->base+ichunk   ))[ 2]= prv)
#  define setsize(ichunk,sz)          (((usoffset *)(usarena->base+ichunk   ))[ 0]= sz,\
									  ((usoffset *)(usarena->base+ichunk+sz))[-1]= sz)
#  define setfree(ichunk)             (((usoffset *)(usarena->base+ichunk   ))[ 0]|=  0x1)
#  define setinuse(ichunk)            (((usoffset *)(usarena->base+ichunk   ))[ 0]&= ~0x1)

#  define isfree(ichunk)              (((((usoffset *)(usarena->base+ichunk   ))[0])&1) == 1)
#  define isinuse(ichunk)             (((((usoffset *)(usarena->base+ichunk   ))[0])&1) == 0)

/* for inuse chunks (free chunks have additional overhead) */
#  define ptr2chunk(ptr)              ( (((usbase *)ptr) - sizeof(usoffset)) - usarena->base)
#  define chunk2ptr(ichunk)           ((void *)((usarena->base + ichunk + sizeof(usoffset))))

/* arena_bin_offset: should be the offset in USArenaShare to the bin array */
# define arena_bin_offset             ((unsigned)(((unsigned char *)&arenashare.bin)  - ((unsigned char *)&arenashare)))
# define arena_info_offset            ((unsigned)(((unsigned char *)&arenashare.info) - ((unsigned char *)&arenashare)))

#  define usabs(x,y)                  ((x>=y)? (x-y) : (y-x))
#  define USMAXONESIZE	63
# endif

/* ------------------------------------------------------------------------
 * Local Data Structures: {{{1
 */
struct USFreeBin_str {                /* USFreeBin:                     {{{2               */
    usoffset hd;                      /* head of same-bin-size linked list                 */
    usoffset tl;                      /* tail of same-bin-size linked list                 */
    };
struct USArena_str {                  /* USArena: (usptr_t)             {{{2               */
    char           *filename;         /* name of shared memory file                        */
    unsigned long   permission;       /* usual unix process permission for shared mem file */
    usbase         *mempool;          /* shared memory starts here                         */
    int             semid;            /* semaphore identifier                              */
    usbase         *base;             /* ptr to beginning of allocation-memory             */
    void           *memattach;        /* optional where-to-attach mempool                  */
    key_t           key;              /* (USArenaShare) IPC key                            */
    size_t          memsize;          /* (USArenaShare) total size of shared memory        */
    unsigned long   maxusers;         /* (USArenaShare) controls qty semaphores            */
    usoffset        info;             /* (USArenaShare) usinfo storage                     */
    USFreeBin      *bin;              /* (USArenaShare) free chunk bins                    */
    };
struct USArenaShare_str {             /* USArenaShare                    {{{2              */
    void          *memattach;         /* optional where-to-attach mempool                  */
    key_t          key;               /* IPC key                                           */
	int            usedlocks;         /* qty of locks currently in use                     */
    size_t         memsize;           /* total size of shared memory                       */
    unsigned long  maxusers;          /* current qty of semaphores                         */
	usoffset       info;              /* usgetinfo() and usputinfo() modify this           */
    USFreeBin      bin[USMAXFREEBIN]; /* free chunk bins                                   */
    };

/* ------------------------------------------------------------------------
 * Prototypes: {{{1
 */
ptrdiff_t usconfig(int,...);                             /* usarena.c  */
usptr_t *usinit(const char *);                           /* usarena.c  */
int usadd(usptr_t *);                                    /* usarena.c  */
int usarenalockinit(usptr_t *);                          /* usarena.c  */
int usarenalock(usptr_t *);                              /* usarena.c  */
int usarenaunlock(usptr_t *);                            /* usarena.c  */
void usfreearena(usptr_t *);                             /* usarena.c  */
void *uscalloc( size_t, size_t, usptr_t *);              /* usmalloc.c */
void usfree( void *, usptr_t *);                         /* usmalloc.c */
void *usmalloc( size_t, usptr_t *);                      /* usmalloc.c */
void *usrealloc( void *, size_t, usptr_t *);             /* usmalloc.c */
void *usrecalloc( void *,  size_t,  size_t,  usptr_t *); /* usmalloc.c */
int ushashsize(usoffset);                                /* usmalloc.c */
void usmemuse( USArena *, int);                          /* usmalloc.c */
char *usmemdesc( void *, char *);                        /* usmalloc.c */
void usmemdescfree(void *);                              /* usmalloc.c */
void usputinfo(usptr_t *,void *);                        /* usinfo.c   */
void *usgetinfo(usptr_t *);                              /* usinfo.c   */
int uscasinfo(usptr_t *,void *,void *);                  /* usinfo.c   */
#endif	/*  __USARENA_H__ */

/* ---------------------------------------------------------------------
 * Modelines: {{{1
 * vim: fdm=marker
 */
