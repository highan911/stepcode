/*
 * Dynamic hashing, after CACM April 1988 pp 446-457, by Per-Ake Larson.
 * https://cacm.acm.org/magazines/1988/4/9970-dynamic-hash-tables/abstract
 * 
 * Coded into C, with minor code improvements, and with hsearch(3) interface,
 * by ejp@ausmelb.oz, Jul 26, 1988: 13:16;
 * also, hcreate/hdestroy routines added to simulate hsearch(3).
 *
 * ~50% rewritten by Chris Horler for STEPcode
 * 
 * Specific implementation points (based on empirical data)
 * 
 *  - the maximum SCHEMA symbol table is probably about 4-6k entries
 *  - the typical symbol table is (only!) between 8 and 64 entries
 *  - there's no extended growth behaviour of symbol tables
 *  - ...
 * 
 * REMARK: This approach is probably overly complex for such a small data set
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#ifdef HASH_DEBUG
#include <math.h>
#endif

#include "bstrlib.h"
#include "sc_memmgr.h"

#include "express/hash.h"

#include "hash_impl.h"

#define SEGMENT_SZ       32 /* deliberately small segment size! */
#define SEGMENT_WIDTH     5 /* log2(SEGMENT_SZ)	*/
#define LOAD_FACTOR       1 /* ensures minimum collisions, maximum size */

#define PRIME1           37
#define PRIME2      1048583

typedef Hash_Entry **Segment;

struct Hash_Table_ {
    unsigned int    p;      /**< Next bucket to be split  */
    unsigned int    maxp;   /**< upper bound on p during expansion    */
    unsigned int    KeyCount;       /**< current # keys   */
    unsigned int    SegmentCount;   /**< current # segments   */
    Segment *Directory;
    unsigned int DirectorySize;
    unsigned int SegmentSize;
#ifdef HASH_DEBUG
    unsigned int HashCollisions;
#endif    
};

#ifndef HASH_TESTING

struct freelist_head HASH_Table_fl;
struct freelist_head HASH_Entry_fl;

void
HASHinitialize() {
    if( HASH_Table_fl.size_elt == 0 ) {
        MEMinitialize( &HASH_Table_fl, sizeof( struct Hash_Table_ ), 500, 100 );
    }
    if( HASH_Entry_fl.size_elt == 0 ) {
        MEMinitialize( &HASH_Entry_fl, sizeof( Hash_Entry ), 2500, 500 );
    }
}
#endif

Hash_Table
HASHcreate()
{
    Hash_Table tbl;
    unsigned int i;
    
    tbl = HASH_Table_new();

    /* default to minimum size */
    tbl->maxp = SEGMENT_SZ;
    tbl->DirectorySize = tbl->maxp >> SEGMENT_WIDTH;

    /* allocate segments */
    tbl->Directory = sc_calloc(sizeof(Segment *), tbl->DirectorySize);
    if (tbl->Directory == NULL) {
        HASHdestroy(tbl);
        return NULL;
    }

    /* Allocate initial buckets */
    for (i = 0; i < tbl->DirectorySize; i++) {
        tbl->Directory[i] = sc_calloc(sizeof(Segment), SEGMENT_SZ);
        if (tbl->Directory[i] == NULL) {
            HASHdestroy(tbl);
            return NULL;
        }
        tbl->SegmentCount++;
    }
    
#ifdef HASH_DEBUG
    fprintf(stderr, "[hcreate] Table %p Count %u maxp %u SegmentCount %u\n",
            tbl, tbl->KeyCount, tbl->maxp, tbl->SegmentCount);
#endif
    return tbl;
}

void
HASHdestroy(Hash_Table tbl)
{
    unsigned int i, j;
#ifdef HASH_DEBUG
    float n, r;
#endif
    Segment	s;
    Hash_Entry	*p,*q;

    assert(tbl != NULL);

    for (i = 0; i < tbl->SegmentCount; i++) {
        s = tbl->Directory[i];
        assert(s != NULL);
        
        for (j = 0; j < SEGMENT_SZ; j++) {
            p = s[j];
            while (p != NULL) {
                q = p->next;
                sc_free(p);
                p = q;
            }
        }
        sc_free(s);
    }
    
    sc_free(tbl->Directory);
    
#ifdef HASH_DEBUG    
    r = tbl->KeyCount;
    n = tbl->SegmentCount << SEGMENT_WIDTH;
    
    fprintf(
        stderr,
        "[hdestroy] Count %u HashSize: %u Collisions %u ExpectedCollisions: %.2f\n",
        tbl->KeyCount,
        tbl->SegmentCount << SEGMENT_WIDTH,
        tbl->HashCollisions,
        ((r - n) + n * powf((n - 1) / n, r))
        );
#endif
    
    HASH_Table_destroy(tbl);
}

/* make a complete copy of a hash table */
/* Note that individual objects are shallow-copied.  OBJcopy is not called! */
/* But then, it isn't called when objects are inserted/deleted so this seems */
/* reasonable - DEL */
Hash_Table
HASHcopy( Hash_Table oldtable ) {
    Hash_Table newtable;
    Hash_Entry *p, **q;
    unsigned int i, j;

    newtable = HASH_Table_new();
    memcpy(newtable, oldtable, sizeof *oldtable);

    newtable->Directory = sc_calloc(newtable->DirectorySize, sizeof(Segment *));
    if (!newtable->Directory)
        /* HASHdestroy? */
        return NULL;
    
    for (i = 0; i < newtable->SegmentCount; i++) {
        newtable->Directory[i] = sc_calloc(SEGMENT_SZ, sizeof(Segment));
        if (!newtable->Directory[i])
            /* HASHdestroy */
            return NULL;
        
        for (j = 0; j < SEGMENT_SZ; j++) {
            p = oldtable->Directory[i][j];
            q = &newtable->Directory[i][j];
            while (p) {
                *q = HASH_Entry_new();
                memcpy(*q, p, sizeof(Hash_Entry));
                (*q)->next = NULL;
                q = &(*q)->next;
                p = p->next;
            }
        }
    }
    
    return newtable;
}

Hash_Entry *
HASHsearch(Hash_Table tbl, Hash_Entry item, Action action) {
    size_t h;
    Segment	CurrentSegment;
    int SegmentIndex;
    int SegmentDir;
    Hash_Entry **p, *q;

    assert(tbl != NULL);

    h = HASHhash(item.key);
    if (h % tbl->maxp < tbl->p) {
        h %= (tbl->maxp << 1);
    } else {
        h %= tbl->maxp;
    }
        
    SegmentDir = h >> SEGMENT_WIDTH;
    SegmentIndex = h % SEGMENT_SZ;

    CurrentSegment = tbl->Directory[SegmentDir];
    assert(CurrentSegment != NULL);

    p = &CurrentSegment[SegmentIndex];
    q = *p;

    /* Follow collision chain */    
    while (q != NULL && strcmp((char *) q->key, (char *) item.key)) {
        p = &q->next;
        q = *p;
    }
    
    /* use HASHdelete */
    assert(action != HASH_DELETE);
    
    /* found, not found - search only, alloc or fail */
    if (q != NULL || action == HASH_FIND || (q = HASH_Entry_new()) == NULL)
        return q;

#ifdef HASH_DEBUG
    /* check for a (new) collision */
    if (p != &CurrentSegment[SegmentIndex])
        tbl->HashCollisions++;
#endif

    /* link into chain, initialize new element */
    *p = q; 
    q->key = item.key;
    q->data = item.data;
    /* TODO: check, probably only need one of these */
    q->symbol = item.symbol;
    q->type = item.type;

    ++tbl->KeyCount;
    
    /* resize the table */
    if (tbl->KeyCount / (tbl->SegmentCount << SEGMENT_WIDTH ) >= LOAD_FACTOR)
        HASHexpand_table(tbl);
    
    return q;
}

void
HASHdelete(Hash_Table tbl, Hash_Entry *item) {
    size_t h;
    Segment	CurrentSegment;
    int SegmentIndex;
    int SegmentDir;
    Hash_Entry **p, *q;

    assert(tbl != NULL);

    h = HASHhash(item->key);
    if (h % tbl->maxp < tbl->p) {
        h %= (tbl->maxp << 1);
    } else {
        h %= tbl->maxp;
    }
        
    SegmentDir = h >> SEGMENT_WIDTH;
    SegmentIndex = h % SEGMENT_SZ;

    CurrentSegment = tbl->Directory[SegmentDir];
    assert(CurrentSegment != NULL);

    p = &CurrentSegment[SegmentIndex];
    q = *p;

    /* Follow collision chain */    
    while (q != NULL && strcmp((char *) q->key, (char *) item->key)) {
        p = &q->next;
        q = *p;
    }
    
    /* ensure no undefined assignment behaviour */
    assert(q != NULL && q != item);
    
    *p = q->next;
    item->data = q->data;
    item->symbol = q->symbol;
    item->type = q->type;
    tbl->KeyCount--;
    HASH_Entry_destroy(q);
}


/*
** Internal routines
*/
size_t HASHhash(unsigned char *key)
{
    size_t h;
    unsigned char *k;
    
    /* Convert string to integer */
    for (h = 0, k = key; *k; k++)
        h = h * PRIME1 ^ toupper(*k);
    h %= PRIME2;
    
    return h;
}

/*
 * tbl->p     the next bucket to be split [x mod N]
 * tbl->maxp  the current table size [N]
 * 
 * NOTE: the method, as it's not entirely obvious
 * 
 * e.g. tbl->maxp == 4 [N]
 * 4, 8, 12, 16 are in bucket 0  (x mod 4 == 0)
 * 4, 12 are in (new) bucket 4   (x mod 8 == 4)
 * 8, 16 remain in bucket 0      (x mod 8 == 0)
 * 
 * general form
 * x mod N
 * -> x mod 2N
 */
void
HASHexpand_table(Hash_Table tbl)
{
    size_t	NewAddress, h;
#ifdef HASH_DEBUG
    int ci, cj, ck;
#endif
    int OldSegmentIndex, NewSegmentIndex;
    int OldSegmentDir, NewSegmentDir;
    Segment OldSegment, NewSegment, *NewDirectory;
    Hash_Entry *Current,**Previous,**LastOfNew;

    /* check there's enough space for a new bucket, attempt resize */
    if (tbl->maxp + tbl->p >= (tbl->DirectorySize << SEGMENT_WIDTH)) {
        NewDirectory = sc_realloc(tbl->Directory, sizeof(Segment *) * (tbl->DirectorySize << 1));
        if (!NewDirectory)
            return;
        tbl->Directory = NewDirectory;
        memset(tbl->Directory + tbl->SegmentCount, 0, sizeof(Segment *) * tbl->DirectorySize);
        tbl->DirectorySize <<= 1;
    }

    /*
    ** Locate the bucket to be split
    */
    OldSegmentDir = tbl->p >> SEGMENT_WIDTH;
    OldSegment = tbl->Directory[OldSegmentDir];
    OldSegmentIndex = tbl->p % SEGMENT_SZ;
    /*
    ** Expand address space; if necessary create a new segment
    */
    NewAddress = tbl->maxp + tbl->p;
    NewSegmentDir = NewAddress >> SEGMENT_WIDTH;
    NewSegmentIndex = NewAddress % SEGMENT_SZ;
    if (NewSegmentIndex == 0) {
#ifdef HASH_DEBUG
        fprintf(stderr, "[ExpandTable] %p Count %u maxp %d SegmentCount %u Collisions %u\n",
                tbl, tbl->KeyCount, tbl->maxp, tbl->SegmentCount, tbl->HashCollisions);
#endif
        tbl->Directory[NewSegmentDir] = sc_calloc(sizeof(Segment), SEGMENT_SZ);
        if (!tbl->Directory[NewSegmentDir])
            return;
        tbl->SegmentCount++;
    }
    NewSegment = tbl->Directory[NewSegmentDir];
            
    /*
    ** Relocate records to the new bucket
    */
    Previous = &OldSegment[OldSegmentIndex];
    Current = *Previous;
    LastOfNew = &NewSegment[NewSegmentIndex];
    *LastOfNew = NULL;
#ifdef HASH_DEBUG
    ci = cj = ck = -1;
#endif
    while (Current != NULL)
    {
#ifdef HASH_DEBUG
        /* collisions to remove */
        ck++;
#endif
        /* rehash for a factor 2 increase */
        h = HASHhash(Current->key) % (tbl->maxp << 1);
        if (h == NewAddress) {
            /* Attach it to the end of the new chain */
            *LastOfNew = Current;
            
            /* Remove it from old chain */
            *Previous = Current->next;
            LastOfNew = &Current->next;
            Current = Current->next;
            *LastOfNew = NULL;
#ifdef HASH_DEBUG      
            /* collisions to add */
            ci++;
#endif
        } else {
            /* leave it on the old chain */
            Previous = &Current->next;
            Current = Current->next;
#ifdef HASH_DEBUG      
            /* collisions to add */
            cj++;
#endif
        }
    }
    
#ifdef HASH_DEBUG
    if (ck > 0) {
        tbl->HashCollisions -= ck;
        if (ci > 0)
            tbl->HashCollisions += ci;
        if (cj > 0)
            tbl->HashCollisions += cj;
    }
#endif
    /* increment for the next bucket */
    tbl->p++;
    if (tbl->p == tbl->maxp)
    {
        tbl->maxp <<= 1;
        tbl->p = 0;
    }
}


/****** minimal iterator implementation **********/

void
HASHdo_init(Hash_Table tbl, Hash_Iterator *it, char type) {
    it->hash = 0;
    it->table = tbl;
    it->p = NULL;
    it->type = type;
}

/* provide a way to step through the hash */
Hash_Entry *
HASHdo( Hash_Iterator * it ) {
    int SegmentDir, SegmentIndex;

    for (; it->hash < (it->table->DirectorySize << SEGMENT_WIDTH); it->hash++) {
        SegmentDir = it->hash >> SEGMENT_WIDTH;
        SegmentIndex = it->hash % SEGMENT_SZ;

        /* segments are initialised in HASHcreate and HASHexpand_table */
        assert(it->table->Directory[SegmentDir] != NULL);
        
        it->p = it->table->Directory[SegmentDir][SegmentIndex];
        if (it->p && (it->type == '*' || it->type))
            return it->p;
    }
    
    return NULL;
}

