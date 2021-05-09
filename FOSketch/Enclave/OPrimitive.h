#ifndef MEASUREMENT_OSORT_H
#define MEASUREMENT_OSORT_H
//typedef uint32_t bid_t;
//#include <vector>
#include "../../Common/CommonUtil.h"
#include <iostream>
#include <assert.h>

using namespace std;

void * malloc_align(size_t size, size_t align, uint8_t ** ppDataAlign)
{
    void *pData = malloc(size+align-1); //cache line align
    assert(pData != NULL);
    uintptr_t mask = ~(uintptr_t)(align - 1);
    uint8_t *pDataAlign = (uint8_t *)(((uintptr_t)pData+align-1) & mask);
    assert((align & (align - 1)) == 0);
    //printf("Address: %x, Aligned address: %x; align: %d\n", (uint8_t *)pData, (uint8_t *)pDataAlign, align);
    *ppDataAlign = pDataAlign;
    //*ppDataAlign = (uint8_t *)pData;
    return pData;
}

inline uint64_t OMove(uint64_t x, uint64_t y, int cond){
    uint64_t val;
    __asm__ __volatile__ (
        "mov %1,%%r13\n"
        "mov %2,%%r14\n"
        "mov %3, %%eax\n"
        "test %%eax, %%eax\n"
        "cmovnz %%r13, %%r14\n"
        "mov %%r14, %0\n"
        :"=m"(val)
        :"m"(x),"m"(y),"m"(cond)
    );
    return val;
}

//The following second set of OMoveEx have differenct time consumption, i don't know why.
//As this one is faster, may be slower.
//May be the codes of the following second set of OMoveEx are too long...
//I give this choice to you.
//Note that the second set of OMoveEx are easy to read and support every size.

#define OMOVEEX_FASTER 1

#ifdef OMOVEEX_FASTER
inline void OMoveEx(uint8_t * px, uint8_t* py, int cond, uint32_t size=32){
    if(size < 16){
        return;
    }
    if(size == 16){
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%xmm14\n"
            "mov %0,%%r14\n"
            "VMOVDQU (%%r14),%%xmm15\n"
            "mov %2, %%eax\n"
            "test %%eax, %%eax\n"
            "je L0\n"
            "VMOVDQU %%xmm14,(%%r14)\n"
            "jmp E0\n"
            "L0: VMOVDQU %%xmm15,(%%r14)\n"
            "E0: nop\n"
            :"=m"(px)
            :"m"(py),"m"(cond)
        );
        return;
    }
    uint8_t * x = px;
    uint8_t* y = py;
    for(int i=0; i < size; i+=32){
        x = px + i;
        y = py + i;
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%ymm14\n"
            "mov %0,%%r14\n"
            "VMOVDQU (%%r14),%%ymm15\n"
            "mov %2, %%eax\n"
            "test %%eax, %%eax\n"
            "je L1\n"
            "VMOVDQU %%ymm14,(%%r14)\n"
            "jmp E1\n"
            "L1: VMOVDQU %%ymm15,(%%r14)\n"
            "E1: nop\n"
            :"=m"(x)
            :"m"(y),"m"(cond)
        );
    }
}

inline void OMoveEx(uint8_t* pz, uint8_t * px, uint8_t* py, int cond, uint32_t size=32){
    if(size < 16){
        return;
    }
    if(size == 16){
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%xmm14\n"
            "mov %2,%%r14\n"
            "VMOVDQU (%%r14),%%xmm15\n"
            "mov %0,%%r14\n"
            "mov %3, %%eax\n"
            "test %%eax, %%eax\n"
            "je L2\n"
            "VMOVDQU %%xmm14,(%%r14)\n"
            "jmp E2\n"
            "L2: VMOVDQU %%xmm15,(%%r14)\n"
            "E2: nop\n"
            :"=m"(pz)
            :"m"(px),"m"(py),"m"(cond)
        );
        return;
    }
    uint8_t * x = px;
    uint8_t* y = py;
    uint8_t* z = pz;
    for(int i=0; i < size; i+=32){
        x = px + i;
        y = py + i;
        z = pz + i;
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%ymm14\n"
            "mov %2,%%r14\n"
            "VMOVDQU (%%r14),%%ymm15\n"
            "mov %0,%%r14\n"
            "mov %3, %%eax\n"
            "test %%eax, %%eax\n"
            "je L3\n"
            "VMOVDQU %%ymm14,(%%r14)\n"
            "jmp E3\n"
            "L3: VMOVDQU %%ymm15,(%%r14)\n"
            "E3: nop\n"
            :"=m"(z)
            :"m"(x),"m"(y),"m"(cond)
        );
    }
}
#else
//There has some primitives following have one very near jump,
//I think it is oblivious.
//For explanation, proofs can be found in the NDSS'19 conference paper:
//"OBFSCURO: A Commodity Obfuscation Engine on Intel SGX"
//If you dont trust this near jump,
//you can use for-loop OMove() instead.
//TODO: Need work to make it support any size, may meet performance slowdonw.
//Can execute like FOSketch.Init_OCU()
// can can any level < 32 (sizeof(size).)
//NONONO, my test indicates this assumption is not corrent: => OMoveEx(px,py,cond,size) may be equally the same as OMoveEx(px,px,py,cond,size).
inline void OMoveEx(uint8_t * px, uint8_t* py, int cond, uint32_t size=32){
    uint32_t curLength = size & 0xFFFFFFE0;
    uint8_t * x = px;
    uint8_t* y = py;
    //for multiple of 32
    for(int i=0; i < curLength; i+=32){
        x = px + i;
        y = py + i;
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%ymm14\n"
            "mov %0,%%r14\n"
            "VMOVDQU (%%r14),%%ymm15\n"
            "mov %2, %%eax\n"
            "test %%eax, %%eax\n"
            "je L0\n"
            "VMOVDQU %%ymm14,(%%r14)\n"
            "jmp E0\n"
            "L0: VMOVDQU %%ymm15,(%%r14)\n"
            "E0: nop\n"
            :"=m"(x)
            :"m"(y),"m"(cond)
        );
    }
    if(size == curLength) return;
    //for 16
    if(size && (1<<4)){
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%xmm14\n"
            "mov %0,%%r14\n"
            "VMOVDQU (%%r14),%%xmm15\n"
            "mov %2, %%eax\n"
            "test %%eax, %%eax\n"
            "je L1\n"
            "VMOVDQU %%xmm14,(%%r14)\n"
            "jmp E1\n"
            "L1: VMOVDQU %%xmm15,(%%r14)\n"
            "E1: nop\n"
            :"=m"(x)
            :"m"(y),"m"(cond)
        );
        x = x+16;
        y = y+16;
        curLength += 16;
    }
    if(size == curLength) return;

    //for 8
    if(size && (1<<3)){
        uint64_t a8 = *(uint64_t *)x;
        uint64_t b8 = *(uint64_t *)y;
        *(uint64_t *)x = OMove(a8, b8, cond);
        x = x+8;
        y = y+8;
    }

    //for 4
    if(size && (1<<2)){
        uint32_t a4 = *(uint32_t *)x;
        uint32_t b4 = *(uint32_t *)y;
        *(uint32_t *)x = OMove(a4, b4, cond);
        x = x+4;
        y = y+4;
    }

    //for 2
    if(size && (1<<1)){
        uint16_t a2 = *(uint16_t *)x;
        uint16_t b2 = *(uint16_t *)y;
        *(uint16_t *)x = OMove(a2, b2, cond);
        x = x+2;
        y = y+2;
    }

    //for 1
    if(size && (1<<0)){
        uint8_t a1 = *(uint8_t *)x;
        uint8_t b1 = *(uint8_t *)y;
        *x = OMove(a1, b1, cond);
        x = x+1;
        y = y+1;
    }
    assert(x==(px+size));
}

inline void OMoveEx(uint8_t* pz, uint8_t * px, uint8_t* py, int cond, uint32_t size=32){
    uint32_t curLength = size & 0xFFFFFFE0;
    uint8_t * x = px;
    uint8_t* y = py;
    uint8_t* z = pz;
    //for multiple of 32
    for(int i=0; i < curLength; i+=32){
        x = px + i;
        y = py + i;
        z = pz + i;
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%ymm14\n"
            "mov %2,%%r14\n"
            "VMOVDQU (%%r14),%%ymm15\n"
            "mov %0,%%r14\n"
            "mov %3, %%eax\n"
            "test %%eax, %%eax\n"
            "je L2\n"
            "VMOVDQU %%ymm14,(%%r14)\n"
            "jmp E2\n"
            "L2: VMOVDQU %%ymm15,(%%r14)\n"
            "E2: nop\n"
            :"=m"(z)
            :"m"(x),"m"(y),"m"(cond)
        );
    }
    if(size == curLength) return;
    //for 16
    if(size && (1<<4)){
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%xmm14\n"
            "mov %2,%%r14\n"
            "VMOVDQU (%%r14),%%xmm15\n"
            "mov %0,%%r14\n"
            "mov %3, %%eax\n"
            "test %%eax, %%eax\n"
            "je L3\n"
            "VMOVDQU %%xmm14,(%%r14)\n"
            "jmp E3\n"
            "L3: VMOVDQU %%xmm15,(%%r14)\n"
            "E3: nop\n"
            :"=m"(z)
            :"m"(x),"m"(y),"m"(cond)
        );
        x = x+16;
        y = y+16;
        z = z+16;
        curLength += 16;
    }
    if(size == curLength) return;

    //for 8
    if(size && (1<<3)){
        uint64_t a8 = *(uint64_t *)x;
        uint64_t b8 = *(uint64_t *)y;
        uint64_t c8 = *(uint64_t *)z;
        *(uint64_t *)z = OMove(a8, b8, cond);
        x = x+8;
        y = y+8;
        z = z+8;
    }

    //for 4
    if(size && (1<<2)){
        uint32_t a4 = *(uint32_t *)x;
        uint32_t b4 = *(uint32_t *)y;
        uint32_t c4 = *(uint32_t *)z;
        *(uint32_t *)z = OMove(a4, b4, cond);
        x = x+4;
        y = y+4;
        z = z+4;
    }

    //for 2
    if(size && (1<<1)){
        uint16_t a2 = *(uint16_t *)x;
        uint16_t b2 = *(uint16_t *)y;
        uint16_t c2 = *(uint16_t *)z;
        *(uint16_t *)x = OMove(a2, b2, cond);
        x = x+2;
        y = y+2;
        z = z+2;
    }

    //for 1
    if(size && (1<<0)){
        uint8_t a1 = *(uint8_t *)x;
        uint8_t b1 = *(uint8_t *)y;
        uint8_t c1 = *(uint8_t *)z;
        *z = OMove(a1, b1, cond);
        x = x+1;
        y = y+1;
        y = z+1;
    }
    assert(x==(px+size));
}
#endif //OMOVEEX_FASTER

inline uint32_t OGet(uint16_t index, uint32_t *pArry, uint16_t size=128){
    //each time, 512 Bytes!
    unsigned cacheMask = 1<<31;
    unsigned indexArr[8];
    unsigned res[8];
    unsigned ret = 0;
    uint8_t subInd = index % 16;
    uint8_t groupInd = index / 16;
    uint16_t epoch = ceil(1.0*size /128);
    for(int i=0; i<epoch; i++){
        for(int j=0;j<8;j++){
            indexArr[j] = 128*i + subInd + j*16;
        }
        __asm__ __volatile__ (
            "VMOVDQU %2,%%ymm14\n"
            "vpbroadcastd %3,%%ymm13\n"
            "mov %1, %%r14\n"
            "vpgatherdd %%ymm13,(%%r14,%%ymm14,4),%%ymm15\n"
            "VMOVDQU %%ymm15,%0\n"
            :"=m"(res)
            :"m"(pArry),"m"(indexArr),"m"(cacheMask)
        );
        for(int j=0; j<8; j++){
            ret += OMove(res[j], 0, (j+i*8)==groupInd);
        }
    }
    return ret;
}

inline void OAdd256(uint32_t val, uint16_t index, uint32_t *pArry){
    //each time, 32 Bytes!
    uint8_t subInd = index % 8;
    uint8_t groupInd = index / 8;
    uint16_t epoch = 256 / 8;
    int cond = 0;
    unsigned add0[8];
    unsigned add1[8];
    for(int i =0; i<8; i++){
        add0[i] = 0;
        add1[i] = OMove(val, 0, i == subInd);
    }
    __asm__ __volatile__ (
        "VMOVDQU %0,%%ymm13\n"
        "VMOVDQU %1,%%ymm14\n"
        ::"m"(add0),"m"(add1)
    );
    uint32_t *curP = NULL;
    for(int i=0; i<epoch; i++){
        curP = pArry + 8*i;
        cond = i == groupInd;
        //We assume this is oblivious, as all happens inside the CPU.
        __asm__ __volatile__ (
            "mov %0,%%r14\n"
            "VMOVDQU (%%r14),%%ymm15\n"
            "mov %1, %%eax\n"
            "test %%eax, %%eax\n"
            "je L4\n"
            "VPADDD %%ymm14, %%ymm15,%%ymm12\n"
            "jmp E4\n"
            "L4: VPADDD %%ymm13, %%ymm15,%%ymm12\n"
            "E4: VMOVDQU %%ymm12,(%%r14)\n"
            ::"m"(curP),"m"(cond)
        );
    }
}

//Using YMM14/YMM15 as a tmp variable may cause event unexpected in the enclave!
//I don't know why.
//Use OMaxSetMemCL() instead.
//Note that, OMaxSetYmm1415CL executed well in the normal world.
inline void OMaxSetYmm1415CL(uint8_t *px, uint8_t cond){
    //each time, 32 Bytes!
    //We assume this is oblivious, as all happens inside the CPU. code read in cache line
    //If it is not oblivious, use OMaxCLYmm1415 and OMoveEx primitives.
    __asm__ __volatile__ (
        "mov %0,%%r14\n"
        "VMOVDQU (%%r14),%%ymm13\n"
        "vpmaxub %%ymm13, %%ymm14, %%ymm14\n"
        "add $32,%%r14\n"
        "VMOVDQU (%%r14),%%ymm13\n"
        "vpmaxub %%ymm13, %%ymm15, %%ymm15\n"
        "mov %1, %%eax\n"
        "test %%eax, %%eax\n"
        "je L5\n"
        "VMOVDQU (%%r14), %%ymm15\n"
        "add $-32,%%r14\n"
        "VMOVDQU (%%r14), %%ymm14\n"
        "jmp E5\n"
        "L5: VMOVDQU %%ymm15, (%%r14)\n"
        "add $-32,%%r14\n"
        "VMOVDQU %%ymm14, (%%r14)\n"
        "E5: nop\n"
        ::"m"(px),"m"(cond)
    );
}

inline void OMaxSetMemCL(uint8_t *px, uint8_t *py, uint8_t cond){
    //each time, 32 Bytes!
    //We assume this is oblivious, as all happens inside the CPU. code read in cache line
    //If it is not oblivious, use OMaxCLYmm1415 and OMoveEx primitives.
        __asm__ __volatile__ (
        "mov %1,%%r14\n"
        "VMOVDQU (%%r14),%%ymm14\n"
        "add $32,%%r14\n"
        "VMOVDQU (%%r14),%%ymm15\n"
        "mov %0,%%r14\n"
        "VMOVDQU (%%r14),%%ymm13\n"
        "vpmaxub %%ymm13, %%ymm14, %%ymm14\n"
        "add $32,%%r14\n"
        "VMOVDQU (%%r14),%%ymm13\n"
        "vpmaxub %%ymm13, %%ymm15, %%ymm15\n"
        "mov %2, %%eax\n"
        "test %%eax, %%eax\n"
        "je L6\n"
        //"mov %0,%%r14\n"
        "VMOVDQU (%%r14),%%ymm15\n"
        "add $-32,%%r14\n"
        "VMOVDQU (%%r14),%%ymm14\n"
        "L6: mov %1,%%r14\n"
        "VMOVDQU %%ymm14, (%%r14)\n"
        "add $32,%%r14\n"
        "VMOVDQU %%ymm15, (%%r14)\n"
        "mov %0,%%r14\n"
        "VMOVDQU %%ymm14, (%%r14)\n"
        "add $32,%%r14\n"
        "VMOVDQU %%ymm15, (%%r14)\n"
        :"+m"(px),"+m"(py)
        :"m"(cond)
    );
}

inline void OMaxCLYmm1415(uint8_t *px){
    //each time, 32 Bytes!
    //We assume this is oblivious, as all happens inside the CPU. code read in cache line
    __asm__ __volatile__ (
        "mov %0,%%r14\n"
        "VMOVDQU (%%r14),%%ymm13\n"
        "vpmaxub %%ymm13, %%ymm14, %%ymm14\n"
        "add $32,%%r14\n"
        "VMOVDQU (%%r14),%%ymm13\n"
        "vpmaxub %%ymm13, %%ymm15, %%ymm15\n"
        ::"m"(px)
    );
}

//#define SORTOR_CMP_COUNTER
typedef bool (*Greater)(uint8_t *, uint8_t *);
class OddEvenMergeSorter
{
private:
    uint8_t *List;
    bool isASCE;
    size_t sizeElem;
    Greater greater;
    typedef void (OddEvenMergeSorter::* FOSwap)(int i, int j, bool cond);
    FOSwap OSwap;
    // sorting direction:
    const static bool ASCENDING=true, DESCENDING=false;
#ifdef SORTOR_CMP_COUNTER
    size_t cnt = 0;
#endif

public:
    OddEvenMergeSorter(Greater grt, uint16_t sizeBlock, bool asce = true){
        greater = grt;
        sizeElem = sizeBlock;
        isASCE = asce;
        switch (sizeBlock) {
        case 1: break;
        case 2: break;
        case 4: OSwap = &OddEvenMergeSorter::OSwap4; break;
        case 8: OSwap = &OddEvenMergeSorter::OSwap8; break;
        case 10: printf("Sorry, Can't support!\n"); break;
        default: OSwap = &OddEvenMergeSorter::OSwapEx; break;
        }
    }
    void sort(uint8_t *pData, uint32_t num)
    {
        if(num < 2) return;
        if(!testPo2(num)){
            printf("Error: Sort not workd\n");
            return;
        }
        List = pData;
#ifdef SORTOR_CMP_COUNTER
        cnt = 0;
#endif
        oddEvenMergeSort(0, num);
#ifdef SORTOR_CMP_COUNTER
        printf("OEcounter: %lu", cnt);
#endif
        List = NULL;
    }

    void merge(uint8_t *pData, uint32_t num){
        if(num < 2) return;
        if(!testPo2(num)){
            printf("Error: Sort not workd\n");
            return;
        }
        List = pData;
#ifdef SORTOR_CMP_COUNTER
        cnt = 0;
#endif

        oddEvenMerge(0, num, 1);
        List = NULL;
#ifdef SORTOR_CMP_COUNTER
        printf("OEMergecounter: %lu", cnt);
#endif
    }

    /** sorts a piece of length n of the array
     *  starting at position lo
     */
private:
    void oddEvenMergeSort(int lo, int n)
    {
        if (n>1)
        {
            int m=n/2;
            oddEvenMergeSort(lo, m);
            oddEvenMergeSort(lo+m, m);
            oddEvenMerge(lo, n, 1);
        }
    }

    /** lo is the starting position and
     *  n is the length of the piece to be merged,
     *  r is the distance of the elements to be compared
     */
    void oddEvenMerge(int lo, int n, int r)
    {
        int m=r*2;
        if (m<n)
        {
            oddEvenMerge(lo, n, m);      // even subsequence
            oddEvenMerge(lo+r, n, m);    // odd subsequence
            for (int i=lo+r; i+r<lo+n; i+=m)
                compare(i, i+r);
        }
        else
            compare(lo, lo+r);
    }

    void compare(int i, int j)
    {
#ifdef SORTOR_CMP_COUNTER
        cnt++;
#endif
        uint8_t cond = greater(List+i*sizeElem,List+j*sizeElem);
        (this->*OSwap)(i,j,cond);
    }

    void OSwap4(int i, int j, bool cond){
        uint32_t tmp;
        tmp = OMove(((uint32_t *)List)[i], ((uint32_t *)List)[j], cond);
        ((uint32_t *)List)[i] = OMove(((uint32_t *)List)[j], ((uint32_t *)List)[i], cond);
        ((uint32_t *)List)[j] = tmp;
    }

    void OSwap8(int i, int j, bool cond){
        uint64_t tmp;
        tmp = OMove(((uint64_t *)List)[i], ((uint64_t *)List)[j], cond);
        ((uint64_t *)List)[i] = OMove(((uint64_t *)List)[j], ((uint64_t *)List)[i], cond);
        ((uint64_t *)List)[j] = tmp;
    }

    void OSwapEx(int i, int j, bool cond){
        uint8_t tmp[sizeElem];
        OMoveEx(tmp, List+i*sizeElem, List+j*sizeElem, cond, sizeElem);
        //OMoveEx(List+i*sizeElem, List+j*sizeElem, List+i*sizeElem, cond,sizeElem);
        OMoveEx(List+i*sizeElem, List+j*sizeElem, cond, sizeElem);
        memcpy(List+j*sizeElem, tmp, sizeElem);
    }
};

class BitonicSorter
{
private:
    int numElem;
    uint8_t *List;
    bool isASCE;
    size_t sizeElem;
    Greater greater;
    typedef void (BitonicSorter::* FOSwap)(int i, int j, bool cond);
    FOSwap OSwap;
    // sorting direction:
    const static bool ASCENDING=true, DESCENDING=false;
#ifdef SORTOR_CMP_COUNTER
    size_t cnt = 0;
#endif

public:
    BitonicSorter(Greater grt, uint16_t sizeBlock, bool asce = true){
        greater = grt;
        sizeElem = sizeBlock;
        isASCE = asce;
        switch (sizeBlock) {
        case 1: break;
        case 2: break;
        case 4: OSwap = &BitonicSorter::OSwap4; break;
        case 8: OSwap = &BitonicSorter::OSwap8; break;
        case 10: printf("Sorry, Can't support!\n"); break;
        default: OSwap = &BitonicSorter::OSwapEx; break;
        }
    }
    void sort(uint8_t *pData, uint32_t num)
    {
        if(num < 2) return;

        // uint8_t needMalloc = 1;
        List = pData;
        numElem = num;

        // uint8_t level = ceil(log2(num));
        // numElem = 1 << level;

        // if(numElem == num){
        //     needMalloc = false;
        //     //printf("Don't need to resize! %d is a power of 2\n", num);
        // }else{
        //     printf("Need to resize! %u => %u\n", num, numElem);
        // }
        // void *tmp_data = NULL;
        // if(needMalloc){
        //     tmp_data = malloc_align(sizeElem*numElem, sizeElem, &List);
        //     assert(tmp_data != NULL);
        //     memcpy(List, pData, num*sizeElem);
        //     memset(List+num*sizeElem, isASCE ? ((1<<8)-1) : 0, (numElem-num)*sizeElem);
        // }
#ifdef SORTOR_CMP_COUNTER
        cnt = 0;
#endif
        bitonicSort(0, numElem, ASCENDING);

        // if(needMalloc){
        //     memcpy(pData, List, num*sizeElem);
        //     free(tmp_data);
        //     tmp_data = NULL;
        // }
        List = NULL;
#ifdef SORTOR_CMP_COUNTER
        printf("BScounter: %lu", cnt);
#endif
    }

    void merge(uint8_t *pData, uint32_t num){
        if(num < 2) return;

        uint8_t needMalloc = 1;
        List = pData;
        numElem = num;

        // uint8_t level = ceil(log2(num));
        // numElem = 1 << level;

        // if(numElem == num){
        //     needMalloc = false;
        //     //printf("Don't need to resize! %d is a power of 2\n", num);
        // }else{
        //     printf("Need to resize! %u => %u\n", num, numElem);
        // }
        // void *tmp_data = NULL;
        // if(needMalloc){
        //     tmp_data = malloc_align(sizeElem*numElem, sizeElem, &List);
        //     assert(tmp_data != NULL);
        //     memcpy(List, pData, num*sizeElem);
        //     memset(List+num*sizeElem, isASCE ? ((1<<8)-1) : 0, (numElem-num)*sizeElem);
        // }
#ifdef SORTOR_CMP_COUNTER
        cnt = 0;
#endif
        bitonicMerge(0, numElem, ASCENDING);

        // if(needMalloc){
        //     memcpy(pData, List, num*sizeElem);
        //     free(tmp_data);
        //     tmp_data = NULL;
        // }
        List = NULL;
#ifdef SORTOR_CMP_COUNTER
        printf("BSmergecounter: %lu", cnt);
#endif
    }

private:
    void bitonicSort(int lo, int n, bool dir)
    {
        if (n>1)
        {
            int m=n/2;
            bitonicSort(lo, m, !dir);
            bitonicSort(lo+m, n-m, dir);
            bitonicMerge(lo, n, dir);
        }
    }

    void bitonicMerge(int lo, int n, bool dir)
    {
        if (n>1)
        {
            // int m=n/2;
            int m=greatestPowerOfTwoLessThan(n);
            for (int i=lo; i<lo+n-m; i++)
                compare(i, i+m, dir);
            bitonicMerge(lo, m, dir);
            bitonicMerge(lo+m, n-m, dir);
        }
    }

    void compare(int i, int j, bool dir)
    {
#ifdef SORTOR_CMP_COUNTER
        cnt++;
#endif
        uint8_t cond = (dir==greater(List+i*sizeElem,List+j*sizeElem));
        (this->*OSwap)(i,j,cond);
    }

    void OSwap4(int i, int j, bool cond){
        uint32_t tmp;
        tmp = OMove(((uint32_t *)List)[i], ((uint32_t *)List)[j], cond);
        ((uint32_t *)List)[i] = OMove(((uint32_t *)List)[j], ((uint32_t *)List)[i], cond);
        ((uint32_t *)List)[j] = tmp;
    }

    void OSwap8(int i, int j, bool cond){
        uint64_t tmp;
        tmp = OMove(((uint64_t *)List)[i], ((uint64_t *)List)[j], cond);
        ((uint64_t *)List)[i] = OMove(((uint64_t *)List)[j], ((uint64_t *)List)[i], cond);
        ((uint64_t *)List)[j] = tmp;
    }

    void OSwapEx(int i, int j, bool cond){
        uint8_t tmp[sizeElem];
        OMoveEx(tmp, List+i*sizeElem, List+j*sizeElem, cond, sizeElem);
        //OMoveEx(List+i*sizeElem, List+j*sizeElem, List+i*sizeElem, cond,sizeElem);
        OMoveEx(List+i*sizeElem, List+j*sizeElem, cond, sizeElem);
        memcpy(List+j*sizeElem, tmp, sizeElem);
    }

    // n>=2  and  n<=Integer.MAX_VALUE
    int greatestPowerOfTwoLessThan(int n)
    {
        int k=1;
        while (k>0 && k<n)
            k=k<<1;
        return k>>1;
    }
};

class BitonicSorter_CDist
{
private:
    size_t size;
    CDist *data;
    const static bool ASCENDING=true, DESCENDING=false;

public:
    void sort(CDist *data, size_t size)
    {
        uint8_t level = ceil(log2(size));
        size_t reSize = 1 << level;
        CDist *pData = (CDist *)malloc(reSize*sizeof(CDist));
        assert(pData != NULL);
        memcpy(pData, data, size*sizeof(CDist));
        memset(pData+size, (numeric_limits<uint64_t>::max()), (reSize-size)*sizeof(CDist));
        // for(int i=0; i<reSize; i++){
        //     printf("(%d %d) ", pData[i].size, pData[i].count);
        // }
        this->data = pData;
        bitonicSort(0, reSize, ASCENDING);
        memcpy(data, pData, size*sizeof(CDist));
        free(pData);
        pData = NULL;
        this->data = NULL;


    }

private:
    void bitonicSort(int lo, int n, bool dir)
    {
        if (n>1)
        {
            int m=n/2;
            bitonicSort(lo, m, ASCENDING);
            bitonicSort(lo+m, m, DESCENDING);
            bitonicMerge(lo, n, dir);
        }
    }

    void bitonicMerge(int lo, int n, bool dir)
    {
        if (n>1)
        {
            int m=n/2;
            for (int i=lo; i<lo+m; i++)
                compare(i, i+m, dir);
            bitonicMerge(lo, m, dir);
            bitonicMerge(lo+m, m, dir);
        }
    }

    void compare(int i, int j, bool dir)
    {
        uint8_t cond = (dir==(data[i].size > data[j].size));
        OSwap(i,j,cond);
    }

    void OSwap(int i, int j, uint8_t cond)
    {
        CDist tmp;
        *(uint64_t *)&tmp = OMove(*(uint64_t *)&data[i], *(uint64_t *)&data[j], cond);
        *(uint64_t *)&data[i] = OMove(*(uint64_t *)&data[j], *(uint64_t *)&data[i], cond);;
        data[j] = tmp;
    }

};

class BitonicSorter_8
{
private:
    size_t size;
    uint8_t *data;
    const static bool ASCENDING=true, DESCENDING=false;

public:
    void sort(uint8_t *data, size_t size)
    {
        //this->data=data;
        uint8_t level = ceil(log2(size));
        size_t reSize = 1 << level;
        uint8_t *pData = (uint8_t *)malloc(reSize);
        assert(pData != NULL);
        memcpy(pData, data, size);
        memset(pData+size, (numeric_limits<uint8_t>::max()), reSize-size);
        this->data = pData;
        bitonicSort(0, reSize, ASCENDING);
        memcpy(data, pData, size);
        free(pData);
        pData = NULL;
        this->data = NULL;
    }

private:
    void bitonicSort(int lo, int n, bool dir)
    {
        if (n>1)
        {
            int m=n/2;
            bitonicSort(lo, m, ASCENDING);
            bitonicSort(lo+m, m, DESCENDING);
            bitonicMerge(lo, n, dir);
        }
    }

    void bitonicMerge(int lo, int n, bool dir)
    {
        if (n>1)
        {
            int m=n/2;
            for (int i=lo; i<lo+m; i++)
                compare(i, i+m, dir);
            bitonicMerge(lo, m, dir);
            bitonicMerge(lo+m, m, dir);
        }
    }

    void compare(int i, int j, bool dir)
    {
        uint8_t cond = (dir==(data[i] > data[j]));
        OSwap(i,j,cond);
    }

    void OSwap(int i, int j, uint8_t cond)
    {
        uint8_t tmp;
        tmp = OMove(data[i], data[j], cond);
        data[i] = OMove(data[j], data[i], cond);
        data[j] = tmp;
    }

};

void ODelta(Item *L1, uint32_t size1, Item *L2, uint32_t size2, float gamm){
    uint8_t Match[size2], isMatch;
    for(int i=0; i<size2; i++){
        Match[i] = 0;
    }
    // printf("BL1:");
    // for(int i=0; i< size1; i++){
    //     printf("(%x, %d)", L1[i].flowID, L1[i].size);
    // }
    // printf("BL2:");
    // for(int i=0; i< size2; i++){
    //     printf("(%x, %d)", L2[i].flowID, L2[i].size);
    // }

    Item dummy = {.flowID = DUMMY_FLOW, .size = 0};
    uint64_t tmp;
    for(int i=0; i<size2; i++){
        for(int j=0; j<size1; j++){
            isMatch = (L1[j].flowID == L2[i].flowID);
            L1[j].size = OMove(abs((int)L1[j].size - (int)L2[i].size), L1[j].size, isMatch);
            tmp = OMove(*(uint64_t *)&dummy, *(uint64_t *)&L1[j], isMatch && (L1[j].size < gamm));
            L1[j] = *(Item *)&tmp;
            Match[i] = OMove(isMatch, Match[i], isMatch);
        }
    }
    for(int i=0; i < size2; i++){
        tmp = OMove(*(uint64_t *)&dummy, *(uint64_t *)&L2[i], Match[i]);
        L2[i] = *(Item *)&tmp;
    }
    // printf("AL1:");
    // for(int i=0; i< size1; i++){
    //     printf("(%x, %d)", L1[i].flowID, L1[i].size);
    // }
    // printf("AL2:");
    // for(int i=0; i< size2; i++){
    //     printf("(%x, %d)", L2[i].flowID, L2[i].size);
    // }
}

inline bool greater_CDist(uint8_t *pi, uint8_t *pj){
    return (*(CDist*)pi).size > (*(CDist*)pj).size;
}

//The OrdArry is in a descending order, the countDist is ascending ordered by size.
void OItemCountDist_DESC(const Item *OrdArry, CDist * countDist, size_t length){
    uint32_t curCount = 0;
    uint8_t cond = 1;
    for(int i=0; i<length-1; i++){
        curCount = OMove(1, curCount+1, cond);
        countDist[i].count = curCount;
        cond = OrdArry[i+1].size < OrdArry[i].size;
        countDist[i].size = OMove(OrdArry[i].size, MAX_SIZE, cond);
    }
    countDist[length-1].size = OrdArry[length-1].size;
    countDist[length-1].count = OMove(1, curCount+1, cond);

    if(testPo2(length)){
        OddEvenMergeSorter oe(&greater_CDist, sizeof(CDist));
        oe.sort((uint8_t *)countDist, length);
    }else{
        BitonicSorter bs(&greater_CDist, sizeof(CDist));
        bs.sort((uint8_t *)countDist, length);
    }
    // BitonicSorter bs(&greater_CDist, sizeof(CDist));
    // bs.sort((uint8_t *)countDist, length);
    // //NOTE: for test only!!!
    // uint8_t level = ceil(log2(length));
    // uint32_t newLength = 1 << level;
    // bs.sort((uint8_t *)countDist, newLength);
}

//The OrdArry is in an ascending order, the countDist is ascending ordered by size.
void OItemCountDist_ASC(const Item *OrdArry, CDist * countDist, size_t length){
    uint32_t curCount = 0;
    uint8_t cond = 1;
    for(int i=0; i<length-1; i++){
        curCount = OMove(1, curCount+1, cond);
        countDist[i].count = curCount;
        cond = OrdArry[i+1].size > OrdArry[i].size;
        countDist[i].size = OMove(OrdArry[i].size, MAX_SIZE, cond);
    }

    countDist[length-1].size = OrdArry[length-1].size;
    countDist[length-1].count = OMove(1, curCount+1, cond);

    BitonicSorter bs(&greater_CDist, sizeof(CDist));
    bs.sort((uint8_t *)countDist, length);
}

void OCountDist(uint32_t *OrdArry, CDist * countDist, size_t length){
    uint32_t curCount = 0;
    uint8_t cond = 1;
    for(int i=0; i<length-1; i++){
        curCount = OMove(1, curCount+1, cond);
        countDist[i].count = curCount;
        cond = OrdArry[i+1] > OrdArry[i];
        countDist[i].size = OMove(OrdArry[i], MAX_SIZE, cond);
    }

    countDist[length-1].size = OrdArry[length-1];
    countDist[length-1].count = OMove(1, curCount+1, cond);

    BitonicSorter bs(&greater_CDist, sizeof(CDist));
    bs.sort((uint8_t *)countDist, length);
}

#endif