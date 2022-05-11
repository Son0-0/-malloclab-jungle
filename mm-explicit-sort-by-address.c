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

#define PREV_P(bp) (*(void**)(bp))          /* 가용 블록 리스트에서 현재 블록의 바로 이전 블록을 반환 */
#define NEXT_P(bp) (*(void**)(bp + WSIZE))  /* 가용 블록 리스트에서 현재 블록의 이후 블록을 반환*/

static char *heap_listp;          /* 가용 블록 리스트의 root */
static void list_add(void *);     /* 가용 블록을 블록 리스트에 저장하는 함수 */
static void list_remove(void *);  /* 가용 블록을 블록 리스트에서 제거하는 함수*/
static void *extend_heap(size_t);
void *list_search(void *);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  // CSAPP 826 Page.

  // 메모리 시스템에서 6워드를 가져와서 빈 가용 리스트를 만들 수 있도록 이들을 초기화한다.
  if ((heap_listp = mem_sbrk(6*WSIZE)) == (void *) - 1) {
    return -1;
  }

  PUT(heap_listp, 0);                                 /*    Alignment padding   */
  /*  
  *   Free block 제일 처음 블록을 생성 해주는 작업이다. 첫 블록의 Next를 아래와 같이 설정 해주고
  *   마지막에 추가되는 블록의 Next로 첫 블록의 Payload 주소를 넣어준다면 마지막 블록이라는 것을 나타낼 수 있다.
  */
  PUT(heap_listp + (1*WSIZE), PACK(2*DSIZE, 1));      /*    Prologue Header     */
  PUT(heap_listp + (2*WSIZE), heap_listp+(3*WSIZE));  /*    PREV POINTER        */
  PUT(heap_listp + (3*WSIZE), heap_listp+(2*WSIZE));  /*    NEXT POINTER        */
  PUT(heap_listp + (4*WSIZE), PACK(2*DSIZE, 1));      /*    Prologue Footer     */
  PUT(heap_listp + (5*WSIZE), PACK(0, 1));            /*    Epilogue Header     */
  
  /*
  * 제일 처음 가용 블록의 시작 주소는 Prologue 블록 바로 뒤에 온다.
  * 가용 블록 리스트의 루트를 heap_listp의 Next로 지정한다.
  */
  heap_listp += (2*WSIZE);

  /* Extend the empty heap with a free block of CHUNKSIZE bytes */
  if (extend_heap(CHUNKSIZE/WSIZE) == NULL) { // 원래 CHUNKSIZE/WSIZE
    return -1;
  }
  return 0;
}

static void *coalesce(void *bp)
{
  // 직전 블록의 헤더를 참고하여 allocated 된 상태인지 판단
  size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
  // 직후 블록의 헤더를 참고하여 allocated 된 상태인지 판단
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size =  GET_SIZE(HDRP(bp));

  // 이전과 다음 블록이 모두 할당되어 있다.
  // 현재 블록은 할당에서 가용상태로 변경
  if (prev_alloc && next_alloc) { /* CASE 1 */
    list_add(bp); // free block이 생기는 것이기 때문에 free block 리스트에 이어준다.
    return bp;
  }
  // 이전 블록은 할당상태, 다음 블록은 가용상태다.
  // 현재 블록은 다음 블록과 통합
  // 현재 블록과 다음 블록의 풋터는 현재와 다음 블록의 크기를 합한 것으로 갱신
  else if (prev_alloc && !next_alloc) { /* CASE 2 */
    list_remove(NEXT_BLKP(bp)); // 다음 블록이 현재 블록과 합쳐지기 때문에 free block 리스트에서 다음 블록을 제거한다.
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));
  }
  // 이전 블록은 가용상태, 다음 블록은 할당상태다.
  else if (!prev_alloc && next_alloc) { /* CASE 3 */
    list_remove(PREV_BLKP(bp)); // 이전 블록이 현재 블록과 합쳐지기 때문에 free block 리스트에서 이전 블록을 제거한다.
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  }
  // 이전 블록과 다음 블록 모두 가용상태다.
  else {
    // 케이스 2, 3 모두 해당하기 때문에 이전, 이후 블록들을 free block 리스트에서 제거 한다.
    list_remove(PREV_BLKP(bp)); 
    list_remove(NEXT_BLKP(bp));
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  }
  // 합쳐진 free block을 리스트에 추가한다.
  list_add(bp);
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
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
  size_t size = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(size, 0));
  PUT(FTRP(bp), PACK(size, 0));
  coalesce(bp);
}

// first-fit 방식
static void *find_fit(size_t asize) {
  // bp는 가용 블록 리스트의 root
  // heap_listp의 NEXT는 항상 root 블록을 가리키고 있다.
  void *bp = NEXT_P(heap_listp);

  // bp의 NEXT를 탐색하다가 해당 블록의 헤더가 할당 상태라면 종료한다.
  // 마지막 블록은 heap_listp의 payload 주소 부분을 가리키고 있으며 이 블록은 항상 할당 상태에 있다.
  for(bp; !GET_ALLOC(HDRP(bp)); bp = NEXT_P(bp)) {
    // printf("free list cur: %d / next : %d\n", GET(bp), GET(NEXT_P(bp)));
    if (asize <= GET_SIZE(HDRP(bp))) {
      return bp;
    }
  }
  return NULL;
}

static void place(void *bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));
  list_remove(bp);
  if((csize - asize) >= (2*DSIZE)) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize-asize, 0));
    PUT(FTRP(bp), PACK(csize-asize, 0));
    list_add(bp);
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

  if ((bp = find_fit(asize)) != NULL) {
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

void *list_search(void *bp) {
  void *temp = NEXT_P(heap_listp);
  void *cur = NULL;

  for(temp; !GET_ALLOC(HDRP(temp)); temp = NEXT_P(temp)) {
    cur = temp;
    if (bp < temp) {
      return temp;
    }
  }

  if (cur && cur < bp) {
    return cur;
  }
  return heap_listp;
}

static void list_add(void *bp) {

  if (NEXT_P(heap_listp) == heap_listp) {
    NEXT_P(bp) = NEXT_P(heap_listp);
    PREV_P(bp) = heap_listp;
    PREV_P(NEXT_P(heap_listp)) = bp;
    NEXT_P(heap_listp) = bp;
  } else {
    void *target_bp = list_search(bp);
    NEXT_P(PREV_P(target_bp)) = bp;
    PREV_P(bp) = PREV_P(target_bp);
    PREV_P(target_bp) = bp;
    NEXT_P(bp) = target_bp;
  }

  // 주소 순으로 정렬되어 있는지 출력
  // void *t = NEXT_P(heap_listp);
  // int cnt = 0;
  // for (t; !GET_ALLOC(HDRP(t)); t = NEXT_P(t)) {
  //   printf("%d: %x\n", cnt++, t);
  // }

}

static void list_remove(void *bp) {
  NEXT_P(PREV_P(bp)) = NEXT_P(bp);
  PREV_P(NEXT_P(bp)) = PREV_P(bp);
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
