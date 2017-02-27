/* usinfo.c: this program implements exchanging information through an arena
 *   Author: Charles E. Campbell, Jr.
 *   Date:   Dec  2, 2005
 */

/* =====================================================================
 * Header Section: {{{1
 */

/* ---------------------------------------------------------------------
 * Includes: {{{2
 */
#define USINTERNAL
#include "arena.h"

/* ------------------------------------------------------------------------
 * Local Data: {{{2
 */
static USArenaShare arenashare;

/* ========================================================================
 * Functions: {{{1
 */

/* --------------------------------------------------------------------- */
/* usputinfo: this function puts an information pointer into the shared arena {{{2
 *   It is assumed that that information pointer points to something *in* the arena.
 */
void usputinfo(usptr_t *usarena,void *info)
{

if(usarena) {
    /* modify local-to-process copy of usarena structure */
    if(info) usarena->info = ptr2chunk(info);
    else     usarena->info = 0L;

    /* put copy of info into shared memory */
    usarenalock(usarena);
    *((usoffset *)(usarena->mempool + arena_info_offset))= usarena->info;
    usarenaunlock(usarena);
    }

}

/* --------------------------------------------------------------------- */
/* usgetinfo: this function gets the info out of the shared arena {{{2 */
void *usgetinfo(usptr_t *usarena)
{
void     *info   = NULL;
usoffset  ichunk;


if(usarena) {
    /* get copy of info out of shared memory */
    usarenalock(usarena);
    ichunk = *((usoffset *) (usarena->mempool + arena_info_offset));
    usarenaunlock(usarena);
    if(usarena->info) info = chunk2ptr(usarena->info);
    else              info = NULL;
    }

return info;
}

/* --------------------------------------------------------------------- */
/* uscasinfo: this function {{{2 */
int uscasinfo(
  usptr_t *usarena,
  void    *old, /* will hold old info (should be void **) */
  void    *new) /* set info to new                        */
{
usoffset ichunk;


if(usarena) {
    usarenalock(usarena);
    if(old) {
        ichunk= *((usoffset *) (usarena->mempool + arena_info_offset));
        if(ichunk) *((usoffset **)old) = chunk2ptr(ichunk);
        else       *((usoffset **)old) = NULL;
        }
    if(new) {
        ichunk                                                = ptr2chunk(new);
        *((usoffset *)(usarena->mempool + arena_info_offset)) = ichunk;
        usarena->info                                         = ichunk;
        }
        else *((usoffset *)(usarena->mempool + arena_info_offset)) = 0L;
    usarenaunlock(usarena);
    return 1;
    }

return 0;
}

/* =====================================================================
 * Modelines: {{{1
 * vim: sw=4 sts=4 et fdm=marker ts=4
 */
