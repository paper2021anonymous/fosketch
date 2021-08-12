//
// Created by liulingtong on 4/22/21.
//

#ifndef MEASUREMENT_FOSKETCH_H
#define MEASUREMENT_FOSKETCH_H
#include "EnclaveUtil.h"
#include "./SpookyHash/SpookyV2.h"
#include <assert.h>
#include "OPrimitive.h"

//Uncomment for performance test
//Comment for debug!
#define assert(a)
using namespace std;

//uint32_t BT_BUCKET_NUM = (NUM_HEAVY * 2 * BT_FACTOR / BT_BUCKET_SIZE);
//uint32_t BT_TABLE_MEM = (((NUM_HEAVY * 2 * BT_FACTOR / BT_BUCKET_SIZE)+BT_BUCKET_ADD)*BT_BUCKET_SIZE*BT_BLOCK_SIZE);

constexpr std::size_t MB        = 1024 * 1024;

//For Intel(R) SGX
static int rand(void)
{
    int num=0;
    sgx_read_rand((unsigned char*)&num, sizeof(int));
    return num;
}

inline bool smallerItem(uint8_t *pi, uint8_t *pj){
    return (*(Item*)pi).size < (*(Item*)pj).size;
}
inline bool greaterItem(uint8_t *pi, uint8_t *pj){
    return (*(Item*)pi).size > (*(Item*)pj).size;
}

inline bool smallerFlowID(uint8_t *pi, uint8_t *pj){
    return (*(Item*)pi).flowID < (*(Item*)pj).flowID;
}

inline bool greaterFlowID(uint8_t *pi, uint8_t *pj){
    return (*(Item*)pi).flowID > (*(Item*)pj).flowID;
}

inline bool greater_FOS(uint8_t *pi, uint8_t *pj){
    return *(bid_t *)(pi+sizeof(bid_t)) > *(bid_t *)(pj+sizeof(bid_t));
}

inline bool greater_SP(uint8_t *pi, uint8_t *pj){
    return *(bid_t *)pi > *(bid_t *)pj;
}
inline bool smaller_SP(uint8_t *pi, uint8_t *pj){
    return *(bid_t *)pi < *(bid_t *)pj;
}

//To initiate the light part
size_t Cardinality = 0;
uint32_t HHCountDistinct = 0;
uint8_t *pDataCUSketch = NULL;
void *pDataForCUSketch = NULL;
//To initiate the heavy part
Item *pDataStashTable = NULL;
void *pDataForStashHeavy = NULL;
//Just the Stash for the Bucket Table
Item *pStashHeavy = NULL;
uint32_t countStash = 0;
//For Heavy-Change Detection:
ShufflePair *pHeavyListDlt = NULL;
ShufflePair *pHeavyListPreDlt = NULL;
uint32_t btHash = 0;
uint32_t stHash = 0;
uint32_t cusHash = 0;
uint32_t numHeavyChange = 0;
alignas(64) Item HeavyHitterList[2*NUM_HEAVY];
// alignas(64) Item HeavyHitterList[32*NUM_HEAVY];
alignas(64) Item HeavyChangeList[2*NUM_HEAVY];
// alignas(64) Item HeavyChangeList[32*NUM_HEAVY];
// alignas(64) CDist CountHeavy[2*NUM_HEAVY];
alignas(64) CDist CountHeavy[NUM_HEAVY];
// alignas(64) CDist CountHeavy[32*NUM_HEAVY];
alignas(64) uint32_t CountLight[256];


typedef struct Indicator{
    uint16_t ind1;
    uint16_t ind2;
}Indic;

//Tow options, here we store the upper and lower part of the heavy-hitter list.
//TODO: Stash Table can be build by first osort with stHash, not need first osort with random order.
bool OBuildSTable(){
    countStash = 0;
    ShufflePair * pSP;
    void *pData = malloc_align(NUM_HEAVY*sizeof(ShufflePair), 64, (uint8_t **)&pSP);
    bool showZero = false;
    for(int i=0; i<NUM_HEAVY; i++){
        pSP[i].index = abs(rand()%ST_BUCKET_NUM);//ST_BUCKET_NUM
        //TODO Be careful: Just for test, Force each flow id is unique.!!!!!!!!!!!!!!!
        HeavyHitterList[i].flowID = i;
        pSP[i].elem = HeavyHitterList[i];
    }
    
    if(testPo2(NUM_HEAVY)){
        OddEvenMergeSorter oeShuffPair(&greater_SP, sizeof(ShufflePair));
        oeShuffPair.sort((uint8_t *)pSP, NUM_HEAVY);
    }else{
        BitonicSorter bsShuffPair(&greater_SP, sizeof(ShufflePair));
        bsShuffPair.sort((uint8_t *)pSP, NUM_HEAVY);
    }


    uint8_t idcList[ST_BUCKET_NUM];
    memset(idcList, 0, ST_BUCKET_NUM);
    pDataStashTable = (Item *)malloc(ST_TABLE_MEM);
    pDataForStashHeavy = malloc_align(ST_STASH_MEM, 64, (uint8_t **)&pStashHeavy);
    int index;
    int failed = 0;
    int max_count = 0;
    
    for(int i = 0; i < NUM_HEAVY; i++){
        Item tmp = pSP[i].elem;
        uint16_t ind = OMove(abs(rand() % ST_BUCKET_NUM),
            (SpookyHash::Hash32((void *)&tmp.flowID, FLOW_KEY_SIZE, stHash)) % ST_BUCKET_NUM,
            (tmp.flowID == 0));
        if(idcList[ind] >= ST_BUCKET_SIZE) {
            if(countStash >= ST_STASH_NUM){
                printf("Build StashTable Failed!\n"
                    "CHECK IF EACH FLOW HAS A UNIQUE ID!!!");
                return false;
            }
            pStashHeavy[countStash] = tmp;
            countStash++;
        } else {
            index = (ST_BUCKET_SIZE * ind + idcList[ind]);
            pDataStashTable[index] = tmp;
            idcList[ind]++;
        }
    }
    //printf("Stash Heavy Size: %d\n", countStash);
    pSP = NULL;
    free(pData);
    pData = NULL;
    return true;
}

// OHeavyChange(NUM_HEAVY, BT_BUCKET_NUM, BT_BUCKET_MAX_NUM, BT_BUCKET_MAX_SIZE);
uint32_t OHeavyChange(size_t numHeavy, uint32_t numBucket,
        uint32_t maxnumBucket,uint32_t maxsizeBucket){
    size_t memTable = numBucket*BT_BUCKET_SIZE*BT_BLOCK_SIZE;
    bool isPo2 = testPo2(numHeavy);
    // uint8_t level = ceil(log2(numHeavy));
    // size_t length = 1 << level;
    pHeavyListPreDlt = (ShufflePair *)HeavyChangeList;//Instead of 8+4,Can be faster using AVX2.
    for(int i=0; i<numHeavy; i++){
        pHeavyListPreDlt[i].index = rand() % numBucket;
        //TODO: In real system, the previous heavy hitters are stored in CountHeavy for saving memory.
        // pHeavyListPreDlt[i].elem = ((Item *)CountHeavy)[i];
        //TODO: This just for test
        pHeavyListPreDlt[i].elem = HeavyHitterList[numHeavy+i];
    }
    // for(int i=numHeavy; i<length; i++){
    //     pHeavyListPreDlt[i].index = DUMMY_FLAG;
    // }
    OddEvenMergeSorter oeShuffPair(&greater_SP, sizeof(ShufflePair));
    BitonicSorter bsShuffPair(&greater_SP, sizeof(ShufflePair));
    if(isPo2){
        oeShuffPair.sort((uint8_t *)pHeavyListPreDlt, numHeavy);
    }else{
        bsShuffPair.sort((uint8_t *)pHeavyListPreDlt, numHeavy);
    }

    uint8_t * BucketTable = NULL;
    int totalIdc = numBucket;
    Indicator *idcList = (Indicator *)malloc(totalIdc*sizeof(Indicator));
    memset(idcList, 0, totalIdc*sizeof(Indicator));
    void *pData = malloc_align(memTable, 64, &BucketTable);
    int index;
    int failed = 0;
    int max_count = 0;
    for(int i = 0; i < numHeavy; i++){
        Item tmp = pHeavyListPreDlt[i].elem;
        uint32_t ind = OMove(rand() % numBucket,
            (SpookyHash::Hash32((void *)&tmp.flowID, FLOW_KEY_SIZE, btHash)) % numBucket,
            (tmp.flowID == 0));
        if(idcList[ind].ind1 >= BT_BUCKET_SIZE) {
            printf("Build BucketTable from Pre Heavy List Failed! (Mostly it is impossible logically!)\n");
            return 0;
        } else {
            index = BT_BLOCK_SIZE * (BT_BUCKET_SIZE * ind + idcList[ind].ind1);
            *(Item *)(BucketTable+index) = tmp;
            idcList[ind].ind1++;
        }
    }

    for(int i=0; i<numBucket; i++){
        idcList[i].ind2 = idcList[i].ind1;
    }

    //Copy the upper part to HeavyHitterList for heavy-change detection:
    pHeavyListDlt = (ShufflePair *)HeavyChangeList;
    for(int i=0; i<numHeavy; i++){
        pHeavyListDlt[i].index = rand() % numBucket;
        pHeavyListDlt[i].elem = HeavyHitterList[i];
    }

    if(isPo2){
        oeShuffPair.sort((uint8_t *)pHeavyListDlt, numHeavy);
    }else{
        bsShuffPair.sort((uint8_t *)pHeavyListDlt, numHeavy);
    }

    Item tmpBucketTable[maxsizeBucket*maxnumBucket];
    uint16_t tmpCount = 0;

    failed = 0;
    max_count = 0;
    for(int i = 0; i < numHeavy; i++){
        Item tmp = pHeavyListDlt[i].elem;
        uint32_t ind = OMove(rand() % numBucket,
            (SpookyHash::Hash32((void *)&tmp.flowID, FLOW_KEY_SIZE, btHash)) % numBucket,
            (tmp.flowID == 0));
        if(idcList[ind].ind2 == BT_BUCKET_SIZE) {
            if(tmpCount >= maxnumBucket){
                failed++;
                printf("Build BucketTable Exceeds the tmpBucket Limit Failed!\n");
                return 0;
            }
            memcpy((void *)(tmpBucketTable+tmpCount*32),
                BucketTable+BT_BLOCK_SIZE * BT_BUCKET_SIZE * ind,
                BT_BLOCK_SIZE * BT_BUCKET_SIZE);
            tmpBucketTable[tmpCount*maxsizeBucket+idcList[ind].ind2] = tmp;
            *(uint8_t *)(BucketTable+BT_BLOCK_SIZE * BT_BUCKET_SIZE * ind) = tmpCount;
            tmpCount++;
        } else if (idcList[ind].ind2 > BT_BUCKET_SIZE){
            if(idcList[ind].ind2 >= maxsizeBucket){
                failed++;
                printf("Build BucketTable Exceeds the Max tmpBuckets Failed!\n");
                return 0;
            }else {
                uint32_t loca = *(uint8_t *)(BucketTable+BT_BLOCK_SIZE * BT_BUCKET_SIZE * ind);
                tmpBucketTable[loca*maxsizeBucket+idcList[ind].ind2] = tmp;
            }
        } else {
            index = BT_BLOCK_SIZE * (BT_BUCKET_SIZE * ind + idcList[ind].ind2);
            *(Item *)(BucketTable+index) = tmp;
        }
        idcList[ind].ind2++;
    }

    index = 0;
    for(int i=0; i<numBucket; i++){
        Item* addr = (Item*)(BucketTable) +i*BT_BUCKET_SIZE;
        if(idcList[i].ind2 > BT_BUCKET_SIZE){
            uint32_t loca = *(uint8_t *)(BucketTable+BT_BLOCK_SIZE * BT_BUCKET_SIZE * i);
            addr = &tmpBucketTable[loca*maxsizeBucket];
        }
        ODelta(addr, idcList[i].ind1, addr+idcList[i].ind1, idcList[i].ind2-idcList[i].ind1, 1);
    }

    Item* idxDesc = HeavyChangeList;
    Item* idxSrc = (Item*)(BucketTable);
    uint32_t leng = 0;
    uint32_t acount = 0;

    for(int i=0; i<numBucket; i++){
        idxSrc = (Item*)(BucketTable) +i*BT_BUCKET_SIZE;
        leng = idcList[i].ind2;
        if(leng > BT_BUCKET_SIZE){
            uint32_t loca = *(uint8_t *)idxSrc;
            idxSrc = &tmpBucketTable[loca*maxsizeBucket];
        }
        memcpy(idxDesc, idxSrc, leng*BT_BLOCK_SIZE);
        idxDesc += leng;
        acount += leng;
    }
    if(acount != 2*numHeavy){
        printf("BucketTable Wrong! acount = %d, %d\n", acount, 2*numHeavy);
        return 0;
    }

    BucketTable = NULL;
    free(idcList);
    idcList = NULL;
    free(pData);
    pData = NULL;


    if(isPo2){
        OddEvenMergeSorter oeItemDESC(&smallerItem, sizeof(Item), false);
        oeItemDESC.sort((uint8_t *)HeavyChangeList, numHeavy*2);
    }else{
        BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
        bsItemDESC.sort((uint8_t *)HeavyChangeList, numHeavy*2);
    }
    uint32_t count = 0;
    for(int i=0; i<numHeavy*2; i++){
        count += OMove(1, 0, HeavyChangeList[i].size>0);
    }
    // // for(int i=0;i<1000;i++){
    // for(int i=count-1000;i<count;i++){
    //     printf("%x, %d", HeavyChangeList[i].flowID, HeavyChangeList[i].size);
    // }
    return count;
}

// bool OHeavyChange(){
//     //Form the Heavy-Change List:
//     pHeavyListPreDlt = (ShufflePair *)HeavyChangeList;//Instead of 8+4,Can be faster using AVX2.
//     //bool showZero = false;
//     for(int i=0; i<NUM_HEAVY; i++){
//         pHeavyListPreDlt[i].index = rand() % BT_BUCKET_NUM;
//         pHeavyListPreDlt[i].elem = HeavyHitterList[i];
//     }
//     BitonicSorter bsShuffPair(&greater_SP, sizeof(ShufflePair));
//     bsShuffPair.sort((uint8_t *)pHeavyListPreDlt, NUM_HEAVY);

//     uint8_t * BucketTable = NULL;
//     int totalIdc = BT_BUCKET_NUM+BT_BUCKET_ADD;
//     Indicator *idcList = (Indicator *)malloc(totalIdc*sizeof(Indicator));
//     memset(idcList, 0, totalIdc*sizeof(Indicator));
//     void *pData = malloc_align(BT_TABLE_MEM, 64, &BucketTable);
//     int index;
//     int failed = 0;
//     int max_count = 0;
//     for(int i = 0; i < NUM_HEAVY; i++){
//         Item tmp = pHeavyListPreDlt[i].elem;
//         uint16_t ind = OMove(abs(rand() % BT_BUCKET_NUM),
//             (SpookyHash::Hash32((void *)&tmp.flowID, FLOW_KEY_SIZE, btHash)) % BT_BUCKET_NUM,
//             (tmp.flowID == 0));
//         if(idcList[ind].ind1 >= BT_BUCKET_SIZE) {
//             printf("Build BucketTable from Pre Heavy List Failed! (Mostly it is impossible logically!)\n");
//             return false;
//         } else {
//             index = BT_BLOCK_SIZE * (BT_BUCKET_SIZE * ind + idcList[ind].ind1);
//             *(Item *)(BucketTable+index) = tmp;
//             idcList[ind].ind1++;
//         }
//     }

//     for(int i=0; i<BT_BUCKET_NUM; i++){
//         idcList[i].ind2 = idcList[i].ind1;
//     }

//     //Copy the upper part to HeavyHitterList for heavy-change detection:
//     pHeavyListDlt = (ShufflePair *)HeavyChangeList;
//     bool showZero = false;
//     for(int i=0; i<NUM_HEAVY; i++){
//         pHeavyListDlt[i].index = rand() % BT_BUCKET_NUM;
//         pHeavyListDlt[i].elem = HeavyHitterList[i+NUM_HEAVY];
//     }

//     bsShuffPair.sort((uint8_t *)pHeavyListDlt, NUM_HEAVY);

//     Item tmpBucketTable[BT_BUCKET_MAX_SIZE*BT_BUCKET_MAX_NUM];
//     uint8_t tmpCount = 0;

//     failed = 0;
//     max_count = 0;
//     for(int i = 0; i < NUM_HEAVY; i++){
//         Item tmp = pHeavyListDlt[i].elem;
//         uint16_t ind = OMove(abs(rand() % BT_BUCKET_NUM),
//             (SpookyHash::Hash32((void *)&tmp.flowID, FLOW_KEY_SIZE, btHash)) % BT_BUCKET_NUM,
//             (tmp.flowID == 0));
//         if(idcList[ind].ind2 == BT_BUCKET_SIZE) {
//             if(tmpCount >= BT_BUCKET_MAX_NUM){
//                 failed++;
//                 printf("Build BucketTable Exceeds the tmpBucket Limit Failed!\n");
//                 return false;
//             }
//             memcpy((void *)(tmpBucketTable+tmpCount*32),
//                 BucketTable+BT_BLOCK_SIZE * BT_BUCKET_SIZE * ind,
//                 BT_BLOCK_SIZE * BT_BUCKET_SIZE);
//             tmpBucketTable[tmpCount*BT_BUCKET_MAX_SIZE+idcList[ind].ind2] = tmp;
//             *(uint8_t *)(BucketTable+BT_BLOCK_SIZE * BT_BUCKET_SIZE * ind) = tmpCount;
//             tmpCount++;
//         } else if (idcList[ind].ind2 > BT_BUCKET_SIZE){
//             if(idcList[ind].ind2 >= BT_BUCKET_MAX_SIZE){
//                 failed++;
//                 printf("Build BucketTable Exceeds the Max tmpBuckets Failed!\n");
//                 return false;
//             }else {
//                 uint8_t loca = *(uint8_t *)(BucketTable+BT_BLOCK_SIZE * BT_BUCKET_SIZE * ind);
//                 tmpBucketTable[loca*BT_BUCKET_MAX_SIZE+idcList[ind].ind2] = tmp;
//             }
//         } else {
//             index = BT_BLOCK_SIZE * (BT_BUCKET_SIZE * ind + idcList[ind].ind2);
//             *(Item *)(BucketTable+index) = tmp;
//         }
//         idcList[ind].ind2++;
//     }

//     index = 0;
//     for(int i=0; i<BT_BUCKET_NUM; i++){
//         Item* addr = (Item*)(BucketTable) +i*BT_BUCKET_SIZE;
//         if(idcList[i].ind2 <= BT_BUCKET_SIZE){
//         } else {
//             uint8_t loca = *(uint8_t *)(BucketTable+BT_BLOCK_SIZE * BT_BUCKET_SIZE * i);
//             addr = &tmpBucketTable[loca*BT_BUCKET_MAX_SIZE];
//         }
//         ODelta(addr, idcList[i].ind1, addr+idcList[i].ind1, idcList[i].ind2-idcList[i].ind1, 1);
//     }

//     Item* idxDesc = HeavyChangeList;
//     Item* idxSrc = (Item*)(BucketTable);
//     uint16_t leng = 0;
//     uint16_t acount = 0;

//     for(int i=0; i<BT_BUCKET_NUM; i++){
//         idxSrc = (Item*)(BucketTable) +i*BT_BUCKET_SIZE;
//         leng = idcList[i].ind2;
//         if(leng > BT_BUCKET_SIZE){
//             uint8_t loca = *(uint8_t *)idxSrc;
//             idxSrc = &tmpBucketTable[loca*BT_BUCKET_MAX_SIZE];
//         }
//         memcpy(idxDesc, idxSrc, leng*BT_BLOCK_SIZE);
//         idxDesc += leng;
//         acount += leng;
//     }
//     if(acount != 2*NUM_HEAVY){
//         printf("BucketTable Wrong! acount = %d, %d\n", acount, 2*NUM_HEAVY);
//     }

//     BucketTable = NULL;
//     free(idcList);
//     idcList = NULL;
//     free(pData);
//     pData = NULL;

//     BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
//     bsItemDESC.sort((uint8_t *)HeavyChangeList, NUM_HEAVY*2);
//     return true;
// }

void OUpdateCUSketch_B(){
    uint8_t nRecord = CACHE_LINE - 2*sizeof(bid_t);
    uint32_t num1Block = CUSKETCH_MEM / nRecord;
    uint32_t numBlock = num1Block + NUM_HEAVY;
    uint8_t level = ceil(log2(numBlock));
    numBlock = 1 << level; //OSort need the size of the data to be a power of 2.
    //printf("real nBlock: %d, need nBlock: %d\n", num1Block + NUM_HEAVY, numBlock);
    alignas(64) Block64 dump[numBlock];
    //make dummy block:
    for(int i=num1Block + NUM_HEAVY; i<numBlock; i++) {
        dump[i].blockID = DUMMY_FLOW;
    }

    // printf("\nVerify the correctness11\n");
    // for(int i = CUSKETCH_MEM - 256; i<CUSKETCH_MEM; i++){
    //     printf("%d ", pDataCUSketch[i]);
    // }
    // printf("\nXXX\n");
    // for(int i = 0; i<256; i++){
    //     printf("%d ", pDataCUSketch[i]);
    // }
    //process CUSketch array.
    for(int i=0; i<num1Block; i++) {
        dump[i].blockID = i;
        memcpy((uint8_t *)(&dump[i].data), pDataCUSketch+i*nRecord, nRecord);
    }
    //process Heavy hitter
    uint32_t cid;
    uint32_t cnt;
    uint8_t cidx;
    for(int i=0; i<NUM_HEAVY; i++) {
        cid = (SpookyHash::Hash32((void *)&HeavyHitterList[i+NUM_HEAVY].flowID,
                        FLOW_KEY_SIZE, cusHash)) % CUSKETCH_MEM;
        dump[i+num1Block].blockID = cid / nRecord;
        cidx = cid % nRecord;
        cnt = HeavyHitterList[i+NUM_HEAVY].size;
        cnt = OMove(255, cnt, cnt > 255);
        for(int j=0;j<nRecord;j++){
            dump[i+num1Block].data[j] = OMove(cnt, 0, j == cidx);
        }
    }
    BitonicSorter bsClASC(&greater_SP,CACHE_LINE);
    bsClASC.sort((uint8_t *)dump, numBlock);

    //OMax:
    Block64 tmpB;
    uint8_t cond = 1;
    for(int i=0; i<num1Block + NUM_HEAVY-1; i++){
        //if(i<256) printf("(%d,%d,%d,%d,%d -> ", i, dump[i].blockID, cond, dump[i].data[0], tmpB.data[0]);
        OMaxSetMemCL((uint8_t *)&dump[i], (uint8_t *)&tmpB, cond);
        //OMaxSetYmm1415CL((uint8_t *)&dump[i], cond);
        cond = dump[i+1].blockID > dump[i].blockID;
        dump[i].blockID = OMove(dump[i].blockID, DUMMY_FLOW, cond);
        //if(i<256) printf("%d,%d,%d)\n", cond, dump[i].data[0], tmpB.data[0]);
    }
    OMaxSetMemCL((uint8_t *)&dump[num1Block + NUM_HEAVY-1], (uint8_t *)&tmpB, cond);
    //OMaxSetYmm1415CL((uint8_t *)&dump[num1Block + NUM_HEAVY-1], cond);

    //Rebuild CUSketch array.
    bsClASC.sort((uint8_t *)dump, numBlock);

    for(int i=0; i<num1Block; i++) {
        memcpy(pDataCUSketch+i*nRecord, (uint8_t *)(&dump[i].data), nRecord);
    }

    // uint32_t test[5] = {2,5,0,1,0};
    // for(int i=0; i<5;i++){
    //     assert(test[i]==pDataCUSketch[(i+251)*nRecord]);
    // }
    // printf("\nVerify the correctness11\n");
    // //Should set cusHash = 177 any fixed number in OPrepareData()
    // printf("\nAAA\n");
    // for(int i = 0; i<256; i++){
    //     printf("%d ", pDataCUSketch[i]);
    // }
}

//OUpdateCUSketch_BEx(pDataCUSketch,CUSKETCH_MEM,HeavyHitterList+NUM_HEAVY,NUM_HEAVY);
void OUpdateCUSketch_BEx(uint8_t *pLight, size_t numLight, Item *pHeavy, size_t numHeavy){
    //auto t = Timer{BT_TABLE_MEM, __FUNCTION__};
    uint8_t nRecord = CACHE_LINE - 2*sizeof(bid_t);
    uint32_t numEBlock = numLight / nRecord;
    // uint8_t level = ceil(log2(numEBlock));
    // uint32_t num1Block = 1 << level;
    uint32_t num1Block = numEBlock;
    //printf("%d, %d => %d, %d", numEBlock, numEBlock*nRecord, num1Block, num1Block*nRecord);
    // level = ceil(log2(numHeavy));
    // uint32_t num2Block = 1 << level;
    uint32_t num2Block = numHeavy;
    // printf("Before num1Block %d , num2Block %d", num1Block, num2Block);
    num2Block = num2Block > num1Block ? num2Block : num1Block;
    // printf("Before num1Block %d , num2Block %d", num1Block, num2Block);
    uint32_t numTBlock = num1Block+num2Block;
    alignas(64) Block64 dump[numTBlock];
    bool isLightPo2 = testPo2(numEBlock);
    bool isHeavyPo2 = testPo2(num2Block);
    // isLightPo2 = false;
    // isHeavyPo2 = false;
    // uint8_t level = ceil(log2(numEBlock));
    // size_t leng = (1L << level) * nRecord;
    // printf("Leng: %lu", leng);
    //make dummy block:
    // for(int i=numEBlock; i<num1Block; i++) {
    //     dump[i].blockID = DUMMY_FLOW;
    // }
    // for(int i=num1Block+numHeavy; i<numTBlock; i++) {
    //     dump[i].blockID = DUMMY_FLOW;
    // }
    // printf("\nXXX\n");
    // for(int i = 0; i<256; i++){
    //     printf("%d ", pDataCUSketch[i*nRecord]);
    // }
    //process CUSketch array.
    if(isLightPo2){
        for(int i=0; i<numEBlock; i++) {
            dump[i].blockID = i;
            memcpy((uint8_t *)(&dump[i].data), pLight+i*nRecord, nRecord);
        }
        // printf("isLightPo2");
        // for(int i=0; i<50; i++){
        //     printf("%d -> (%d, %d)", i, dump[i].blockID, dump[i].data[0]);
        // }
    } else {
        for(int i=numEBlock-1; i>=0; i--) {
            dump[i].blockID = i;
            memcpy((uint8_t *)(&dump[i].data), pLight+i*nRecord, nRecord);
        }
        // printf("isNotLightPo2");
        // for(int i=0; i<50; i++){
        //     printf("%d -> (%d, %d)", i, dump[i].blockID, dump[i].data[0]);
        // }
    }
    //process Heavy hitter
    Block64 *dump2 = dump+num1Block;
    uint32_t cid;
    uint32_t cnt;
    uint8_t cidx;
    for(int i=numHeavy;i<num1Block;i++){
        printf("haha\n");
        dump2[i].blockID = DUMMY_FLOW;
    }
    for(int i=0; i<numHeavy; i++) {
        cid = (SpookyHash::Hash32((void *)&pHeavy[i].flowID,
                        FLOW_KEY_SIZE, cusHash)) % numLight;
        dump2[i].blockID = cid / nRecord;
        cidx = cid % nRecord;
        // printf("FlowID: %d, size: %d, cid %d, cidx: %d\n",
        //  HeavyHitterList[i+numHeavy].flowID,
        //  HeavyHitterList[i+numHeavy].size, cidx, cid);
        cnt = pHeavy[i].size;
        cnt = OMove(255, cnt, cnt > 255);
        for(int j=0;j<nRecord;j++){
            dump2[i].data[j] = OMove(cnt, 0, j == cidx);
        }
        //printf("Data: %d, cidx: %d\n",dump[i+num1Block].data[cidx], cidx);
    }
    // printf("\nBefor Sort\n");
    // for(int i = numHeavy-1; i>numHeavy-256; i--){
    //     printf("(%d, %d, %d) ", i, dump2[i].blockID, dump2[i].data[0]);
    // }
    // BitonicSorter bsClASC(&greater_SP,CACHE_LINE);
    // bsClASC.sort((uint8_t *)dump2, num2Block);
    OddEvenMergeSorter oeBlockASC(&greater_SP, sizeof(Block64));
    BitonicSorter bsBlockASC(&greater_SP, sizeof(Block64));
    if(isHeavyPo2){
        // printf("isHeavyPo2");
        oeBlockASC.sort((uint8_t *)dump2, num2Block);
    }else{
        bsBlockASC.sort((uint8_t *)dump2, num2Block);
    }
    //OMax:
    // printf("\nAfter Sort\n");
    // for(int i = numHeavy-1; i>numHeavy-256; i--){
    //     printf("(%d, %d, %d) ", i, dump2[i].blockID, dump2[i].data[0]);
    // }

    // printf("\nBBAA\n");
    Block64 tmpB;
    uint8_t cond = 1;
    for(int i=0; i<numHeavy-1; i++){
        //if(i<256) printf("(%d,%d,%d,%d,%d -> ", i, dump2[i].blockID, cond, dump2[i].data[0], tmpB.data[0]);
        OMaxSetMemCL((uint8_t *)&dump2[i], (uint8_t *)&tmpB, cond);
        //OMaxSetYmm1415CL((uint8_t *)&dump2[i], cond);
        cond = dump2[i+1].blockID > dump2[i].blockID;
        dump2[i].blockID = OMove(dump2[i].blockID, DUMMY_FLOW, cond);
        // dump2[i].blockID = OMove(dump2[i].blockID, DUMMY_FLOW, cond);
        //if(i<256) printf("%d,%d,%d)\n", cond, dump2[i].data[0], tmpB.data[0]);
    }

    OMaxSetMemCL((uint8_t *)&dump2[numHeavy-1], (uint8_t *)&tmpB, cond);
    //OMaxSetYmm1415CL((uint8_t *)&dump2[numHeavy-1], cond);

    // bsClASC.sort((uint8_t *)dump2, num2Block);
    if(isHeavyPo2){
        oeBlockASC.sort((uint8_t *)dump2, num2Block);
    }else{
        bsBlockASC.sort((uint8_t *)dump2, num2Block);
    }
    //bsClDESC.sort((uint8_t *)dump2, num2Block);
    // OddEvenMergeSorter_Block64 mergeASC;
    // mergeASC.merge(dump, num1Block*2);
    // printf("\nBefor Sort\n");
    // for(int i = 0; i<256; i++){
    //     printf("%d -> (%d, %d) or (%d, %d) ", i,
    //          dump[i].blockID, dump[i].data[0],
    //          dump2[i].blockID, dump2[i].data[0]);
    // }
    if(isLightPo2){
        oeBlockASC.merge((uint8_t *)dump, num1Block*2);
    }else{
        bsBlockASC.merge((uint8_t *)dump, num1Block*2);
    }
    //bsClDESC.merge((uint8_t *)dump, num1Block*2);
    // printf("\nAfter Sort\n");
    // for(int i = num1Block+num2Block-256; i<num1Block+num2Block; i++){
    //     printf("(%d, %d, %d) ", i, dump[i].blockID, dump[i].data[0]);
    // }
    // for(int i = 2*num1Block-256; i<2*num1Block+5; i++){
    //     printf("(%d, %d, %d) ", i, dump[i].blockID, dump[i].data[0]);
    // }
    cond = 1;
    for(int i=0; i<numEBlock*2-1; i++){
        //if(i<256) printf("(%d,%d,%d,%d,%d -> ", i, dump[i].blockID, cond, dump[i].data[0], tmpB.data[0]);
        OMaxSetMemCL((uint8_t *)&dump[i], (uint8_t *)&tmpB, cond);
        //OMaxSetYmm1415CL((uint8_t *)&dump[i], cond);
        cond = dump[i+1].blockID > dump[i].blockID;
        dump[i].blockID = OMove(dump[i].blockID, DUMMY_FLOW, cond);
        //if(i<256) printf("%d,%d,%d)\n", cond, dump[i].data[0], tmpB.data[0]);
    }

    OMaxSetMemCL((uint8_t *)&dump[numEBlock*2-1], (uint8_t *)&tmpB, cond);
    //OMaxSetYmm1415CL((uint8_t *)&dump[numEBlock*2-1], cond);

    //printf("Last stage:\n");
    // printf("\nBefor Sort\n");
    // for(int i = 0; i<256; i++){
    //     printf("%d -> (%d, %d) or (%d, %d) ", i,
    //          dump[i].blockID, dump[i].data[0],
    //          dump2[i].blockID, dump2[i].data[0]);
    // }

    if(isLightPo2){
        oeBlockASC.sort((uint8_t *)dump, num1Block*2);
    }else{
        bsBlockASC.sort((uint8_t *)dump, num1Block*2);
    }
    // BitonicSorter bsClASC(&greater_SP,CACHE_LINE);
    // bsClASC.sort((uint8_t *)dump, num1Block*2);
    // int start = 0;
    // if(dump[0].blockID == 0){
    //     start = 1;
    //     // printf("Yes, expected");
    // }
    for(int i=0; i<numEBlock; i++) {
        memcpy(pLight+i*nRecord, (uint8_t *)(&dump[i].data), nRecord);
        // if(i<50) printf("%i -> %i %d", i, dump[i].blockID,dump[i].data[0]);
    }
    return;
}

//OUpdateCUSketch(pDataCUSketch, CUSKETCH_MEM,HeavyHitterList+NUM_HEAVY,NUM_HEAVY);
//Need refine!!!!!!!
void OUpdateCUSketch(uint8_t *pLight, size_t numLight, Item *pHeavy, size_t numHeavy){
    uint32_t length = numLight+numHeavy;
    uint8_t level = ceil(log2(length));
    length = 1 << level;
    alignas(64) Item dump[length]; //524288
    for(int i=numLight+numHeavy; i<length; i++){
        dump[i].flowID = DUMMY_FLOW;
    }
    for(int i=0; i<numLight; i++) {
        dump[i].flowID = i;
        dump[i].size = pLight[i];
    }
    uint32_t cnt = 0;
    for(int i=0; i<numHeavy; i++){
        dump[i+numLight].flowID =
                    (SpookyHash::Hash32((void *)&pHeavy[i].flowID,
                        FLOW_KEY_SIZE, cusHash)) % numLight;
        cnt = pHeavy[i].size;
        dump[i+numLight].size = OMove(255, cnt, cnt > 255);
    }
    BitonicSorter bsItemASC(&greaterFlowID, sizeof(Item));
    bsItemASC.sort((uint8_t *)dump, length);
    //OMax:
    uint32_t curMax = 0;
    uint32_t tmpMax = 0;
    uint8_t cond = 1;

    for(int i=0; i<numLight+numHeavy-1; i++){
        tmpMax = OMove(dump[i].size, curMax, dump[i].size > curMax);
        curMax = OMove(dump[i].size, tmpMax, cond);
        dump[i].size = curMax;
        cond = dump[i+1].flowID > dump[i].flowID;
        dump[i].flowID = OMove(dump[i].flowID, DUMMY_FLOW, cond);
    }

    tmpMax = OMove(dump[numLight+numHeavy-1].size, curMax, 
        dump[numLight+numHeavy-1].size > curMax);
    dump[numLight+numHeavy-1].size = 
        OMove(dump[numLight+numHeavy-1].size, tmpMax, cond);

    //Rebuild CUSketch array.
    bsItemASC.sort((uint8_t *)dump, length);
    for(int i=0; i<numLight; i++) {
        pLight[i] = dump[i].size;
    }

    // printf("\nVerify the correctness\n"); 
    // //Should set cusHash = 177 any fixed number in OPrepareData()
    // for(int i = numLight - 256; i<numLight; i++){
    //     printf("%d ", pLight[i]);
    // }
}

//TODO: Need effort to make it support other settings
//not only for the default one.
bool OPrepareData(Message * pMsgSketch, struct ctx_gcm_s *pCtx){
    //---------------------------------------------
    //Saving previous epoch's heavy hitter
    //---------------------------------------------
    //multiplexing the precious memory.
    memcpy((uint8_t *)CountHeavy, (uint8_t *)HeavyHitterList, HEAVY_MEM);


    //---------------------------------------------
    //Entering a new epoch:
    //---------------------------------------------
    //Receiving the two sketches.
    uint8_t *p2Sketch = NULL;
    if(pMsgSketch->header.payload_size <= 0) {
        printf("Receiving Two Sketches Error!\n");
        return false;
    }
    int payload_size = pMsgSketch->header.payload_size - GCM_IV_SIZE;
    if(payload_size != 2*(TOTAL_MEM+2)){
        printf("Payload Size Error for Two Sketches!\n");
        return false;
    }
    p2Sketch = (uint8_t *)malloc(payload_size);
    if(p2Sketch == NULL){
        printf("Malloc Error for Two Sketches!\n");
        return false;
    }
    payload_size = unpack_message(pMsgSketch, pCtx, p2Sketch);
    if(payload_size != 2*(TOTAL_MEM+2)){
        free(p2Sketch);
        p2Sketch = NULL;
        printf("Real Payload Size Error for Two Sketches!\n");
        return false;
    }
    printf("Two Sketches Received!");

    uint8_t * pData1 = p2Sketch;
    uint8_t * pData2 = p2Sketch+TOTAL_MEM+2;
    Cardinality = 0;
    btHash = rand() % (1L<<32);
    stHash = rand() % (1L<<32);
    cusHash = rand() % (1L<<32);

    //---------------------------------------------
    //Merge the two Light parts
    //---------------------------------------------
    //Magic Number... just for fun.
    printf("pData1 magic: %d == 8, %d = 9", pData1[0], pData1[1+CUSKETCH_MEM]);
    printf("pData2 magic: %d == 8, %d = 9", pData2[0], pData2[1+CUSKETCH_MEM]);
    pDataForCUSketch = malloc_align(CUSKETCH_MEM, 64, &pDataCUSketch);
    assert(pDataForCUSketch != NULL);
    memcpy(pDataCUSketch, pData1+1, CUSKETCH_MEM);
    uint8_t * pTmp = pData2+1;
    //Merge the Light part:
    uint8_t *px = pDataCUSketch;
    uint8_t *py = pTmp;
    //TODO: NOTE: We assume CUSKETCH_MEM can be divisible by 32.
    for(int i=0; i<CUSKETCH_MEM / 32; i++){
        __asm__ __volatile__ (
            "mov %1,%%r14\n"
            "VMOVDQU (%%r14),%%ymm14\n"
            "mov %0,%%r14\n"
            "VMOVDQU (%%r14),%%ymm15\n"
            "vpmaxub %%ymm14, %%ymm15, %%ymm15\n"
            "VMOVDQU %%ymm15, (%%r14)\n"
            :"+m"(px)
            :"m"(py)
        );
        px += 32;
        py += 32;
    }


    //---------------------------------------------
    //Calculate Heavy-hitter candidates.
    //---------------------------------------------
    //multiplexing the precious memory.
    Item * pHeavy1 = HeavyHitterList;
    Item * pHeavy2 = HeavyHitterList+NUM_HEAVY;
    memcpy(pHeavy1, pData1+2+CUSKETCH_MEM, HEAVY_MEM);
    memcpy(pHeavy2, pData2+2+CUSKETCH_MEM, HEAVY_MEM);
    //----------------
    //free the payload immediately
    free(p2Sketch);
    p2Sketch = NULL;
    //----------------
    if(testPo2(NUM_HEAVY*2)){
        OddEvenMergeSorter oeItemDESC(&smallerItem, sizeof(Item), false);
        // oeItemDESC.sort((uint8_t *)HeavyHitterList, NUM_HEAVY*2);
        oeItemDESC.merge((uint8_t *)HeavyHitterList, NUM_HEAVY*2);
    }else{
        printf("Reverse the order\n");
        Item tmp;
        for(int k=0;k<NUM_HEAVY/2;k++){
            tmp = pHeavy1[k];
            pHeavy1[k] = pHeavy1[NUM_HEAVY-k];
            pHeavy1[NUM_HEAVY-k] = tmp;
        }
        BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
        //bsItemDESC.sort((uint8_t *)HeavyHitterList, NUM_HEAVY*2);
        bsItemDESC.merge((uint8_t *)HeavyHitterList, NUM_HEAVY*2);
    }
    // //for(int i=0; i<100; i++){
    // for(int i=NUM_HEAVY-100; i<NUM_HEAVY; i++){
    //     printf("%x %d", HeavyHitterList[i].flowID, HeavyHitterList[i].size);
    // }

    //---------------------------------------------
    //Calculate Heavy-change candidates.
    //---------------------------------------------
    numHeavyChange = 0;
    numHeavyChange = OHeavyChange(NUM_HEAVY, BT_BUCKET_NUM, 
                        BT_BUCKET_MAX_NUM, BT_BUCKET_MAX_SIZE);
    if(numHeavyChange == 0){
        printf("Detection Heavy Change Failed");
    }
    printf("Heavy-change Candidates: %u", numHeavyChange);




    //---------------------------------------------
    //Merge the lower part of the Heavy-hitters to the light part
    //---------------------------------------------
    //OUpdateCUSketch();
    //OUpdateCUSketch(pDataCUSketch, CUSKETCH_MEM,HeavyHitterList+NUM_HEAVY,NUM_HEAVY);
    //OUpdateCUSketch_B();
    OUpdateCUSketch_BEx(pDataCUSketch,CUSKETCH_MEM,HeavyHitterList+NUM_HEAVY,NUM_HEAVY);


    //---------------------------------------------
    //Calculate light part counter distribution.
    //---------------------------------------------
    memset(CountLight, 0, 256*sizeof(uint32_t));
    for(int i=0; i<CUSKETCH_MEM;i++){
        OAdd256(1, pDataCUSketch[i], CountLight);
    }


    //---------------------------------------------
    //Calculate heavy part flow distribution.
    //---------------------------------------------
    OItemCountDist_DESC((Item *)HeavyHitterList, CountHeavy, NUM_HEAVY);


    //---------------------------------------------
    //Calculate Cardinality.
    //---------------------------------------------
    float ratio = 1.0*CountLight[0] / CUSKETCH_MEM;
    Cardinality = -CUSKETCH_MEM * log(ratio);
    printf("\tLight part Cardinality: %zu", Cardinality);
    HHCountDistinct = 0;
    for(int i=0; i<NUM_HEAVY; i++){
        bool cond = (HeavyHitterList[i].flowID!=0) 
                    && (HeavyHitterList[i].flowID != DUMMY_FLOW);
        HHCountDistinct += OMove(1,0,cond);
    }
    Cardinality += HHCountDistinct;
    printf("\tHeavy part Cardinality: %u\n"
           "Total Cardinality: %zu", HHCountDistinct, Cardinality);


    //---------------------------------------------
    //Build BucketTable with Stash, later for Init OSTable
    //---------------------------------------------
    OBuildSTable();

    return true;
}

//Response requests:
Item *OResponseHeavyHitter(uint16_t topK){
    if(topK > NUM_HEAVY) topK = NUM_HEAVY;
    //TODO: Requset Mem for the outside by switchless call
    //We juse malloc here:
    Item *respHH = (Item*)malloc(topK*sizeof(Item));
    assert(respHH != NULL);
    for(int i=0; i<topK; i++){
        respHH[i] = HeavyHitterList[i];
    }
    return respHH;
}

Item *OResponseHeavyChange(float gamma){
    if(gamma > 1) gamma = 1;
    //TODO: Requset Mem for the outside by switchless call
    //We juse malloc here:
    uint16_t topK = 0;
    Item *respHC = NULL;
    uint32_t threshold = gamma * Cardinality / 100;
    for(int i=0; i<NUM_HEAVY; i++){
        if(HeavyChangeList[i].size < threshold)
            break;
        topK++;
    }
    if(topK == 0) return NULL;
    respHC = (Item*)malloc(topK*sizeof(Item));
    assert(respHC != NULL);
    for(int i=0; i<topK; i++){
        respHC[i] = HeavyChangeList[i];
    }
    return respHC;
}

// #define FOS_TEST
//#define SHOW_COMPRESS_STASH
// #define COUNT_ACCESS

//TODO: Need reconstruction for ease of use and human readable,
//maybe performance (OAccess).
class FOSketch {
    private:
    char sketchType = 'X';
    void *data = NULL;
    void *OblivData = NULL;
    void *m_stash = NULL;
    //uint8_t *sk = NULL;
    uint8_t *OblivSK = NULL;
    bid_t *pos_map = NULL;
    void *m_pos_map = NULL;
    uint8_t *stash = NULL;
    uint32_t sizeMaxStash = 0;

    uint32_t length_st = 0;

    size_t size_sk = 0;
    uint16_t level = 0;
    size_t nLeaf = 0;
    uint16_t nRecord = 0;
    uint8_t elemSize = 0;
    uint8_t payloadInd;
    size_t nBlock = 0;
    size_t sizeBlock = 0;
    size_t sizeBucket = 0;
    size_t nBucket = 0;
    size_t global_repeat = 0;
    size_t global_added = 0;
    uint8_t needComStash = 0;
    uint32_t maxRealStash = 0;
    uint32_t maxInitStash = 0;
#ifdef COUNT_ACCESS
    uint64_t numAccess = 0;
    uint64_t maxAtAccess = 0;
#endif



    bool global_setOblivious = false;

public:
    uint8_t *sk = NULL;
    FOSketch() = default;

    explicit FOSketch(size_t w_sk, size_t sizeBl, size_t sizeBk, uint8_t compStash) {
        // //Test the correct of PORAM:
        // uint32_t loc[100];
        // uint32_t val[100];
        // for(int i = 0; i< 100; i++){
        //     loc[i] = rand() % w_sk;
        //     val[i] = pDataCUSketch[loc[i]];
        // }
        sketchType = 'L';
        if(!Init_OCU(w_sk, sizeBl, sizeBk, compStash)){
            sketchType = 'X';
            printf("Sketch Init Failed!\n");
        }
        // for(int i = 0; i< 100; i++){
        //     printf("\nLIUInd: %d, realVal: %d, accessVal: %d", loc[i], val[i], OAccess(loc[i]));
        // }
    }
    explicit FOSketch(char stype, uint8_t compStash) {
        if(stype == 'C'){
            // //Test the correct of PORAM:
            // uint32_t loc[100];
            // uint32_t val[100];
            // for(int i = 0; i< 100; i++){
            //     loc[i] = abs(rand() % CUSKETCH_MEM);
            //     val[i] = pDataCUSketch[loc[i]];
            // }
            sketchType = 'C';//SIZE_BLOCK, SIZE_BUCKET
            if(!Init_OCU(CUSKETCH_MEM, SIZE_BLOCK, SIZE_BUCKET, compStash)){
                sketchType = 'X';
                printf("OCUSketch Init Failed!\n");
            }
            // for(int i = 0; i< 100; i++){
            //     printf("\nCUSInd: %d, realVal: %d, accessVal: %d", loc[i], val[i], OAccess(loc[i]));
            // }
        } else if (stype == 'T') {
            sketchType = 'T';
            if(!Init_OCU(ST_TABLE_MEM, SIZE_BLOCK, SIZE_BUCKET, compStash)){
                sketchType = 'X';
                printf("OSTable Init Failed!\n");
            }
        } else {
            printf("Error: Not supported type!");
        }
    }

    ~FOSketch() {
        free_OCU();
    }

    bool initOK(){
        if(sketchType == 'X')
            return false;
        return true;
    }

    //TODO: Check if any pointers in OPrepareData() not freeeeeeeed!
    void free_OCU(){
        sizeBlock = 0;
        sizeBucket = 0;
        size_sk = 0;
        sizeMaxStash = 0;
        level = 0;
        nLeaf = 0;
        nRecord = 0;
        elemSize = 0;
        payloadInd = 0;
        nBlock = 0;
        nBucket = 0;
        sk = NULL;
        OblivSK = NULL;
        length_st = 0;
        global_repeat = 0;
        global_added = 0;
        maxRealStash = 0;
        maxInitStash = 0;
#ifdef COUNT_ACCESS
        numAccess = 0;
        maxAtAccess = 0;
#endif
        needComStash = 0;
        free(data);
        free(OblivData);
        free(m_pos_map);
        free(m_stash);
        m_stash = NULL;
        stash = NULL;
        data = NULL;
        OblivData = NULL;
        pos_map = NULL;
        m_pos_map = NULL;
        if(sketchType == 'T'){
            pStashHeavy = NULL;
            free(pDataForStashHeavy);
            pDataForStashHeavy = NULL;
        }
        sketchType = 'X';
    }

    void showInfo(){
        float totalMemory = 1.0*(nBlock*(sizeBlock+sizeof(bid_t))+nBucket*sizeBucket*sizeBlock)/ MB;
        printf("Block Size: %dB, Bucket Size: %d\n", sizeBlock, sizeBucket);
        printf("Sketch Size: %dKB, Blocks: %d, Counters: %d\n", size_sk/1024, nBlock, nRecord);
        printf("Tree Level: %d\n", level);
        printf("Mem Footprint: %fMB\n", totalMemory);
    }

    void shortInfo(){
        float poramMemory = 1.0*(sizeMaxStash
                + nBucket*sizeBucket*sizeBlock) / MB;
        float metricMemory = 1.0*((sizeof(CDist)
                + 2*2*sizeof(Item))*NUM_HEAVY
                + 256*4) / MB;
        float stashSTableMemory = 1024.0 * ST_STASH_MEM / MB;
        printf("(%c %c %c %dB %d %dKB %d %d %d S%d %.4fMB+%.4fKB+%.4fMB %dK +%d IS%d"
#ifdef FOS_TEST
                " RS%d"
#ifdef COUNT_ACCESS
                " at %d"
#endif
#endif
                ") ",
                sketchType,
                global_setOblivious ? 'O':'N',
                needComStash ? 'C':'N',
                sizeBlock, sizeBucket, size_sk/1024,
                nBlock, level, nRecord, countStash, poramMemory,
                stashSTableMemory, metricMemory, global_repeat/1000, global_added, maxInitStash,
                needComStash ? maxRealStash : length_st
#ifdef COUNT_ACCESS
                , maxAtAccess
#endif
                );
    }

    void setObliv(uint8_t val){
        val > 0 ? global_setOblivious = true : global_setOblivious = false;
    }

    void OCompressStash(){
        size_t flagS = 0;
        size_t insPos = 0;
        //This if-clause is just for test only, should be deleted in a production environment.
#ifdef FOS_TEST
#ifdef SHOW_COMPRESS_STASH
        bool showStash = false;
#endif
        if(length_st > maxRealStash){
            maxRealStash = length_st;
#ifdef COUNT_ACCESS
            maxAtAccess = numAccess;
#endif
        }
#ifdef SHOW_COMPRESS_STASH
        //Check stash with real block
        for(int curPos = 0; curPos < length_st; curPos++){
            flagS = *(bid_t *) (stash+curPos*sizeBlock);
            if(flagS != DUMMY_FLAG){
                showStash = true;
                break;
            }
        }
        if(showStash){
            printf("Test Compress by BitonicSorter of %u\n\tBefore", length_st);
            for(int curPos = 0; curPos < length_st; curPos++){
                flagS = *(bid_t *) (stash+curPos*sizeBlock);
                printf("%u", flagS);
            }
        }
#endif
#endif
        BitonicSorter bs(&greater_SP,sizeBlock);
        //bsItemDESC.sort((uint8_t *)HeavyHitterList, NUM_HEAVY*2);
        bs.sort((uint8_t *)stash, length_st);
        for(int curPos = 0; curPos < length_st; curPos++){
            flagS = *(bid_t *) (stash+curPos*sizeBlock);
            insPos = OMove(insPos+1, insPos, flagS != DUMMY_FLAG);
        }
        length_st = OMove(DEFAULT_STASH_LENGTH, insPos, insPos < DEFAULT_STASH_LENGTH);
        // length_st = (insPos < DEFAULT_STASH_LENGTH) ? DEFAULT_STASH_LENGTH : insPos;
        // if(insPos) printf("insPos %d", insPos);
        // printf("StashLeagth from %d to %d", length_st, insPos);

#ifdef FOS_TEST
        if(insPos > maxInitStash){
            maxInitStash = insPos;
        }
#ifdef SHOW_COMPRESS_STASH
        if(showStash){
            printf("\tAfter, length_st = %u", length_st);
            for(int curPos = 0; curPos < length_st; curPos++){
                flagS = *(bid_t *) (stash+curPos*sizeBlock);
                printf("%u", flagS);
            }
        }
#endif
#endif
    }

    void compressStash(){
        size_t flagS = 0;
        size_t insPos = 0;
        size_t curPos = 0;
        //This i
        if(length_st > maxRealStash){
            maxRealStash = length_st;
#ifdef COUNT_ACCESS
            maxAtAccess = numAccess;
#endif
        }
        for(int curPos = 0; curPos < length_st; curPos++){
            flagS = *(bid_t *) (stash+curPos*sizeBlock);
            if(flagS != DUMMY_FLAG){
                if(curPos != insPos){
                    memcpy(stash+insPos*sizeBlock, stash+curPos*sizeBlock, sizeBlock);
                    *(bid_t *) (stash+curPos*sizeBlock) = DUMMY_FLAG;
                }
                insPos++;
            }
        }
        if(insPos > maxInitStash){
            maxInitStash = insPos;
        }
        //if(insPos) printf("insPos %d", insPos);
        //printf("StashLeagth from %d to %d", length_st, insPos);
        length_st = (insPos < DEFAULT_STASH_LENGTH) ? DEFAULT_STASH_LENGTH : insPos;
    }

    void writeToStash(uint8_t* Path){
        size_t flagS = 0;
        size_t flagP = 0;
        int pos_st = 0;
        for(int i=0; i<sizeBucket*(level+1); i++) {
            flagP = *(bid_t *) (Path+i*sizeBlock);
            if(flagP == DUMMY_FLAG) continue;
            //printf("flagP: %d\n", flagP);
            while(pos_st<length_st){
                //cout << "st: " << pos_st;
                flagS = *(bid_t *) (stash+pos_st*sizeBlock);
                    //printf("flagS: %d\n", flagS);
                if(flagS != DUMMY_FLAG){
                    if(pos_st == (length_st-1)){
#ifdef COUNT_ACCESS
                        if(!needComStash){
                            maxAtAccess = numAccess;
                        }
#endif
                        length_st += 8;
                        if(length_st >MAX_STASH_NUM){
                            length_st -= 8;
                            printf("Block is not write to stash\n");
                            break;
                        }
                        //printf("Stash length is increased to %d\n", length_st);
                        printf("(WS %d)", length_st);
                    }
                    pos_st++;
                    continue;
                } else {
                    memcpy(stash+pos_st*sizeBlock, Path+i*sizeBlock, sizeBlock);
                }
                pos_st++;
                break;
            }
        }
    }


    void OWriteToStash(uint8_t* Path){
        size_t flagS = 0;
        size_t flagP = 0;
        uint8_t dummyBlock[sizeBlock];
        *(bid_t *) dummyBlock = DUMMY_FLAG;
        bool cond = false;
        bool found = false;
        for(int i=0; i<sizeBucket*(level+1); i++) {
            found = false;
            flagP = *(bid_t *) (Path+i*sizeBlock);
            for(int j=0; j<length_st; j++){
                flagS = *(bid_t *) (stash+j*sizeBlock);
                cond = (flagS == DUMMY_FLAG) && (flagP != DUMMY_FLAG);
                OMoveEx((uint8_t *)(stash+j*sizeBlock), (uint8_t *)(Path+i*sizeBlock), cond && !found, sizeBlock);
                // if(cond && !found) {
                //     memcpy(stash+j*sizeBlock, Path+i*sizeBlock, sizeBlock);
                // } else {
                //     memcpy(dummyBlock, Path+i*sizeBlock, sizeBlock);
                //     *(bid_t *) dummyBlock = DUMMY_FLAG;
                // }
                if((j == (length_st-1)) && !found && ((flagS != DUMMY_FLAG) && (flagP != DUMMY_FLAG))){
                    length_st += PACE_STASH;
#ifdef COUNT_ACCESS
                    if(!needComStash){
                        maxAtAccess = numAccess;
                    }
#endif
                    if(length_st >MAX_STASH_NUM){
                        length_st -= 8;
                        printf("Block is not write to stash\n");
                        break;
                    }
                    //printf("Stash length is increased to %d\n", length_st);
                    //printf("(WS %d)", length_st);
                }
                if(!found && cond) {
                    found = true;
                }
            }
        }
    }

    void OWriteToStash_S(uint8_t* Path){
        size_t flagS = 0;
        size_t flagP = 0;
        uint8_t dummyBlock[sizeBlock];
        *(bid_t *) dummyBlock = DUMMY_FLAG;
        bool cond = false;
        bool found = false;
        for(int i=0; i<sizeBucket*(level+1); i++) {
            found = false;
            flagP = *(bid_t *) (Path+i*sizeBlock);
            for(int j=0; j<length_st; j++){
                flagS = *(bid_t *) (stash+j*sizeBlock);
                cond = (flagS == DUMMY_FLAG) && (flagP != DUMMY_FLAG);
                if(cond && !found) {
                    memcpy(stash+j*sizeBlock, Path+i*sizeBlock, sizeBlock);
                } else {
                    memcpy(dummyBlock, Path+i*sizeBlock, sizeBlock);
                    *(bid_t *) dummyBlock = DUMMY_FLAG;
                }
                if((j == (length_st-1)) && !found && ((flagS != DUMMY_FLAG) && (flagP != DUMMY_FLAG))){
                    length_st += 8;
                    if(length_st >MAX_STASH_NUM){
                        length_st -= 8;
                        printf("Block is not write to stash\n");
                        break;
                    }
                    //printf("Stash length is increased to %d\n", length_st);
                    printf("(WS %d)", length_st);
                }
                if(!found && cond) {
                    found = true;
                }
            }
        }
    }

    void OWriteToStash_F(uint8_t* Path){
        size_t flagS = 0;
        size_t flagP = 0;
        uint8_t dummyBlock[sizeBlock];
        *(bid_t *) dummyBlock = DUMMY_FLAG;
        bool cond = false;
        bool found = false;
        for(int i=0; i<sizeBucket*(level+1); i++) {
            found = false;
            flagP = *(bid_t *) (Path+i*sizeBlock);
            for(int j=0; j<length_st; j++){
                flagS = *(bid_t *) (stash+j*sizeBlock);
                cond = (flagS == DUMMY_FLAG) && (flagP != DUMMY_FLAG);
                if(cond && !found) {
                    memcpy(stash+j*sizeBlock, Path+i*sizeBlock, sizeBlock);
                }
                if((j == (length_st-1)) && !found && ((flagS != DUMMY_FLAG) && (flagP != DUMMY_FLAG))){
                    length_st += 8;
                    if(length_st >MAX_STASH_NUM){
                        length_st -= 8;
                        printf("Block is not write to stash\n");
                        break;
                    }
                    //printf("Stash length is increased to %d\n", length_st);
                    printf("(WS %d)", length_st);
                }
                if(!found && cond) {
                    found = true;
                }
            }
        }
    }


    bool readFromStash(uint32_t bid, uint8_t *Block){
        size_t flagS = 0;
        uint8_t dummyBlock[sizeBlock];
        size_t pos = rand() % nLeaf;
        size_t pre_pos = pos_map[bid];
        pos_map[bid] = pos;
        bool found = false;
        for(int i=0; i<length_st; i++){
            flagS = *(bid_t *) (stash+i*sizeBlock);
            found = flagS == bid;
            if(found){
                assert(*(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) == pre_pos);
                *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = pos;
                memcpy(Block, stash+i*sizeBlock, sizeBlock);
                return true;
            }
        }
        if(!found){
            for(int i=0; i<length_st; i++){
                flagS = *(bid_t *) (stash+i*sizeBlock);
                if(flagS == DUMMY_FLAG){
                    *(bid_t *) (stash+i*sizeBlock) = bid;
                    *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = pos;
                    memset(stash+i*sizeBlock+2*sizeof(bid_t), 0, nRecord);
                    memcpy(Block, stash+i*sizeBlock, sizeBlock);
                    found = true;
                    global_added++;
                    //printf("0+%d+", bid);
                    break;
                }
                if(i == (length_st-1)){
                    length_st += 8;
                    if(length_st >MAX_STASH_NUM){
                        length_st -= 8;
                        printf("Block is not write to stash\n");
                        break;
                    }
                    //printf("Stash length is increased to %d\n", length_st);
                    printf("(RS %d)", length_st);
                }
            }
        }
        return found;
    }

    bool OReadFromStash(uint32_t bid, uint8_t *Block){
        size_t flagS = 0;
        uint8_t dummyBlock[sizeBlock];
        size_t pos = rand() % nLeaf;
        size_t pre_pos = pos_map[bid];
        pos_map[bid] = pos;
        bool cond = false;
        bool found = false;
        for(int i=0; i<length_st; i++){
            flagS = *(bid_t *) (stash+i*sizeBlock);
            cond = flagS == bid;
            assert(!(found&&cond));
            bid_t tmp = *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t));
            *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = (bid_t)OMove(pos,tmp, cond && !found);
            OMoveEx(Block, (uint8_t *)(stash+i*sizeBlock), cond && !found, sizeBlock);
            if(!found && cond) {
                found = true;
            }
        }
        //TODO: Need Be refined!
        if(!found){
            for(int i=0; i<length_st; i++){
                flagS = *(bid_t *) (stash+i*sizeBlock);
                if(flagS == DUMMY_FLAG){
                    *(bid_t *) (stash+i*sizeBlock) = bid;
                    *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = pos;
                    memset(stash+i*sizeBlock+2*sizeof(bid_t), 0, nRecord);
                    memcpy(Block, stash+i*sizeBlock, sizeBlock);
                    found = true;
                    global_added++;
                    // printf("global %d\n", global_added);
                    // printf("1+%d+", bid);
                    break;
                }
                if(i == (length_st-1)){
                    length_st += 8;
#ifdef COUNT_ACCESS
                    if(!needComStash){
                        maxAtAccess = numAccess;
                    }
#endif
                    if(length_st >MAX_STASH_NUM){
                        length_st -= 8;
                        printf("Block is not write to stash\n");
                        break;
                    }
                    //printf("Stash length is increased to %d\n", length_st);
                    printf("(RS %d)", length_st);
                }
            }
        }
        return found;
    }

    bool OReadFromStash_S(uint32_t bid, uint8_t *Block){
        size_t flagS = 0;
        uint8_t dummyBlock[sizeBlock];
        size_t pos = rand() % nLeaf;
        size_t pre_pos = pos_map[bid];
        pos_map[bid] = pos;
        bool cond = false;
        bool found = false;
        for(int i=0; i<length_st; i++){
            flagS = *(bid_t *) (stash+i*sizeBlock);
            cond = flagS == bid;
            assert(!(found&&cond));
            if(cond && !found){
                assert(*(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) == pre_pos);
                *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = pos;
                memcpy(Block, stash+i*sizeBlock, sizeBlock);
            } else {
                memcpy(dummyBlock, stash+i*sizeBlock, sizeBlock);
                *(bid_t *) dummyBlock = DUMMY_FLAG;
            }
            if(!found && cond) {
                found = true;
            }
        }
        if(!found){
            for(int i=0; i<length_st; i++){
                flagS = *(bid_t *) (stash+i*sizeBlock);
                if(flagS == DUMMY_FLAG){
                    *(bid_t *) (stash+i*sizeBlock) = bid;
                    *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = pos;
                    memset(stash+i*sizeBlock+2*sizeof(bid_t), 0, nRecord);
                    memcpy(Block, stash+i*sizeBlock, sizeBlock);
                    found = true;
                    global_added++;
                    //printf("2+%d+", bid);
                    break;
                }
                if(i == (length_st-1)){
                    length_st += 8;
                    if(length_st >MAX_STASH_NUM){
                        length_st -= 8;
                        printf("Block is not write to stash\n");
                        break;
                    }
                    //printf("Stash length is increased to %d\n", length_st);
                    printf("(RS %d)", length_st);
                }
            }
        }
        return found;
    }

    bool OReadFromStash_F(uint32_t bid, uint8_t *Block){
        size_t flagS = 0;
        uint8_t dummyBlock[sizeBlock];
        size_t pos = rand() % nLeaf;
        size_t pre_pos = pos_map[bid];
        pos_map[bid] = pos;
        bool cond = false;
        bool found = false;
        for(int i=0; i<length_st; i++){
            flagS = *(bid_t *) (stash+i*sizeBlock);
            cond = flagS == bid;
            assert(!(found&&cond));
            if(cond && !found){
                assert(*(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) == pre_pos);
                *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = pos;
                memcpy(Block, stash+i*sizeBlock, sizeBlock);
            }
            if(!found && cond) {
                found = true;
            }
        }
        if(!found){
            for(int i=0; i<length_st; i++){
                flagS = *(bid_t *) (stash+i*sizeBlock);
                if(flagS == DUMMY_FLAG){
                    *(bid_t *) (stash+i*sizeBlock) = bid;
                    *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = pos;
                    memset(stash+i*sizeBlock+2*sizeof(bid_t), 0, nRecord);
                    memcpy(Block, stash+i*sizeBlock, sizeBlock);
                    found = true;
                    global_added++;
                    //printf("3+%d+", bid);
                    break;
                }
                if(i == (length_st-1)){
                    length_st += 8;
                    if(length_st >MAX_STASH_NUM){
                        length_st -= 8;
                        printf("Block is not write to stash\n");
                        break;
                    }
                    //printf("Stash length is increased to %d\n", length_st);
                    printf("(RS %d)", length_st);
                }
            }
        }
        return found;
    }

    void rebuildStash(uint8_t* Path, uint32_t leaf){
        size_t flagS = 0;
        size_t flagP = 0;
        bool cond = false;
        uint32_t size_st = length_st;

        for(int i=0; i<sizeBucket*(level+1); i++) {
            *(bid_t *) (Path+i*sizeBlock) = DUMMY_FLAG;
        }

        bool found = false;
        for(int i=level; i>=0; i--) {
            int k=0;
            for(int j=0; j<sizeBucket; j++) {
                while(k<size_st){
                    flagS = *(bid_t *) (stash+k*sizeBlock);
                    if(flagS != DUMMY_FLAG){
                        assert(*(bid_t *) (stash+k*sizeBlock+sizeof(bid_t)) == pos_map[flagS]);
                        if((pos_map[flagS]>>(level-i)) == (leaf>>(level-i))){
                            //printf("Found: flagS %d in level %d\n", flagS, i);
                            memcpy(Path+(i*sizeBucket+j)*sizeBlock, stash+k*sizeBlock, sizeBlock);
                            *(bid_t *) (stash+k*sizeBlock) = DUMMY_FLAG;
                            k++;
                            break;
                        }
                    }
                    k++;
                }
            }
        }
    }

    void ORebuildStash(uint8_t* Path, uint32_t leaf){
        size_t flagS = 0;
        size_t flagP = 0;
        bool cond = false;
        uint32_t size_st = length_st;
        uint8_t dummyBlock[sizeBlock];

        for(int i=0; i<sizeBucket*(level+1); i++) {
            *(bid_t *) (Path+i*sizeBlock) = DUMMY_FLAG;
        }

        bool found = false;
        for(int i=level; i>=0; i--) {
            for(int j=0; j<sizeBucket; j++) {
                found = false;
                cond = false;
                for(int k=0; k<size_st; k++){
                    flagS = *(bid_t *) (stash+k*sizeBlock);

                    //bid_t tmp = *(bid_t *) (stash+i*sizeBlock+sizeof(bid_t));
                    //*(bid_t *) (stash+i*sizeBlock+sizeof(bid_t)) = (bid_t)OMove(tmp, pos, cond && !found);
                    //OMoveEx(Block, (uint8_t *)(stash+i*sizeBlock), cond && !found, sizeBlock);
                    

                    cond = (bool)OMove(((pos_map[flagS]>>(level-i)) == (leaf>>(level-i))), cond, flagS != DUMMY_FLAG);
                    if(flagS != DUMMY_FLAG){
                        assert(*(bid_t *) (stash+k*sizeBlock+sizeof(bid_t)) == pos_map[flagS]);
                        //cond = ((pos_map[flagS]>>(level-i)) == (leaf>>(level-i)));
                    }
                    OMoveEx(Path+(i*sizeBucket+j)*sizeBlock,stash+k*sizeBlock,cond && !found,sizeBlock);
                    *(bid_t *) (stash+k*sizeBlock) = (bid_t)OMove(DUMMY_FLAG, *(bid_t *) (stash+k*sizeBlock), cond && !found);
                    found = OMove(true, found, cond && !found);
                    cond = OMove(false, cond, cond && !found);
                    /*if(cond && !found) {
                        //printf("Found: flagS %d in level %d\n", flagS, i);
                        memcpy(Path+(i*sizeBucket+j)*sizeBlock, stash+k*sizeBlock, sizeBlock);
                        *(bid_t *) (stash+k*sizeBlock) = DUMMY_FLAG;
                        found = true;
                        cond = false;
                    } else {
                        memcpy(dummyBlock, stash+k*sizeBlock, sizeBlock);
                        *(bid_t *) dummyBlock = DUMMY_FLAG;
                    }*/
                }
            }
        }
        if(needComStash)
            OCompressStash();
    }

    void ORebuildStash_S(uint8_t* Path, uint32_t leaf){
        size_t flagS = 0;
        size_t flagP = 0;
        bool cond = false;
        uint32_t size_st = length_st;
        uint8_t dummyBlock[sizeBlock];

        for(int i=0; i<sizeBucket*(level+1); i++) {
            *(bid_t *) (Path+i*sizeBlock) = DUMMY_FLAG;
        }

        bool found = false;
        for(int i=level; i>=0; i--) {
            for(int j=0; j<sizeBucket; j++) {
                found = false;
                cond = false;
                for(int k=0; k<size_st; k++){
                    flagS = *(bid_t *) (stash+k*sizeBlock);
                    if(flagS != DUMMY_FLAG){
                        assert(*(bid_t *) (stash+k*sizeBlock+sizeof(bid_t)) == pos_map[flagS]);
                        cond = ((pos_map[flagS]>>(level-i)) == (leaf>>(level-i)));
                    }
                    if(cond && !found) {
                        //printf("Found: flagS %d in level %d\n", flagS, i);
                        memcpy(Path+(i*sizeBucket+j)*sizeBlock, stash+k*sizeBlock, sizeBlock);
                        *(bid_t *) (stash+k*sizeBlock) = DUMMY_FLAG;
                        found = true;
                        cond = false;
                    } else {
                        memcpy(dummyBlock, stash+k*sizeBlock, sizeBlock);
                        *(bid_t *) dummyBlock = DUMMY_FLAG;
                    }
                }
            }
        }
    }

    void ORebuildStash_F(uint8_t* Path, uint32_t leaf){
        size_t flagS = 0;
        size_t flagP = 0;
        bool cond = false;
        uint32_t size_st = length_st;
        uint8_t dummyBlock[sizeBlock];

        for(int i=0; i<sizeBucket*(level+1); i++) {
            *(bid_t *) (Path+i*sizeBlock) = DUMMY_FLAG;
        }

        bool found = false;
        for(int i=level; i>=0; i--) {
            for(int j=0; j<sizeBucket; j++) {
                found = false;
                cond = false;
                for(int k=0; k<size_st; k++){
                    flagS = *(bid_t *) (stash+k*sizeBlock);
                    if(flagS != DUMMY_FLAG){
                        assert(*(bid_t *) (stash+k*sizeBlock+sizeof(bid_t)) == pos_map[flagS]);
                        cond = ((pos_map[flagS]>>(level-i)) == (leaf>>(level-i)));
                    }
                    if(cond && !found) {
                        //printf("Found: flagS %d in level %d\n", flagS, i);
                        memcpy(Path+(i*sizeBucket+j)*sizeBlock, stash+k*sizeBlock, sizeBlock);
                        *(bid_t *) (stash+k*sizeBlock) = DUMMY_FLAG;
                        found = true;
                        cond = false;
                    }
                }
            }
        }
    }

    //TODO  only works for 64B blocks as CACHE_LINE = 64.
    void ORebuildStash_CL(uint8_t* Path, uint32_t leaf){
        size_t flagS = 0;
        size_t flagP = 0;
        bool cond = false;
        uint32_t size_st = length_st;
        uint8_t dummyBlock[sizeBlock];

        for(int i=0; i<sizeBucket*(level+1); i++) {
            *(bid_t *) (Path+i*sizeBlock) = DUMMY_FLAG;
        }

        bool found = false;
        for(int i=level; i>=0; i--) {
            for(int j=0; j<sizeBucket; j++) {
                found = false;
                cond = false;
                for(int l=0; l<size_st; l+=(CACHE_LINE/sizeBlock)){
                    for(int k=l; k<(l+CACHE_LINE/sizeBlock) && k<size_st; k++){
                        flagS = *(bid_t *) (stash+k*sizeBlock);
                        if(flagS != DUMMY_FLAG){
                            assert(*(bid_t *) (stash+k*sizeBlock+sizeof(bid_t)) == pos_map[flagS]);
                            cond = ((pos_map[flagS]>>(level-i)) == (leaf>>(level-i)));
                        }
                        if(cond && !found) {
                            //printf("Found: flagS %d in level %d\n", flagS, i);
                            memcpy(Path+(i*sizeBucket+j)*sizeBlock, stash+k*sizeBlock, sizeBlock);
                            *(bid_t *) (stash+k*sizeBlock) = DUMMY_FLAG;
                            found = true;
                            cond = false;
                            break;
                        }
                        if(k==(l+CACHE_LINE/sizeBlock)-1)
                        {
                            memcpy(dummyBlock, stash+k*sizeBlock, sizeBlock);
                            *(bid_t *) dummyBlock = DUMMY_FLAG;
                        }
                    }
                }
            }
        }
    }

    void pushPath(uint8_t* Path, size_t leaf){
        size_t delta = 1;
        size_t mask = 1<<(level-1);
        memcpy(OblivSK, Path, sizeBucket*sizeBlock);
        for(int i=1; i<=level; i++){
            delta *= 2;
            if(mask & leaf) delta++;
            memcpy(OblivSK+(delta-1)*sizeBucket*sizeBlock, Path+i*sizeBucket*sizeBlock, sizeBucket*sizeBlock);
            mask = mask>>1;
        }
        assert(delta == (leaf+(1<<level)));
    }

    void readPath(uint8_t* Path, uint32_t leaf){
        size_t delta = 1;
        size_t mask = 1<<(level-1);
        memcpy(Path, OblivSK, sizeBucket*sizeBlock);
        for(int i=1; i<=level; i++){
            delta *= 2;
            if(mask & leaf) delta++;
            memcpy(Path+i*sizeBucket*sizeBlock, OblivSK+(delta-1)*sizeBucket*sizeBlock, sizeBucket*sizeBlock);
            mask = mask>>1;
        }
        assert(delta == (leaf+(1<<level)));
    }

    //TODO: access may be not correct, need check. 
    //(- x) (...+y...)  there, x should not smaller than y. if access enough times, they muse be equal!
    //[(- 24) (L N C 16B 5 450KB 38400 15 12 S0 5.1249MB+7.8125KB+0.7510MB 50000 +36 S0)] 
    uint32_t access(size_t index){
        uint32_t size =0;
        size_t bid = index / nRecord;
        uint16_t subIdx = index % nRecord;
        size_t leaf = 0;
        //assert(bid < nBlock);
        leaf = pos_map[bid];
        uint8_t Path[sizeBucket*sizeBlock*(level+1)];
        readPath(Path, leaf);
        uint8_t Block[sizeBlock];
        writeToStash(Path);
        bool isFound = readFromStash(bid, Block);
        rebuildStash(Path, leaf);

        pushPath(Path, leaf);

        if(!isFound){
            printf("x%dx", bid);
        }else{
            size =*(uint8_t *)(Block+payloadInd+subIdx);
        }

        return size;
    }

    bool OAccess(size_t index, uint8_t *Block){
        bid_t bid;
        if (sketchType == 'T'){
            bid = index;
        } else {
            bid = index / nRecord;
        }
        bid_t leaf = 0, dummy = 0;
        uint16_t pace = (CACHE_LINE/sizeof(bid_t)); //2^5
        size_t subCIdx = bid % pace;
#ifdef COUNT_ACCESS
        numAccess++;
#endif
        // uint16_t t = bid;
        // uint16_t la;
        // uint32_t oval = OGet(t/2,(uint32_t *)pos_map,ceil(1.0*nBlock/2));
        // la = ((uint16_t *)&oval)[t&1];//assume this is oblivious!
        // if(la == pos_map[t] && isbb == true) {
        //     printf("Here is right%p\n", pos_map);
        //     printf("nBl %d\n", nBlock);
        //     isbb = false;
        //     return 0;
        // }

        for(int i=0; i<nBlock; i += pace) {
            dummy = pos_map[i];
            if(i == (bid & ~(pace-1))){
                assert((i+subCIdx) == bid);
                leaf = pos_map[bid];
            }
        }

        uint8_t Path[sizeBucket*sizeBlock*(level+1)];
        readPath(Path, leaf);
        // uint8_t Block[sizeBlock];
        OWriteToStash(Path);
        bool isFound = OReadFromStash(bid, Block);
        ORebuildStash(Path, leaf);

        pushPath(Path, leaf);

        //uint8_t test;
        if(!isFound){
            printf("x%dx", bid);
        }
        return isFound;
    }

    // uint32_t OAccess(size_t index){
    uint32_t OAccessFID(uint32_t flowID){
        uint32_t size = 0;
        size_t index;
        uint16_t subIdx;
        if (sketchType == 'T'){
            index = (SpookyHash::Hash32((void *)&flowID,
                       FLOW_KEY_SIZE, stHash)) % ST_BUCKET_NUM;
        } else {
            index = (SpookyHash::Hash32((void *)&flowID,
                        FLOW_KEY_SIZE, cusHash)) % CUSKETCH_MEM;
            subIdx = index % nRecord;
        }

        uint8_t Block[sizeBlock];
        // bool isFound = OAccess(index, Block);
        if(OAccess(index, Block)){//TODO: make it oblivious!
            if(sketchType == 'T'){
                for(int x=0; x<nRecord; x++){
                    size = OMove(((Item *)(Block+payloadInd))[x].size, size,
                        ((Item *)(Block+payloadInd))[x].flowID == flowID);
                    // size = ((Item *)(Block+payloadInd))[subIdx].size;
                }
            } else {
                size = ((uint8_t *)(Block+payloadInd))[subIdx];
                //test = OGet(Block, uint32_t *pArry)
            }
        }

        // bool isInSt = false;
        // uint32_t flowID = 1777;
        if (sketchType == 'T'){//countStash //pStashHeavy
            for(int i=0; i<countStash; i++){
                size = OMove(pStashHeavy[i].size, size,
                        pStashHeavy[i].flowID == flowID);
                // isInSt = OMove(1, isInSt, pStashHeavy[i].flowID == flowID);
            }
        }
        return size;
    }


    uint8_t OAccess_S(size_t index){
        uint16_t size =-1;
        size_t bid = index / nRecord;
        uint16_t subIdx = index % nRecord;
        size_t leaf = 0, dummy = 0;
        for(int i=0; i<nBlock; i++) {
            if(i==bid){
                leaf = pos_map[i];
            } else {
                dummy = pos_map[i];
            }
        }
        uint8_t Path[sizeBucket*sizeBlock*(level+1)];
        readPath(Path, leaf);
        uint8_t Block[sizeBlock];
        OWriteToStash_S(Path);
        bool isFound = OReadFromStash_S(bid, Block);
        ORebuildStash_S(Path, leaf);

        pushPath(Path, leaf);

        if(!isFound){
            printf("x%dx", bid);
        }else{
            size =*(uint8_t *)(Block+payloadInd+subIdx);
        }

        return size;
    }

    uint8_t OAccess_F(size_t index){
        uint16_t size =-1;
        size_t bid = index / nRecord;
        uint16_t subIdx = index % nRecord;
        bid_t leaf = 0, dummy = 0;
        uint16_t pace = (CACHE_LINE/sizeof(bid_t));
        size_t subCIdx = bid % pace; 
        for(int i=0; i<nBlock; i += pace) {
            dummy = pos_map[i];
            if(i == (bid & ~(pace-1))){
                assert((i+subCIdx) == bid);
                leaf = pos_map[bid];
            }
        }
        uint8_t Path[sizeBucket*sizeBlock*(level+1)];
        readPath(Path, leaf);
        uint8_t Block[sizeBlock];
        OWriteToStash_F(Path);
        bool isFound = OReadFromStash_F(bid, Block);
        ORebuildStash_F(Path, leaf);

        pushPath(Path, leaf);

        if(!isFound){
            printf("x%dx", bid);
        }else{
            size =*(uint8_t *)(Block+payloadInd+subIdx);
        }

        return size;
    }

    void OBurstAccessFID(uint32_t repeats){
        //auto t = Timer{repeats*sizeBucket*sizeBlock*(level+1), __FUNCTION__};
        // uint16_t scale = (sketchType == 'T') ? 1 : nRecord;
        // printf("nBlock*scale: %u, ST_BUCKET_NUM: %u, CUSKETCH_MEM: %u\n", nBlock*scale,ST_BUCKET_NUM, CUSKETCH_MEM);
        global_repeat = repeats*REPEAT_BASE;
        uint32_t flowID;
        for(uint64_t i=0; i<global_repeat; i++){
            sgx_read_rand((unsigned char*)&flowID, sizeof(flowID));
            // flowID = rand() % MAX_SIZE;
            OAccessFID(flowID);
        }

        // if (sketchType == 'T'){
        //     uint32_t size = 0;
        //     for(int i=0; i< NUM_HEAVY; i++){
        //         flowID = HeavyHitterList[i].flowID;
        //         size = OAccessFID(flowID);
        //         printf("flowID: %u, real size: %u, ORAM size: %u", 
        //             flowID, HeavyHitterList[i].size, size);
        //     }
        // }
    }

    void BurstAccess(uint32_t repeats){
        //auto t = Timer{repeats*sizeBucket*sizeBlock*(level+1), __FUNCTION__};
        global_repeat = repeats*REPEAT_BASE;
        size_t index;
        uint8_t Block[sizeBlock];
        uint16_t scale = (sketchType == 'T') ? 1 : nRecord;
        for(uint64_t i=0; i<global_repeat; i++){
            index = rand() % (nBlock*scale);
            //index = rand() % nBlock;
            if(global_setOblivious){
                OAccess(index, Block);
            }else{
                access(index);
            }
        }
    }

    void LinearAccess(uint32_t repeats){
        uint32_t repts = repeats<nBlock?repeats:nBlock;
        //printf("repeats: %d\n", repts);
        //auto t = Timer{repts*sizeBucket*sizeBlock*(level+1), __FUNCTION__};
        global_repeat = repts;
        uint8_t Block[sizeBlock];
        uint16_t scale = (sketchType == 'T') ? 1 : nRecord;
        for(int i=0; i<repts; i++){
            if(global_setOblivious){
                OAccess(i*scale, Block);
            }else{
                access(i*scale);
            }
        }
    }

    bool Init_OCU(size_t w_sk, size_t sizeBl, size_t sizeBk, uint8_t compStash = 1){
        uint8_t * pSource = NULL;
        sizeBlock = sizeBl;
        sizeBucket = sizeBk;
        size_sk = w_sk;
        // printf("size_sk is %lu\n", size_sk);
        needComStash = compStash;
        if(sketchType == 'C'){
            nBlock = ceil(1.0*size_sk/(sizeBlock-2*sizeof(bid_t)));
            nRecord = (sizeBlock - 2*sizeof(bid_t)) / sizeof(uint8_t);
            elemSize = 1;
            payloadInd = 2*sizeof(bid_t);
            pSource = pDataCUSketch;
        }else if(sketchType == 'T'){
            nBlock = ceil(1.0*size_sk/(sizeBlock-sizeof(Item)));
            nRecord = (sizeBlock - 2*sizeof(bid_t)) / sizeof(Item);
            elemSize = sizeof(Item);
            payloadInd = sizeof(Item); //sizeof(Item) == HEAVY_PAIR_SIZE
            pSource = (uint8_t *)pDataStashTable;
        } else {
            nRecord = (sizeBlock - 2*sizeof(bid_t)) / sizeof(uint8_t);
            elemSize = 1;
            payloadInd = 2*sizeof(bid_t);
            pSource = pDataCUSketch;
        }
        
        level = floor(log2(nBlock));
        if(level >= sizeof(bid_t)*8){
            printf("Level (%d) >= %d bits! not allowed. Need resize the type of Block ID!",
                level, sizeof(bid_t)*8);
            return false;
        }
        nLeaf = 1<<level;
        nBucket = (1 << (level+1)) -1; //1024*2
        //shortInfo();

        BitonicSorter bs(&greater_FOS,sizeBlock);

        data = malloc_align(nBlock*sizeBlock, sizeBlock, &sk);
        sizeMaxStash = sizeBlock * MAX_STASH_NUM;
        m_stash = malloc_align(sizeMaxStash, sizeBlock, &stash);

        //uint8_t dummyBlock[sizeBlock];
        //*(bid_t *) dummyBlock = DUMMY_FLAG;
        for(int i=0; i < MAX_STASH_NUM; i++) {
            *(bid_t *) (stash+i*sizeBlock) = DUMMY_FLAG;
        }
        length_st = DEFAULT_STASH_LENGTH;
        maxRealStash = DEFAULT_STASH_LENGTH;
        maxInitStash = maxRealStash;
#ifdef COUNT_ACCESS
        numAccess = 0;
        maxAtAccess = 0;
#endif

        m_pos_map = malloc_align(nBlock*sizeof(bid_t), sizeBlock, (uint8_t **)&pos_map);
        assert(pos_map != NULL);
        //bid_t pos_map[nBlock];
        //bid_t array[nBlock];
        //srand (time(NULL));
        size_t flag_start = 0;
        size_t flag_end = 0;
        size_t cur_length =0;

        size_t bucket_start =nBucket;
        size_t bucket_end =nBucket;

        OblivData = malloc_align(nBucket*sizeBucket*sizeBlock, sizeBlock, &OblivSK);
        for(int i=0; i<nBucket*sizeBucket;i++){
            *(bid_t *) (OblivSK+i*sizeBlock) = DUMMY_FLAG;
        }
        //shortInfo();

        size_t preIndex = 0;
        size_t curIndex = 0;
        uint16_t count = 0;
        size_t index = 0;
        size_t nFailed =0;
        //auto t = Timer{nBucket*sizeBucket*sizeBlock, __FUNCTION__};
        for(int i=level; i>=0; i--){
            cur_length = 1<<i;
            bucket_end =bucket_start;
            bucket_start =bucket_end-cur_length;
            //printf("startB:%d;endB:%d\n",bucket_start,bucket_end );
            if(!(nBlock & cur_length)) continue;
            //flag_start = flag_end;
            flag_end = flag_start + cur_length;
            //flag_end = flag_end > nBlock ? nBlock : flag_end;
            //printf("\n---------------------------------------------\n");
            //printf("Processing Block from %d to %d, at level %d:\n", flag_start, flag_end-1, i);

            for(int j =flag_start; j < flag_end; j++){
                pos_map[j] = rand() % nLeaf;
                //array[j] = (pos_map[j]) % cur_length;
                //array[j] = (pos_map[j])>>(level-i);
                *(bid_t *) (sk+j*sizeBlock) = j;
                *(bid_t *)(sk+j*sizeBlock+sizeof(bid_t)) = pos_map[j];
                //memset(sk+j*sizeBlock+payloadInd, 7, nRecord);
                memcpy(sk+j*sizeBlock+payloadInd, pSource+j*nRecord*elemSize, (sizeBlock-payloadInd));
                // if(j==20){
                //     for(int x=0;x<nRecord;x++){
                //         printf("%d -> %d", x, (sk+j*sizeBlock+payloadInd)[x]);
                //     }
                // }
            }

            //bs.sort(array+flag_start, flag_end - flag_start, sk+flag_start*sizeBlock, sizeBlock);
            bs.sort(sk+flag_start*sizeBlock, flag_end - flag_start);

            count = sizeBucket;
            preIndex = -1;
            for(int j =flag_start; j <flag_end; j++){
                //curIndex = array[j];
                curIndex = (*(bid_t *)(sk+j*sizeBlock+sizeof(bid_t)))>>(level-i);
                //printf("idx: %d ", curIndex);
                if (preIndex == curIndex) {
                    count++;
                    if(count >= sizeBucket) {
                        //TODO: We should save those overflowed Blocks
                        //and reinsert them into the PORAM when the outer for-loop finished.
                        bid_t flag = *(bid_t *) (sk+j*sizeBlock);
                        //printf("For Block %d, out of bucket!\n", flag);
                        nFailed++;
                        //printf("[%d:%d]", flag,pos_map[flag]);
                        continue;
                    }
                } else {
                    count = 0;
                }
                /*
                if(*(bid_t *) (sk+j*sizeBlock) == 2){
                    printf("\n\nbid:2;leaf:%d;curindex:%d;loca:%d;count:%d\n",pos_map[2],curIndex,bucket_start + curIndex,count);
                }
                */
                index = (bucket_start + curIndex)*(sizeBucket*sizeBlock)+count*sizeBlock;
                memcpy(OblivSK+index, sk+j*sizeBlock, sizeBlock);
                preIndex = curIndex;
            }
            flag_start = flag_end;
            //if(!(nBlock & cur_length) || (nBlock == flag_end)) break;
        }
        //printf("%d uninserted blocks\n", nFailed);
        printf("(- %d)", nFailed);

        if(sketchType == 'C'){
            pDataCUSketch = NULL;
            free(pDataForCUSketch);
            pDataForCUSketch = NULL;
        } else if(sketchType == 'T'){
            free(pDataStashTable);
            pDataStashTable = NULL;
        } else {
            pDataCUSketch = NULL;
            free(pDataForCUSketch);
            pDataForCUSketch = NULL;
        }

        return true;
    }
};

#endif //MEASUREMENT_CMSKETCH_H

