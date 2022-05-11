/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
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
    /* Team name */
    "jungle-week6-1-team",
    /* First member's full name */
    "Seungwoo Son",
    /* First member's email address */
    "@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 교재에 선언된 매크로 및 상수 */
#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<12) /* Extend heap by this amount (bytes) */ // (1<<12) == 2^12 (4096)

#define MAX(x,y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc)) // 816p Block size + (allocated or free) 

#define GET(p) (*(unsigned int*)(p))
#define PUT(p,val) (*(unsigned int*)(p)=(val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char*)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(((char*)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))
static char *heap_listp;
static char *last_allocp;

static void *coalesce(void *bp)
{
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size =  GET_SIZE(HDRP(bp));

  // 이전과 다음 블록이 모두 할당되어 있다.
  // 현재 블록은 할당에서 가용상태로 변경
  if (prev_alloc && next_alloc) { /* CASE 1 */
    return bp;
  }
  // 이전 블록은 할당상태, 다음 블록은 가용상태다.
  // 현재 블록은 다음 블록과 통합
  // 현재 블록과 다음 블록의 풋터는 현재와 다음 블록의 크기를 합한 것으로 갱신
  else if (prev_alloc && !next_alloc) { /* CASE 2 */
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));
  }
  // 이전 블록은 가용상태, 다음 블록은 할당상태다.
  else if (!prev_alloc && next_alloc) { /* CASE 3 */
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  }
  // 이전 블록과 다음 블록 모두 가용상태다.
  else {
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  }
  last_allocp = bp;
  return bp;
}

static void *extend_heap(size_t words)
{
  char *bp;
  size_t size;

  /* Allocate an even number of words to maintain alignment */
  size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
  if ((long)(bp = mem_sbrk(size)) == -1) {
    return NULL;
  }

  /* Initialize free block header/footer and the epilogue header */
  PUT(HDRP(bp), PACK(size, 0));         /*  Free block header   */
  PUT(FTRP(bp), PACK(size, 0));         /*  Free block footer   */
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /*  New epilogue header */

  /* Coalesce if the previous block was free */
  return coalesce(bp);
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  // CSAPP 826 Page.

  // 메모리 시스템에서 4워드를 가져와서 빈 가용 리스트를 만들 수 있도록 이들을 초기화한다.
  if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *) - 1) {
    return -1;
  }

  PUT(heap_listp, 0);                           /*    Alignment padding   */
  PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));  /*    Prologue header     */
  PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));  /*    Prologue footer     */
  PUT(heap_listp + (3*WSIZE), PACK(0, 1));      /*    Epilogue header     */
  // 제일 처음 할당할 블록의 시작 주소는 Prologue 블록 바로 뒤에 온다.
  heap_listp += (2*WSIZE);

  /* Extend the empty heap with a free block of CHUNKSIZE bytes */
  if (extend_heap(CHUNKSIZE/WSIZE) == NULL) {
    return -1;
  }
  // Next-Fit 탐색을 위해서 제일 마지막에 할당한 포인터에 heap_listp를 넣어준다.
  last_allocp = heap_listp;
  return 0;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
  size_t size = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(size, 0));
  PUT(FTRP(bp), PACK(size, 0));
  coalesce(bp);
}

static void *next_fit(size_t asize) {
  void *bp;

  // next-fit으로 탐색
  for(bp = last_allocp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) {
      last_allocp = bp;
      return bp;
    }
  }

  // next-fit으로 찾지 못한다면 extend 전에 first-fit으로 한번 더 탐색
  for(bp = heap_listp; bp != last_allocp; bp = NEXT_BLKP(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
      return bp;
    }
  }

  return NULL;
}

// first-fit 방식
// static void *find_fit(size_t asize) {
//   void *bp;

//   for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
//     if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
//       return bp;
//     }
//   }
//   return NULL;
// }

static void place(void *bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));

  if((csize - asize) >= (2*DSIZE)) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize-asize, 0));
    PUT(FTRP(bp), PACK(csize-asize, 0));
  }
  else {
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
  }
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
  size_t asize;
  size_t extendsize;
  char *bp;

  if (size == 0) {
    return NULL;
  }

  if (size <= DSIZE) {
    asize = 2*DSIZE;
  }
  else {
    // 나보다 큰 가장 가까운 8의 배수를 구하는 과정, 헤더/풋터/패딩 모두 포함하여 블록의 사이즈 지정
    asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); // DSIZE 앞은 header + footer size 
  }

  // find-fit 방식 채택시 54점 / next-fit 방식 채택시 82점
  if ((bp = next_fit(asize)) != NULL) {
    place(bp, asize);
    return bp;
  }

  extendsize = MAX(asize, CHUNKSIZE);
  if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
    return NULL;
  }
  place(bp, asize);
  return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */

void *mm_realloc(void *ptr, size_t size)
{
  void *oldptr = ptr;
  void *newptr;
  size_t copySize;
  
  newptr = mm_malloc(size);
  if (newptr == NULL)
    return NULL;
  copySize = GET_SIZE(HDRP(oldptr));
  if (size < copySize)
    copySize = size;
  memcpy(newptr, oldptr, copySize);
  mm_free(oldptr);
  return newptr;
}
