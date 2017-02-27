/* usmalloc.c: this program uses the arena to (re-)allocate and free
 * shared memory.
 *
 * Author: Charles E. Campbell, Jr.
 * Date:   Oct 26, 2005
 */

/* =====================================================================
 * Header Section: {{{1
 */

/* ---------------------------------------------------------------------
 * Includes: {{{2
 */
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#define USINTERNAL
#include "arena.h"

/* ---------------------------------------------------------------------
 * Definitions: {{{2
 */
void usmemdescfree(void *ptr);
/* double_link: handles the generation of doubly linked lists.  Note that
 *   each structure is assumed to have members "nxt" and "prv".
 *   The new member becomes "tail" - ie. oldest is first in the linked list,
 *   the newest is last.
 */
#define double_link(structure,head,tail,fail_msg) {                    \
	structure *newstr;                                                 \
	newstr= (structure *) malloc(sizeof(structure));                   \
	if(!newstr) fprintf(stderr,(void *) newstr,"%s\n",fail_msg);       \
    else {                                                             \
        if(tail) (tail)->nxt= newstr;                                  \
        else     head       = newstr;                                  \
        newstr->prv= tail;                                             \
        newstr->nxt= (structure *) NULL;                               \
        tail       = newstr;                                           \
        }                                                              \
	}
#define delete_double_link(structure,str,head,tail) {                  \
	structure *old= str;                                               \
	if(old->prv) old->prv->nxt= old->nxt;                              \
	else         head         = old->nxt;                              \
	if(old->nxt) old->nxt->prv= old->prv;                              \
	else         tail         = old->prv;                              \
	free((char *) old);                                                \
	}
/* stralloc: allocates new memory for and copies a string into the new mem */
#define stralloc(ptr,string,fail_msg) {                                  \
	ptr= (char *) calloc((size_t) strlen(string) + 1,sizeof(char));      \
	if(!ptr) fprintf(stderr,"%s: <%s>\n",fail_msg,string);               \
    else     strcpy(ptr,string);                                         \
	}

/* ---------------------------------------------------------------------
 * Data: {{{2
 */
extern USArena *usarena;

/* ---------------------------------------------------------------------
 * Prototypes: {{{2
 */
static usoffset getnxtneighbor(usoffset);         /* usmalloc.c */
static usoffset getprvneighbor(usoffset);         /* usmalloc.c */
static usoffset resize(usoffset);                 /* usmalloc.c */
static usoffset sizecheck(usoffset);              /* usmalloc.c */
static void ExtractChunk(usoffset);               /* usmalloc.c */
static usoffset FindChunk(usoffset);              /* usmalloc.c */
static void InsertFreeChunk(usoffset);            /* usmalloc.c */
static void MergeFreeChunk(usoffset);             /* usmalloc.c */
static usoffset SplitChunk( usoffset,  usoffset); /* usmalloc.c */

/* =====================================================================
 * Functions: {{{1
 */

/* --------------------------------------------------------------------- */
/* uscalloc: this function emulates calloc() but using the arena memory pool {{{2 */
void *uscalloc(
  size_t   nelem, 
  size_t   elsize,
  usptr_t *arena) 
{
usoffset  totsize;
void     *pchunk = NULL;


usarena = arena;
totsize = nelem*elsize + 2*sizeof(usoffset); /* inuse overhead: size:status | user data | size:status */
if(totsize != 0) {
    pchunk = usmalloc(totsize,arena);
    if(pchunk) memset(pchunk,0,(size_t) totsize);       /* initialize memory to all zeros                        */
    }

return pchunk;
}

/* --------------------------------------------------------------------- */
/* usfree: this function emulates free() but using the arena memory pool {{{2 */
void usfree(
  void    *ptr,  
  usptr_t *arena)
{
usoffset ichunk;



usarena= arena;
if(ptr) {
    usarenalock(usarena);
    ichunk= ptr2chunk(ptr); /* convert pointer to user memory into an ichunk */
    sizecheck(ichunk);      /* check that the chunk hasn't been corrupted    */
    if(isfree(ichunk)) {    /* can't free an already free chunk              */
        return;
        }
    setfree(ichunk);        /* label memory as free                          */
    MergeFreeChunk(ichunk); /* merge newly free'd chunk                      */
    usarenaunlock(usarena);
    }


}

/* --------------------------------------------------------------------- */
/* usmalloc: this function emulates malloc() but using the arena memory pool {{{2
 *   inuse chunk:  size/status=inuse
 *                 ..user space..     <-pointer returned to user space
 *                 size
 */
void *usmalloc(
  size_t   size, 
  usptr_t *arena)
{
usoffset  ichunk;
void     *pchunk;



size   += 2*sizeof(usoffset); /* inuse overhead: size:status | user data | size:status */
usarena = arena;
usarenalock(usarena);
ichunk  = FindChunk((usoffset) size);
if(ichunk) {
    setinuse(ichunk);
    pchunk  = chunk2ptr(ichunk);
    }
else pchunk= NULL;
usarenaunlock(usarena);


return pchunk;
}

/* --------------------------------------------------------------------- */
/* usrealloc: this function emulates realloc() but using the arena memory pool {{{2 */
void *usrealloc(
  void    *ptr,  
  size_t   size, 
  usptr_t *arena)
{
usoffset  newchunk;         /* new chunk having new size */
usoffset  oldchunk;         /* old chunk                 */
usoffset  oldsize;          /* actual old chunk size     */
usoffset  copyqty;
void     *newptr = NULL;



usarena = arena;

if(!ptr) newptr= usmalloc(size,arena);             /* if ptr is null, treat like a plain malloc     */
else if(size == 0L) {                              /* looks like an odd way to free a chunk         */
    usfree(ptr,arena);
    newptr= NULL;
    }
else {                                             /* do a real re-alloc                            */
    oldchunk= ptr2chunk(ptr);                      /* convert ptr to chunk index                    */
    oldsize = sizecheck(oldchunk);                 /* get ichunk's current size, including overhead */
    newptr= usmalloc(size,arena);
    if(newptr) {
        newchunk = ptr2chunk(newptr);
        copyqty  = oldsize - 2*sizeof(usoffset);   /* don't copy overhead bytes                     */
        if(size < copyqty) copyqty= size;
        memcpy(newptr,ptr,copyqty);
        usfree(ptr,arena);
        }
    }


return newptr;
}

/* --------------------------------------------------------------------- */
/* usrecalloc: this function merges usrealloc() and uscalloc(). {{{2
 * New bytes are initialized to zero (if the new size is > old size).
 * Old bytes are copied to the beginning of the new memory.
 */
void *usrecalloc(
  void    *ptr,   	/* pointer to previously uscalloc'd memory */
  size_t   nel,   	/* number of elements in new memory        */
  size_t   elsize,	/* size of an element                      */
  usptr_t *arena) 	/* arena                                   */
{
void     *newptr;
usoffset  newchunk;
usoffset  newsize;
usoffset  oldchunk;
usoffset  oldsize;


if(nel == 0 || elsize == 0) { /* an odd way to free the memory */
    usfree(ptr,arena);
    }
else {
    oldchunk = ptr2chunk(ptr);
    oldsize  = sizecheck(oldchunk);
    newsize  = nel*elsize;
    newptr   = usmalloc(newsize,arena);
    if(newptr) {
        newchunk = ptr2chunk(newptr);
        oldsize -= 2*sizeof(usoffset);
        if(newsize < oldsize) {
            memcpy(newptr,ptr,newsize);
            }
        else {
            memcpy(newptr,ptr,oldsize);
            memset(newptr+oldsize,0,newsize-oldsize);
            }
        usfree(ptr,arena); /* free up old pointer information */
        }
    }

return newptr;
}

/* =====================================================================
 * Support Routines: {{{1
 */

/* getnxtneighbor: this function returns a usoffset to the next neighboring chunk {{{2
 *  free chunk : size/status=free
 *               ptr to next chunk in bin (actually an offset)
 *               ptr to prev chunk in bin (actually an offset)
 *               ..unused space..
 *               size
 */
static usoffset getnxtneighbor(usoffset ichunk)
{

ichunk+= getsizebgn(ichunk);
if(ichunk > usarena->memsize) ichunk= 0;

return ichunk;
}

/* --------------------------------------------------------------------- */
/* getprvneighbor: this function returns a usoffset to the preceding neighboring chunk {{{2
 *  free chunk : size/status=free
 *               index to next chunk in bin
 *               index to prev chunk in bin
 *               ..unused space..
 *               size
 */
static usoffset getprvneighbor(usoffset ichunk)
{
usoffset iszchunk;


iszchunk= ((usoffset *)(usarena->base+ichunk))[-1];
if     (iszchunk == 0)     ichunk  = 0;
else if(iszchunk > ichunk) ichunk  = 0;
else                       ichunk -= iszchunk;

return ichunk;
}

/* --------------------------------------------------------------------- */
/* hashsize: maps sz to [0,155] {{{2
 *  The first [0,63] are one-size-only bins
 */
int ushashsize(usoffset sz)
{
int           hash;
unsigned long isz;         /* temporary sz */
unsigned long ssz    = sz; /* shifted      */
unsigned long offset = 0;


/* overhead for inuse chunk: 8 bytes  (size: ... : size)
 * overhead for free  chunk: 16 bytes (size:nxt:prv:...:size)
 * So the minimum chunk must have 16 bytes.
 * All chunks assumed to be in multiples of 8 bytes (for alignment purposes).
 * bins 0-63 : each hold one size only
 */
if     (sz <= 512) hash= (sz>>3)-1; /* ie. hash/8, maps [1,512] -> [0,63] */
else {
    ssz= sz>>3;
    isz= ssz;
    
    hash= 1;
    if(isz & 0xffff0000) hash+= 16, isz&= 0xffff0000; /* 11111111 11111111 00000000 00000000 */
    if(isz & 0xff00ff00) hash+=  8, isz&= 0xff00ff00; /* 11111111 00000000 11111111 00000000 */
    if(isz & 0xf0f0f0f0) hash+=  4, isz&= 0xf0f0f0f0; /* 11110000 11110000 11110000 11110000 */
    if(isz & 0xcccccccc) hash+=  2, isz&= 0xcccccccc; /* 11001100 11001100 11001100 11001100 */
    if(isz & 0xaaaaaaaa) hash+=  1, isz&= 0xaaaaaaaa; /* 10101010 10101010 10101010 10101010 */
    
    offset = (ssz>>(hash-3))&(~4); /* extract two bits to the right of the leftmost 1 */
    hash   = ((hash<<2)|offset) + 36;
    }


return hash;
}

/* --------------------------------------------------------------------- */
/* resize: this function resizes a user request by requiring that a chunk: {{{2
 *  - has at least sz bytes available for the user to use
 *  - begins at an eight-byte boundary
 *  - is at least 16 bytes (four usoffsets) long, so that when it is free'd, it can go into a free-bin-list
 *
 * rounding upwards
 * to the nearest eight-byte boundary.  In addition, a minimum of
 * 16 bytes is imposed
 */
static usoffset resize(usoffset sz)
{
usoffset rsz;


if(sz < MINCHUNKSIZE) rsz= MINCHUNKSIZE;
else                  rsz= ((sz-1)&(~0x7)) + 8;

return rsz;
}

/* --------------------------------------------------------------------- */
/* sizecheck: this function checks sizes and will issue a SIGBUS to {{{2
 *  cause a core dump if the sizes don't match.
 *
 *  When beginning and ending sizes match: returns size of chunk.
 *  Else issues a SIGBUS, which generally causes a core dump.
 *       If the user bypasses the core dump and this routine continues,
 *       a size of zero will be returned.
 */
static usoffset sizecheck(usoffset ichunk)
{
usoffset sz1;
usoffset sz2;


sz1 = getsizebgn(ichunk);
sz2 = getsizeend(ichunk,sz1);
if(sz1 != sz2) { /* looks like memory is corrupted! */
    int status;
    fprintf(stderr,"(usmemuse) shared memory corruption detected concerning <%s>! (sz=%lu != endsz=%lu)\n",
      usmemdesc(chunk2ptr(ichunk),NULL),
      sz1,sz2);
    kill(0,SIGBUS); /* normally should get a core dump out of this */
    wait(&status);
    sz1= 0;
    }

return sz1&(~7);
}

/* --------------------------------------------------------------------- */
/* ExtractChunk: this function extracts a free chunk for subsequent {{{2
 *                   use - ie. it removes it from the binlist links.
 */
static void ExtractChunk(usoffset ichunk)
{
usoffset prvchunk;
usoffset nxtchunk;
usoffset zero= 0;
int      ibin= 0;



prvchunk = getprvchunk(ichunk);
nxtchunk = getnxtchunk(ichunk);

if(prvchunk) {
    setnxtchunk(prvchunk,nxtchunk);
    }
else { /* ichunk must be head-of-binlist */
    ibin                  = ushashsize(getsizebgn(ichunk));
    usarena->bin[ibin].hd = nxtchunk;
    }

if(nxtchunk) {
    setprvchunk(nxtchunk,prvchunk);
    }
else { /* ichunk must be tail-of-binlist */
    if(!ibin) ibin= ushashsize(getsizebgn(ichunk));
    usarena->bin[ibin].tl = prvchunk;
    }

setnxtchunk(ichunk,zero);
setprvchunk(ichunk,zero);


}

/* --------------------------------------------------------------------- */
/* FindChunk: this function finds a suitable free chunk for conversion {{{2
 *            into a inuse chunk.  Splits the chunk, assuming it finds
 *            a suitable free chunk.
 */
static usoffset FindChunk(usoffset needsz)
{
int      ibin;
int      needszhash;
usoffset fsz;
usoffset ichunk     = 0;


needsz     = resize(needsz);
needszhash = ushashsize(needsz);

/* look for a non-empty free space bin >= needszhash */
for(ibin= needszhash; ibin < USMAXFREEBIN; ++ibin) if(usarena->bin[ibin].hd) break;

/* If ibin == needszhash, then since the bins hold multiple sizes,
 * there still may not be a free chunk with needsz bytes.
 * The bins are sorted on size; so if the tail is big enough, fine;
 * otherwise, look further.  Any subsequent non-empty ibins will
 * always have a free chunk big enough.
 */
if(ibin == needszhash) {
    fsz= getsizebgn(usarena->bin[ibin].tl);
    if(needsz == fsz) {
        /* looks like the tail of the bin has a free chunk exactly of the size
         * needed to accomodate the request.
         */
        ichunk= SplitChunk(usarena->bin[ibin].tl,needsz);
        return ichunk;
        }
    else if(needsz > fsz) {
        for(++ibin; ibin < USMAXFREEBIN; ++ibin) if(usarena->bin[ibin].hd) break;
        }
    }

if(ibin >= USMAXFREEBIN) { /* whoops! unable to find a free chunk big enough to handle needsz */
    /* if this was normal memory, this place is where one
     * would test a "wilderness" chunk and then attempt to
     * sbrk more as needed
     */
    ichunk= 0;
    }
else if(ibin <= USMAXONESIZE) { /* single-size bin */
    ichunk= SplitChunk(usarena->bin[ibin].hd,needsz);
    }
else { /* multi-size bin */
    usoffset bgnsz;
    usoffset endsz;
    usoffset fchunk;
    usoffset prvfchunk;

    bgnsz = getsizebgn(usarena->bin[ibin].hd); /* bgnsz = hd->size */
    endsz = getsizebgn(usarena->bin[ibin].tl); /* endsz = tl->size */

    if(usabs(bgnsz,needsz) < usabs(endsz,needsz)) { /* search onwards from head   */
        fchunk= usarena->bin[ibin].hd;
        while(fchunk) {
            fsz= getsizebgn(fchunk);
            if(fsz > needsz) break;
            fchunk= getnxtchunk(fchunk);
            }
        }
    else {
        prvfchunk = 0;
        fchunk    = usarena->bin[ibin].tl;
        while(fchunk > 0) {
            fsz= getsizebgn(fchunk);
            if(fsz < needsz) {
                break;
                }
            prvfchunk = fchunk;
            fchunk    = getprvchunk(fchunk);
            }
        fchunk= prvfchunk;
        }
    if(fchunk) {
        ichunk= SplitChunk(fchunk,needsz);
        }
    }


return ichunk;
}

/* --------------------------------------------------------------------- */
/* InsertFreeChunk: this function inserts a chunk into the free-chunk {{{2
 *                  bins.  Searches as necessary.
 */
static void InsertFreeChunk(usoffset ichunk)
{
int          ibin;
usoffset     isz;
usoffset     zero      = 0;


(void)sizecheck(ichunk);

setfree(ichunk);
isz = getsizebgn(ichunk);
ibin= ushashsize(isz);

if(usarena->bin[ibin].hd == 0) { /* the first chunk for this bin */
    setnxtchunk(ichunk,zero);
    setprvchunk(ichunk,zero);
    (void)sizecheck(ichunk);
    usarena->bin[ibin].hd= usarena->bin[ibin].tl= ichunk;
    }

else {
    if(ibin <= USMAXONESIZE) { /* one size bins, simply append it */
        setnxtchunk(usarena->bin[ibin].tl,ichunk);
        setprvchunk(ichunk,usarena->bin[ibin].tl);
        setnxtchunk(ichunk,zero);
        usarena->bin[ibin].tl= ichunk;
        }
    else { /* find insertion point */
        usoffset before;
        usoffset szbefore;
        usoffset bgnsz;
        usoffset endsz;
        usoffset nxtbefore;
        usoffset prvbefore;


        isz   = getsizebgn(ichunk);
        bgnsz = getsizebgn(usarena->bin[ibin].hd);
        endsz = getsizebgn(usarena->bin[ibin].tl);

        /* use size as a proxy for which way to search the list.
         * I may improve this to a red-black tree sometime.
         * Note that ibin>=USMAXONESIZE means that the free space
         * is plenty large enough to accommodate the extra fields
         * needed for red-black trees.
         */
        if(usabs(bgnsz,isz) < usabs(endsz,isz)) { /* search onwards from head   */
            before= usarena->bin[ibin].hd;
            while(before) {
                szbefore  =  getsizebgn(before);
                if(szbefore > isz) break;
                nxtbefore = getnxtchunk(before);
                before    = nxtbefore;
                }
            }
        else {                                    /* search backwards from tail */
            nxtbefore = 0;
            before    = usarena->bin[ibin].tl;
            while(before) {
                szbefore= getsizebgn(before);
                if(szbefore < isz) break;
                prvbefore = getprvchunk(before);
                nxtbefore = before;
                before    = prvbefore;
                }
            before= nxtbefore;
            }

        if(!before) {                                    /* insert after end-of-binlist                    */
            setnxtchunk(usarena->bin[ibin].tl,ichunk); /* tl->nxt= ichunk                                */
            setprvchunk(ichunk,usarena->bin[ibin].tl); /* ichunk->prv= tl                                */
            setnxtchunk(ichunk,zero);                    /* ichunk->nxt= null                              */
            usarena->bin[ibin].tl= ichunk;             /* tl= ichunk                                     */
            }
        else if(before == usarena->bin[ibin].hd) {     /* insert before head-of-binlist                  */
            setprvchunk(usarena->bin[ibin].hd,ichunk); /* hd->prv= ichunk                                */
            setnxtchunk(ichunk,usarena->bin[ibin].hd); /* ichunk->nxt= hd                                */
            setprvchunk(ichunk,zero);                    /* ichunk->prv= null                              */
            usarena->bin[ibin].hd= ichunk;             /* hd= ichunk                                     */
            }
        else {                                           /* perform insertion to yield after,ichunk,before */
            usoffset after;
            after= getprvchunk(before);                  /* after= before->prv                             */
            setnxtchunk(after,ichunk);                   /* after->nxt= ichunk                             */
            setprvchunk(before,ichunk);                  /* before->prv= ichunk                            */
            setnxtchunk(ichunk,before);                  /* ichunk->nxt= before                            */
            setprvchunk(ichunk,after);                   /* ichunk->prv= after                             */
            }
        }
    }


}

/* --------------------------------------------------------------------- */
/* MergeFreeChunk: this function merges a chunk with either or both of {{{2
 * its neighbors (if they are already free chunks).  Does not
 * extract ichunk; assumes its already been extracted!
 */
static void MergeFreeChunk(usoffset ichunk)
{
usoffset prvchunk;
usoffset nxtchunk;
usoffset isz;
usoffset newsz;
usoffset nxtsz;
usoffset prvsz;



/* sanity check */
if(!isfree(ichunk)) {
    return;
    }

/* these two don't refer to the binlist links, but to neighbors */
prvchunk= getprvneighbor(ichunk);
nxtchunk= getnxtneighbor(ichunk);

if(prvchunk && isfree(prvchunk)) { /* merge prvchunk,ichunk */
    isz     = getsizebgn(ichunk);
    prvsz   = getsizebgn(prvchunk);
    ExtractChunk(prvchunk);
    newsz   = isz + prvsz;
    setsize(prvchunk,newsz);
    ichunk  = prvchunk;
    }

if(nxtchunk && isfree(nxtchunk)) { /* merge ichunk,nxtchunk */
    isz   = getsizebgn(ichunk);
    nxtsz = getsizebgn(nxtchunk);
    ExtractChunk(nxtchunk);
    newsz = isz + nxtsz;
    setsize(ichunk,newsz);
    }

/* insert free chunk into binlists */
InsertFreeChunk(ichunk);

}

/* --------------------------------------------------------------------- */
/* SplitChunk: this function splits a chunk into sz and original_size-sz {{{2
 * byte chunks.  The original_size-sz chunk is placed back into the free
 * chunk binlists. 
 *
 *      [ichunk|free chunk] -> [needsz user chunk      | free chunk + leftovers ]
 *      [free chunk|ichunk] -> [free chunk + leftovers | needsz user chunk]
 *
 *  Of course, if there're no "leftovers", then ichunk is simply transformed
 *  into a user chunk (removed from binlists).
 *
 *  free chunk : size/status=free             inuse chunk:  size/status=inuse
 *               index to next chunk in bin                 ..user space..
 *               index to prev chunk in bin                 size
 *               ..unused space..
 *               size
 */
static usoffset SplitChunk(
  usoffset ichunk,  /* this chunk will be split                        */
  usoffset needsz)  /* this is the needed size of the to-be-used chunk */
{
int      nxtfree;    /* free/inuse status of next-neighbor chunk     */
int      prvfree;    /* free/inuse status of previous-neighbor chunk */
usoffset fchunk;     /* new free chunk                               */
usoffset fsz;        /* new free chunk size                          */
usoffset isz;        /* size of ichunk                               */
usoffset nxtchunk;   /* next-neighbor chunk                          */
usoffset nxtsz;      /* size of next-neighbor chunk                  */
usoffset prvchunk;   /* previous-neighbor chunk                      */
usoffset prvsz;      /* size of previous-neighbor chunk              */


sizecheck(ichunk);
isz= getsizebgn(ichunk); /* size of to-be-split chunk */
ExtractChunk(ichunk);

/* Of course, if isz==needsz, no splitting needed, just use ichunk.
 * However, we also don't want to split ichunk into two chunks, one
 * with needsz bytes and one with a size < MINCHUNKSIZE bytes.
 */
if(needsz <= isz && isz <= needsz+MINCHUNKSIZE) {
    setinuse(ichunk);
    }
else if(isz > needsz) {
    fsz      = isz - needsz;
    nxtchunk = getnxtneighbor(ichunk);
    prvchunk = getprvneighbor(ichunk);
    prvfree  = prvchunk? isfree(prvchunk) : 0;
    nxtfree  = nxtchunk? isfree(nxtchunk) : 0;

    if(!prvfree && !nxtfree) {
        /* insert free space sliver into binlists */
        fchunk= ichunk + needsz;
        setsize(fchunk,fsz);
        setfree(fchunk);
        setsize(ichunk,needsz);
        setinuse(ichunk);
        InsertFreeChunk(fchunk);
        }
    else {
        nxtsz = getsizebgn(nxtchunk);
        prvsz = getsizebgn(prvchunk);
        if(prvfree && (!nxtfree || (nxtfree && prvsz > nxtsz))) {
            /* merge free space sliver with prvfree */
            fchunk  = ichunk;
            ichunk += fsz;
            }
        else {
            /* merge free space sliver with nxtfree */
            fchunk= ichunk + needsz;
            }
        setsize(fchunk,fsz);
        setfree(fchunk);
        setsize(ichunk,needsz);
        setinuse(ichunk);
        InsertFreeChunk(fchunk);
        MergeFreeChunk(fchunk);
        }
    }
else fprintf(stderr,"(SplitChunk) requested %lu bytes from a chunk having only %lu bytes!\n",needsz,isz);

return ichunk;
}

/* --------------------------------------------------------------------- */
/* usmemuse: this function displays memory usage {{{2
 *   mode & 1 : print out free memory bins
 *   mode & 2 : print out all shared memory
 *   mode & 4 : only use debugging output
 *   mode & 8 : print out total bytes used
 */
void usmemuse(
  USArena *arena,
  int      mode) 
{
int      ibin   = 0;
usoffset ichunk = 0;
usoffset nxt    = 0;
usoffset prv    = 0;
usoffset sz     = 0;
usoffset endsz  = 0;

/* sanity check */
if(!arena) {
    printf("usmemuse: arena is null!");
    return;
    }
usarena= arena;

/* display free memory usage */
if(mode & 1) {
    if(!(mode & 4)) printf("Shared Free Memory, by bin:\n");
#ifdef USMEMUSEDBG
    dprintf(1,"Shared Free Memory, by bin: {\n");
#endif
    for(ibin= 0; ibin < USMAXFREEBIN; ++ibin) {
        if(arena->bin[ibin].hd) {
            nxt= 0;
            for(ichunk= arena->bin[ibin].hd; ichunk; ichunk= nxt) {
                nxt   = getnxtchunk(ichunk);
                prv   = getprvchunk(ichunk);
                sz    = getsizebgn(ichunk);
                endsz = getsizeend(ichunk,sz);
                if(!(mode & 4)) printf("   %10lu: bin[%3d] %2s prv=%10lu nxt=%10lu sz=%10lu endsz=%10lu %s\n",
                  ichunk,
                  ibin,
                  (ichunk == arena->bin[ibin].hd && ichunk == arena->bin[ibin].tl)? "ht" :
                  (ichunk == arena->bin[ibin].hd)?                                  "hd" :
                  (ichunk == arena->bin[ibin].tl)?                                  "tl" : "",
                  prv,
                  nxt,
                  sz,
                  endsz,
                  isfree(ichunk)? "free" : "inuse");
#ifdef USMEMUSEDBG
                dprintf(1,"   %10lu: bin[%3d] %2s prv=%10lu nxt=%10lu sz=%10lu endsz=%10lu %s\n",
                  ichunk,
                  ibin,
                  (ichunk == arena->bin[ibin].hd && ichunk == arena->bin[ibin].tl)? "ht" :
                  (ichunk == arena->bin[ibin].hd)?                                  "hd" :
                  (ichunk == arena->bin[ibin].tl)?                                  "tl" : "",
                  prv,
                  nxt,
                  sz,
                  endsz,
                  isfree(ichunk)? "free" : "inuse");
#endif
                /* sanity check */
                if(ichunk%8 != 0 || nxt%8 != 0 || prv%8 != 0) {
                    fprintf(stderr,"(usmemuse) shared memory corruption detected!\n");
                    }
                }
            }
        }
#ifdef USMEMUSEDBG
    dprintf(1,"}\n");
#endif
    }

if(mode == 3) {
    if(!(mode & 4)) printf("\n");
#ifdef USMEMUSEDBG
    dprintf(1,"\n");
#endif
    }

/* display memory that's in-use */
if(mode & 2) {
    if(!(mode & 4)) printf("Shared Memory Snapshot:\n");
#ifdef USMEMUSEDBG
    dprintf(1,"Shared Memory Snapshot: {\n");
#endif
    ichunk= 8L;
    do {
        if(isfree(ichunk)) {
            sz    = getsizebgn(ichunk);
            endsz = getsizeend(ichunk,sz);
            nxt   = getnxtchunk(ichunk);
            prv   = getprvchunk(ichunk);
            if(!(mode & 4)) printf(" free  %10lu: sz=%10lu endsz=%10lu nxt=%10lu prv=%10lu: %s\n",
              ichunk,
              sz,
              endsz,
              nxt,
              prv,
              usmemdesc(chunk2ptr(ichunk),NULL));
#ifdef USMEMUSEDBG
            dprintf(1," free  %10lu: sz=%10lu endsz=%10lu nxt=%10lu prv=%10lu: %s\n",
              ichunk,
              sz,
              endsz,
              nxt,
              prv,
              usmemdesc(chunk2ptr(ichunk),NULL) );
#endif
            /* sanity check */
            if(nxt%8 != 0 || prv%8 != 0) {
                fprintf(stderr,"(usmemuse) shared memory corruption detected!\n");
                }
            }
        else {
            sz    = getsizebgn(ichunk);
            endsz = getsizeend(ichunk,sz);
            if(!(mode & 4)) printf(" inuse %10lu: sz=%10lu endsz=%10lu: %s\n",
              ichunk,
              sz,
              endsz,
              usmemdesc(chunk2ptr(ichunk),NULL));
#ifdef USMEMUSEDBG
            dprintf(1," inuse %10lu: sz=%10lu endsz=%10lu %s\n",
              ichunk,
              sz,
              endsz,
              usmemdesc(chunk2ptr(ichunk),NULL));
#endif
            /* sanity check */
            if(nxt%8 != 0 || prv%8 != 0) {
                fprintf(stderr,"(usmemuse) shared memory corruption detected!\n");
                }
            }
        sizecheck(ichunk);
        ichunk+= sz;
        if(ichunk >= arena->memsize) ichunk= 0;
        } while(sz && ichunk);
#ifdef USMEMUSEDBG
    dprintf(1,"}\n");
#endif
    }

if(mode & 8) { /* print out total bytes in use */
    unsigned long totfree= 0L;
    unsigned long inuse  = 0L;
    /* this is done by taking the total allocated for shared memory use
     * and subtracting all free chunk sizes
     */
    if(!arena) inuse= 0;
    else {
        inuse= arena->memsize;
        for(ibin= 0; ibin < USMAXFREEBIN; ++ibin) {
            for(ichunk= arena->bin[ibin].hd; ichunk; ichunk= nxt) {
                nxt   = getnxtchunk(ichunk);
                prv   = getprvchunk(ichunk);
                sz    = getsizebgn(ichunk);
                endsz = getsizeend(ichunk,sz);
                totfree+= sz;
                }
            }
        inuse-= totfree;
        if(!(mode & 4)) printf("Totals:  inuse=%ld bytes   free=%ld bytes\n",inuse,totfree); 
#ifdef USMEMUSEDBG
        dprintf(1,"Totals:  inuse=%ld bytes   free=%ld bytes\n",inuse,totfree); 
#endif
        }
    }
}

/* --------------------------------------------------------------------- */

/* --------------------------------------------------------------------- */
#define USMEMDESCSIZ    1000
typedef struct UsMemDesc_str     UsMemDesc;

struct UsMemDesc_str {
    usoffset   chunk;
    char      *desc;
    UsMemDesc *nxt;
    UsMemDesc *prv;
    };
static UsMemDesc *usmemdeschd[USMEMDESCSIZ];
static UsMemDesc *usmemdesctl[USMEMDESCSIZ];

/* --------------------------------------------------------------------- */
/* usmemdesc: this function helps usmemuse() be more explanatory {{{2
 *   Usage:  immediately after doing a usmalloc, etc, do a usmemdesc(ptr,"description")
 *           The usmemuse() function will use usmemdesc(ptr,NULL) to look up the description
 */
char *usmemdesc(
  void *ptr, 
  char *desc)
{
static int  first  = 1;
UsMemDesc  *usdesc = NULL;
usoffset    chunk;
usoffset    hash;

if(first) {
    memset(usmemdeschd,0,USMEMDESCSIZ*sizeof(UsMemDesc *));
    memset(usmemdesctl,0,USMEMDESCSIZ*sizeof(UsMemDesc *));
    first= 0;
    }

/* avoid problems with null pointers */
if(!ptr) {
    return "null ptr";
    }

/* compute hash of pointer */
chunk = ptr2chunk(ptr);
hash  = chunk % USMEMDESCSIZ;
for(usdesc= usmemdeschd[hash]; usdesc; usdesc= usdesc->nxt) if(usdesc->chunk == chunk) break;

if(desc) { /* enter description into hash */
/*    printf("(usmemdesc) ptr=%px chunk#%9lu hash=%4lu desc<%s>\n",ptr,ptr2chunk(ptr),hash,sprt(desc));*/
    if(usdesc) { /* re-use memory */
        if(usdesc->desc) free(usdesc->desc);
        usdesc->desc  = NULL;
        usdesc->chunk = 0;
        }
    else { /* new memory */
        double_link(UsMemDesc,usmemdeschd[hash],usmemdesctl[hash],"enter usmemdesc");
        usdesc= usmemdesctl[hash];
        }
    stralloc(usdesc->desc,desc,"usmemdesc desc");
    usdesc->chunk= chunk;
    }
else { /* look up description */
    desc= usdesc? usdesc->desc : "";
    }

return desc;
}

/* --------------------------------------------------------------------- */
/* usmemdescfree: this function {{{2 */
void usmemdescfree(void *ptr)
{
usoffset   hash;
usoffset   chunk;
UsMemDesc *usdesc;


/* avoid problems with null pointers */
if(!ptr) {
    return;
    }

/* compute hash of pointer */
chunk = ptr2chunk(ptr);
hash  = chunk % USMEMDESCSIZ;

/* locate pointer in hash table and free it */
for(usdesc= usmemdeschd[hash]; usdesc; usdesc= usdesc->nxt) if(usdesc->chunk == chunk) {
    if(usdesc->desc) free(usdesc->desc);
    usdesc->desc= NULL;
    delete_double_link(UsMemDesc,usdesc,usmemdeschd[hash],usmemdesctl[hash]);
    break;
    }

}

/* =====================================================================
 * Modelines: {{{1
 * vim: sw=4 sts=4 et fdm=marker ts=4
 */
