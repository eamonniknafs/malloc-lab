/*
 * mm.c - An efficient and simple malloc package using a bidirectional
 * implicit list.
 * 
 * In this implementation of malloc, an implicit heap list is made where each 
 * block has both a header an a footer, allowing for bidirectional traversal.
 * This implementation uses next fit to find a free block, in which the list is
 * searched where the previous search finished, allowing for faster fits.
 *
 * NOTE: This code was largely inspired by the example at the end of 
 * chapter 9.9 of the CSAPP textbook.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* bu username : eg. jappavoo */
    "en",
    /* full name : eg. jonathan appavoo */
    "Eamon Niknafs",
    /* email address : jappavoo@bu.edu */
    "en@bu.edu",
    "",
    ""
};

/* Constants and Macros */  
/* header/footer size (bytes) */
#define HFSIZE 4       

/* double word size (bytes) */
#define DWORD 8

/* Default size to extend heap (bytes) */
#define CHUNKSIZE (1<<12)

/* Combines the size and allocated bit into a word (for the header/footer) */
#define HF(size, alloc) ((size) | (alloc))

/* Read and write a word at address a */
#define READ(a) (*(unsigned int *)(a))
#define WRITE(a, val) (*(unsigned int *)(a) = (val))

/* Read the size and allocation status of header/footer block at address a */
#define GET_SIZE(a) (READ(a) & ~0x7)
#define GET_ALLOC(a) (READ(a) & 0x1)

/* Calculate address of given block's header/footer */
#define HEAD(ptr) ((char *)(ptr) - HFSIZE)
#define FOOT(ptr) ((char *)(ptr) + GET_SIZE(HEAD(ptr)) - DWORD)

/* Calculate address of next/previous blocks */
#define NEXT(ptr) ((char *)(ptr) + GET_SIZE(((char *)(ptr) - HFSIZE)))
#define PREV(ptr) ((char *)(ptr) - GET_SIZE(((char *)(ptr) - DWORD)))

/* Global variables */  
static char *heapL = 0; //ptr to first block
static char *trav; //trav for next fit

/*
 * coalesce - Boundary coalescing. Returns pointer to coalesced block.
 *  - checks if next/prev blocks are allocated
 *  - runs different cases to coalesce depending on allocation
 */
static void *coalesce(void *ptr) 
{
    size_t prev_alloc = GET_ALLOC(FOOT(PREV(ptr)));
    size_t next_alloc = GET_ALLOC(HEAD(NEXT(ptr)));
    size_t size = GET_SIZE(HEAD(ptr));

    /* if both previous and next allocated */
    if (prev_alloc && next_alloc) {
        return ptr;
    }

    /* if only next is allocated */
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HEAD(PREV(ptr)));
        WRITE(FOOT(ptr), HF(size, 0));
        WRITE(HEAD(PREV(ptr)), HF(size, 0));
        ptr = PREV(ptr);
    }

    /* if only previous is allocated */
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HEAD(NEXT(ptr)));
        WRITE(HEAD(ptr), HF(size, 0));
        WRITE(FOOT(ptr), HF(size,0));
    }

    /* if neither is */
   else{
       size += GET_SIZE(HEAD(PREV(ptr))) + GET_SIZE(FOOT(NEXT(ptr)));
       WRITE(HEAD(PREV(ptr)), HF(size, 0));
       WRITE(FOOT(NEXT(ptr)), HF(size, 0));
       ptr = PREV(ptr);
    }
   
    if ((trav > (char *)ptr) && (trav < NEXT(ptr))) trav = ptr; //change trav if pointing to coalesced block

    return ptr;
}

/* 
 * *grow_heap - grows the heap size.
 *  - ensures alignment
 *  - adds header/footer for new free block
 *  - moves end header to the end of the grown heap
 *  - coalesces
 */
static void *grow_heap(size_t words) 
{
    size_t size;
    size = (words % 2) ? (words+1) * HFSIZE : words * HFSIZE;

    char *ptr = mem_sbrk(size); //set pointer to start of grown block
    if ((long)ptr == -1) return NULL;
    
    /* Initialize new block's header/footer and end header */
    WRITE(HEAD(ptr), HF(size, 0));
    WRITE(FOOT(ptr), HF(size, 0));
    WRITE(HEAD(NEXT(ptr)), HF(0, 1)); //new end header

    return coalesce(ptr);
}

/* 
 * fit - Find a fit for a block with size bytes
 * - uses next fit method to search for a fit
 * - first traverses the next part of the heap (not already traversed)
 * - next, it traverses the first half
 * - if still no fit, returns NULL, if any then returns pointer to start of fit
 */
static void *fit(size_t adj_size)
{
    /* Next fit search */
    char *origtrav = trav;

    /* search from  trav to end */
    for (; GET_SIZE(HEAD(trav)) > 0; trav = NEXT(trav))
        if (!GET_ALLOC(HEAD(trav)) && (adj_size <= GET_SIZE(HEAD(trav))))
            return trav;

    /* search from start to orig trav */
    for (trav = heapL; trav < origtrav; trav = NEXT(trav))
        if (!GET_ALLOC(HEAD(trav)) && (adj_size <= GET_SIZE(HEAD(trav))))
            return trav;

    return NULL;  /* no fit found */
}

/* 
 * put - Puts size byte block at the free block at ptr, splitting
 * if required.
 */
static void put(void *ptr, size_t adj_size)
{
    size_t csize = GET_SIZE(HEAD(ptr));   

    if ((csize - adj_size) >= (2*DWORD)) { 
        WRITE(HEAD(ptr), HF(adj_size, 1));
        WRITE(FOOT(ptr), HF(adj_size, 1));
        ptr = NEXT(ptr);
        WRITE(HEAD(ptr), HF(csize-adj_size, 0));
        WRITE(FOOT(ptr), HF(csize-adj_size, 0));
    }
    else { 
        WRITE(HEAD(ptr), HF(csize, 1));
        WRITE(FOOT(ptr), HF(csize, 1));
    }
}

/* 
 * mm_init - initialize the malloc package.
 *  - creates a free heap list of size 16 bytes
 *  - adds a start header/footer
 *  - adds an end header
 *  - moves the pointer to the start of the free space
 *  - extends the heap by CHUNKSIZE bytes
 */
int mm_init(void)
{   
    /* Create the initial free heap list */
    if ((heapL = mem_sbrk(4*HFSIZE)) == (void *)-1) return -1;
    WRITE(heapL, 0); //padding
    WRITE(heapL + (1*HFSIZE), HF(DWORD, 1)); //start header
    WRITE(heapL + (2*HFSIZE), HF(DWORD, 1)); //start footer
    WRITE(heapL + (3*HFSIZE), HF(0, 1)); //end header 
    heapL += (2*HFSIZE); //move heapL pointer after start header/footer

    trav = heapL; //set trav to point to heap

    /* Extend the empty heap list CHUNKSIZE bytes */
    if (grow_heap(CHUNKSIZE/HFSIZE) == NULL) return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block of (at least) size bytes
 * - searches for fit of size bytes
 * - if found, puts the block
 * - if not found, grows heap then puts the block
 */
void *mm_malloc(size_t size)
{
    char *ptr;
    size_t adj_size; //adjusted block size
    size_t growsize; //amount to grow heap, if required

    if (heapL == 0) mm_init(); //if no heap list, init
    if (size == 0) return NULL; //if request is useless, return NULL

    if (size <= DWORD) adj_size = 2*DWORD;
    else adj_size = DWORD * ((size + (DWORD) + (DWORD-1)) / DWORD);

    if ((ptr = fit(adj_size)) != NULL){ //if fit
        put(ptr, adj_size); //puts block
        return ptr;
    }
    /* only runs of no fit */
    growsize = ((adj_size) > (CHUNKSIZE) ? (adj_size) : (CHUNKSIZE)); //grows heap by CHUNKSIZE or block size, whichever is larger
    if ((ptr = grow_heap(growsize/HFSIZE)) == NULL) return NULL;
    put(ptr, adj_size); //puts block
    return ptr;
}

/*
 * mm_free - Frees a block.
 *  - checks for bad entry
 *  - gets size of block to be freed
 *  - updates headers/footers so that block is unallocated
 *  - coalesces
 */
void mm_free(void *ptr)
{
    if (ptr == 0) return; //if pointer is NULL, return

    size_t size = GET_SIZE(HEAD(ptr)); //get size of block to free
    if (heapL == 0) mm_init(); //if no heap list, init

    WRITE(HEAD(ptr), HF(size, 0)); //set header to unallocated
    WRITE(FOOT(ptr), HF(size, 0)); //set footer to unallocated 
    coalesce(ptr); //coalesce to optimize
}

/*
 * mm_realloc - reallocates the given area of memory, originally allocated by mm_malloc
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t oldsize;
    void *newptr;

    /* Handles instance where size = 0, just frees */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If old ptr is null, it's just malloc */
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);

    /* If realloc() fails return NULL  */
    if(!newptr) {
        return 0;
    }

    /* Copies old data. */
    oldsize = GET_SIZE(HEAD(ptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Frees old block. */
    mm_free(ptr);

    return newptr;
}