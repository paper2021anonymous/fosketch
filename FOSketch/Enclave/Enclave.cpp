#include "FOSketch.h"

using namespace std;

struct ctx_gcm_s ctx;

// Queue *message_pool = nullptr;
// Queue *input_queue = nullptr;
// Queue *output_queue = nullptr;

// unordered_map<string, float> prep_statistics;
// unordered_map<string, float> cur_statistics;

void ecall_init(unsigned char *ovs_key, size_t key_size) {
    memcpy(ctx.key, ovs_key, key_size);
    //hack by vivian
    return;

    // message_pool = (Queue*) pool;
    // input_queue = (Queue*) queue_in;
    // output_queue = (Queue*) queue_out;
}



int n_ecall_bare_switchless = 0;
int n_ecall_bare = 0;


//Performance Test:
void ClearCountLight(){
    memset(CountLight, 0, 256*4);
}
void TestLightCountDist_NO(){
    uint8_t *px = pDataCUSketch;
    for(int j=0; j<CUSKETCH_MEM; j++)
        CountLight[px[j]]++;
}
void TestLightCountDist_AVX(){
    uint8_t *px = pDataCUSketch;
    for(int j=0; j<CUSKETCH_MEM; j++)
        OAdd256(1, px[j], CountLight);
}

void TestLightCountDist_AVX(uint8_t* pData, size_t size){
    uint8_t *px = pData;
    for(int j=0; j<size; j++)
        OAdd256(1, px[j], CountLight);
}

void TestLightCountDist(){
    uint8_t *px = pDataCUSketch;
    for(int i=0; i<CUSKETCH_MEM; i++)
        for(int j=0; j<256; j++){
            CountLight[j] += OMove(1, 0, j==px[i]);
        }
}


// void ClearCountHeavy(){
//     memset(CountHeavy, 255, NUM_HEAVY*sizeof(CDist));
// }

inline bool smallerCDist(CDist pi, CDist pj){
    return pi.size < pj.size;
}
void TestHeavyCountDist_NO_MAP(Item *HList){
    unordered_map<uint32_t, uint32_t> result_map;
    for(int i = 0; i < NUM_HEAVY; i++) {
        result_map[HList[i].size]++;
    }
    uint32_t idx = 0;
    for(auto & it : result_map) {
        CountHeavy[idx].size = it.first;
        CountHeavy[idx].count = it.second;
        idx++;
    }
    vector<CDist> myvector(CountHeavy, CountHeavy+idx);
    sort(myvector.begin(), myvector.end(), smallerCDist);
    memcpy((uint8_t *)CountHeavy, (uint8_t *)&(myvector[0]), idx*sizeof(CDist));
}
inline bool smallerSItem(Item pi, Item pj){
    return pi.size < pj.size;
}
inline bool greaterSItem(Item pi, Item pj){
    return pi.size > pj.size;
}
void TestHeavyCountDist_NO_ORD(Item *HList){
    // BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
    // bsItemDESC.sort((uint8_t *)HHList, NUM_HEAVY);
    Item *HHList = HList;
    vector<Item> myvector (HHList, HHList+NUM_HEAVY);
    sort (myvector.begin(), myvector.end(), smallerSItem);
    HHList = &myvector[0];
    uint16_t numSize = 0;
    CountHeavy[0].size = HHList[0].size;
    CountHeavy[0].count = 1;
    uint16_t curSize = HHList[0].size;
    for(int i=1; i<NUM_HEAVY; i++){
        if(curSize == HHList[i].size){
            CountHeavy[numSize].count++;
            continue;
        }
        numSize++;
        CountHeavy[numSize].size = HHList[i].size;
        CountHeavy[numSize].count = 1;
        curSize = CountHeavy[numSize].size;
    }
}
void TestHeavyCountDist_NO(){
    uint16_t numSize = 1;
    CountHeavy[0].size = HeavyHitterList[0].size;
    CountHeavy[0].count = 1;
    for(int i=1; i<NUM_HEAVY; i++){
        for(int j=0; j<numSize; j++){
            if(CountHeavy[j].size == HeavyHitterList[i].size){
                CountHeavy[j].count++;
                break;
            }
        }
        CountHeavy[numSize].size = HeavyHitterList[i].size;
        CountHeavy[numSize].count = 1;
        numSize++;
    }
}

void TestHeavyCountDist_SORT(){
    BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
    bsItemDESC.sort((uint8_t *)HeavyHitterList, NUM_HEAVY);
    OItemCountDist_DESC((Item *)HeavyHitterList, CountHeavy, NUM_HEAVY);
}

void TestHeavyCountDist_SORT(Item* pData, size_t size){
    if(testPo2(size)){
        OddEvenMergeSorter oeItemDESC(&smallerItem, sizeof(Item), false);
        oeItemDESC.sort((uint8_t *)pData, size);
    }else{
        BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
        bsItemDESC.sort((uint8_t *)pData, size);
    }
    // BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
    // bsItemDESC.sort((uint8_t *)pData, size);
    OItemCountDist_DESC(pData, CountHeavy, size);
}

void TestHeavyCountDist(){
    uint8_t cond = 0;
    uint8_t isFound = 0;
    uint16_t numSize = 0;
    CountHeavy[0].size = HeavyHitterList[0].size;
    CountHeavy[0].count = 1;
    for(int i=1; i<NUM_HEAVY; i++){
        isFound = false;
        cond = false;
        for(int j=0; j<i; j++){
            cond = OMove(1, 0, j > numSize); //out of bound
            cond = (!cond) && (!isFound) && (CountHeavy[j].size == HeavyHitterList[i].size);
            CountHeavy[j].count += OMove(1, 0, cond);
            isFound = OMove(1, isFound, cond);
            cond = (j == numSize) && (!isFound);
            CountHeavy[j+1].size = OMove(HeavyHitterList[i].size, CountHeavy[j+1].size, cond);
            CountHeavy[j+1].count = OMove(1, CountHeavy[j+1].count, cond);
        }
        numSize += OMove(0, 1, isFound);
    }
}


void TestMergeLightPart_NO(uint8_t *pdata1, uint8_t *pdata2){
    uint8_t *px = pdata1+1;
    uint8_t *py = pdata2+1;
    uint8_t *pz = pDataCUSketch;
    for(int i=0; i<CUSKETCH_MEM; i++){
        if(px[i]>py[i]){
            pz[i] = px[i];
        }else{
            pz[i] = py[i];
        }
    }
}

void TestMergeLightPart(uint8_t *pdata1, uint8_t *pdata2){
    uint8_t *px = pdata1+1;
    uint8_t *py = pdata2+1;
    uint8_t *pz = pDataCUSketch;
    for(int i=0; i<CUSKETCH_MEM; i++){
        pz[i] = OMove(px[i], py[i], px[i]>py[i]);
    }
}

void TestMergeLightPart_AVX(uint8_t *pdata1, uint8_t *pdata2, size_t size){
    uint8_t *px = pdata1;
    uint8_t *py = pdata2;
    for(int i=0; i<size / 32; i++){
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
}


void UpdateCUSketch_NO(uint8_t *pLight, size_t numLight, Item *pHeavy, size_t numHeavy){
    uint32_t idx = 0;
    uint32_t size = 0;
    uint8_t max = 0;
    for(int i=0; i<numHeavy; i++){
        idx = (SpookyHash::Hash32((void *)&pHeavy[i].flowID,
                FLOW_KEY_SIZE, cusHash)) % numLight;
        size = pHeavy[i].size;
        if(i<100) printf("%d %d %d", idx, size, pLight[idx]);
        if(size > 255) size = 255;
        if(size > pLight[idx]) pLight[idx] = size;
        if(i<100) printf("\t%d", pLight[idx]);
    }
}

void UpdateCUSketch_Scan(uint8_t *pLight, size_t numLight, Item *pHeavy, size_t numHeavy){
    uint32_t idx = 0;
    uint32_t size = 0;
    uint8_t max = 0;
    uint8_t cond =0;
    for(int i=0; i<numHeavy; i++){
        idx = (SpookyHash::Hash32((void *)&pHeavy[i].flowID,
                FLOW_KEY_SIZE, cusHash)) % numLight;
        size = pHeavy[i].size;
        if(size > 255) size = 255;
        for(int j=0; j<numLight; j++){
            cond = j == idx;
            max = OMove(size, pLight[j], size > pLight[j]);
            pLight[idx] = OMove(max, pLight[idx], cond);
        }
    }
}


uint32_t TestHeavyChange(Item *pdata1, Item *pdata2, size_t numHeavy){
    ODelta(pdata1, numHeavy, pdata2, numHeavy, 1);
    uint8_t level = ceil(log2(numHeavy));
    uint32_t length = 1 << level;
    BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
    bsItemDESC.sort((uint8_t *)pdata1, length*2);
    uint32_t count = 0;
    for(int i=0; i<numHeavy*2; i++){
        count += OMove(1, 0, pdata1[i].size>0);
    }
    return count;
}

unordered_map<uint32_t, uint32_t> get_stat_map(Item *pdata1) {
    unordered_map<uint32_t, uint32_t> result_map;
    for(int i = 0; i < NUM_HEAVY; i++) {
        result_map[pdata1[i].flowID] = pdata1[i].size;
    }
    return result_map;
}
uint32_t TestHeavyChange_NO_MAP(Item *pdata1, Item *pdata2, size_t numHeavy){
    unordered_map<uint32_t, uint32_t> detected_flow;
    // scan the cur statistics
    unordered_map<uint32_t, uint32_t> prev_statistics = get_stat_map(pdata1);
    unordered_map<uint32_t, uint32_t> cur_statistics = get_stat_map(pdata2);

    for(auto & it : cur_statistics) {
        prev_statistics[it.first] = fabs((int)prev_statistics[it.first] - (int)it.second);
    }

    uint32_t idx = 0;
    for(auto & it : prev_statistics) {
        HeavyChangeList[idx].flowID = it.first;
        HeavyChangeList[idx].size = it.second;
        idx++;
    }
    vector<Item> myvector (HeavyChangeList, HeavyChangeList+idx);
    sort (myvector.begin(), myvector.end(), greaterSItem);
    for(int i=0;i<idx;i++) {
        if(myvector[i].size<1){
            idx = i;
            break;
        }
        HeavyChangeList[i] = myvector[i];
    }
    return idx;
}


void TestHeavyHitter(size_t size){
    vector<Item> myvector (HeavyHitterList, HeavyHitterList+2*size);
    sort (myvector.begin(), myvector.end(), greaterSItem);
    memcpy((uint8_t *)(HeavyHitterList+2*size), (uint8_t *)&(myvector[0]), 2*size*sizeof(Item));
}

void OTestHeavyHitter(size_t size){
    if(testPo2(size)){
        OddEvenMergeSorter oeItemDESC(&smallerItem, sizeof(Item), false);
        oeItemDESC.sort((uint8_t *)HeavyHitterList, size*2);
    }else{
        BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
        bsItemDESC.sort((uint8_t *)HeavyHitterList, size*2);
    }
}

bool OTestShuffle(){
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
    //printf("Stash Heavy Size: %d\n", countStash);
    pSP = NULL;
    free(pData);
    pData = NULL;
    return true;
}

bool BuildSTable_NO(){
    uint32_t countStash_no = 0;
    uint8_t idcList[ST_BUCKET_NUM];
    memset(idcList, 0, ST_BUCKET_NUM);
    pDataStashTable = (Item *)malloc(ST_TABLE_MEM);
    pDataForStashHeavy = malloc_align(ST_STASH_MEM, 64, (uint8_t **)&pStashHeavy);
    int index;
    int failed = 0;
    int max_count = 0;
        
    for(int i = 0; i < NUM_HEAVY; i++){
        Item tmp = HeavyHitterList[i];
        uint16_t ind = OMove(abs(rand() % ST_BUCKET_NUM),
            (SpookyHash::Hash32((void *)&tmp.flowID, FLOW_KEY_SIZE, stHash)) % ST_BUCKET_NUM,
            (tmp.flowID == 0));
        if(idcList[ind] >= ST_BUCKET_SIZE) {
            if(countStash_no >= ST_STASH_NUM){
                printf("Build StashTable Failed!\n");
                return false;
            }
            pStashHeavy[countStash_no] = tmp;
            countStash_no++;
        } else {
            index = (ST_BUCKET_SIZE * ind + idcList[ind]);
            pDataStashTable[index] = tmp;
            idcList[ind]++;
        }
    }
    return true;
}

bool TestDecrypt(Message * pMsgSketch, struct ctx_gcm_s *pCtx){
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
    // printf("Two Sketches Received!");
    free(p2Sketch);
    p2Sketch = NULL;
    return true;
}

FOSketch *pOCUSketch = NULL;
FOSketch *pOSTable = NULL;
void *pData = NULL;
uint8_t *pData1 = NULL;
uint8_t *pData2 = NULL;
uint8_t noMoreTest = 0;
void ecall_fos_test(void *sa){
    size_t repeats = REPEAT_BASE;
    uint64_t *in = (uint64_t *)sa;
    static bool isOK = true;
    if(!isOK) return;
    // int cap_base=CAPACITY_BASE;//size_sk: 300KB
    // int cap_del = CAPACITY_DELTA;

    //Test stuff
    uint16_t topK = 20;
    float gamma = 0.05;
    Item *respHH = NULL;
    Item *respHC = NULL;
    BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
    //pData1 = ((uint8_t **)(in[8]))[0];
    //pData2 = ((uint8_t **)(in[8]))[1];
    Item *HHList = NULL;
    uint8_t level = 0;
    uint32_t length = 0;
    uint32_t numHC = 0;
    uint32_t delta = 0;
    uint32_t factor = in[10];
    uint32_t moreElem = in[11]; //100;
    uint64_t round = 0;
    //TODO: Need more effort to make the experiment easy to conduct.
    //TODO: Should add encryption and decryption for more test.
    if(in[0]==0) noMoreTest = 0;
    switch(in[0]+noMoreTest){
    case 0: printf("------------------------------\n"
                   "Rebuild data (metrics and global sketch):");
            noMoreTest = 0;
            //Notice: Mimic the heavy hitter in the previous epoch.
            //memcpy((uint8_t *)HeavyHitterList, ((uint8_t **)(in[8]))[0]+2+CUSKETCH_MEM, HEAVY_MEM);
            OPrepareData((Message *)(in[8]), &ctx);
            //------------------
            // //for Test PORAM performance:
            // //pDataForCUSketch = malloc_align(1024*(cap_base+cap_del*in[2]), 64, &pDataCUSketch);
            // // memcpy(pDataCUSketch, (uint8_t *)in[9], 1024*(cap_base+cap_del*in[2]));
            // printf("Memsize: %zu\n", CAPACITY_UNIT*in[2]);
            // pDataForCUSketch = malloc_align(CAPACITY_UNIT*in[2], 64, &pDataCUSketch);
            // memcpy(pDataCUSketch, (uint8_t *)in[9], CAPACITY_UNIT*in[2]);
            //------------------------------------
            //for merge two light parts
            // pData = malloc_align(CUSKETCH_MEM*32, 64, &pData1);
            // memcpy(pData1, (uint8_t *)in[9],CUSKETCH_MEM*32);
            //----------------------------------
            // //for heavy part flow distribution
            // pData = malloc_align(HEAVY_MEM*32, 64, &pData1);
            // memcpy(pData1, (uint8_t *)in[9],HEAVY_MEM*32);
            // //sort the data
            // if(testPo2(NUM_HEAVY/2+moreElem)){
            //     OddEvenMergeSorter oeItemDESC(&smallerItem, sizeof(Item), false);
            //     oeItemDESC.sort(pData1, (NUM_HEAVY/2+moreElem));
            // }else{
            //     BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
            //     bsItemDESC.sort(pData1, (NUM_HEAVY/2+moreElem));
            // }
            //-----------------------
            // //heavy-change candidates
            // pData1 = (uint8_t *)HeavyHitterList;
            // memcpy(pData1, (uint8_t *)in[9],HEAVY_MEM*32);
            // // level = ceil(log2(HEAVY_MEM*1));
            // // length = 1 << level;
            // pData2 = pData1 + (NUM_HEAVY*factor+moreElem)*HEAVY_PAIR_SIZE;
            // //11060 for one NUM_HEAVY.
            // delta = 11060*factor;
            // for(int i=0;i<NUM_HEAVY*factor+moreElem; i++){
            //     ((Item *)pData1)[i].flowID = i;
            //     ((Item *)pData2)[i].flowID = i+delta;
            // }
            //------------------------
            // //heavy-hitter candidates
            // pData1 = (uint8_t *)HeavyHitterList;
            // memcpy(pData1, (uint8_t *)in[9],HEAVY_MEM*32);
            // //11060 for one NUM_HEAVY.
            // // pData2 = pData1 + (NUM_HEAVY*factor+100)*HEAVY_PAIR_SIZE;
            // // delta = NUM_HEAVY*factor+100;
            // pData2 = pData1 + (NUM_HEAVY*factor+moreElem)*HEAVY_PAIR_SIZE;
            // delta = NUM_HEAVY*factor+moreElem;
            // //for(int i=0;i<NUM_HEAVY*factor+100; i++){
            // for(int i=0;i<NUM_HEAVY*factor+moreElem; i++){
            //     ((Item *)pData1)[i].flowID = i;
            //     ((Item *)pData2)[i].flowID = i+delta;
            // }
            // //for additional test for sort/merge
            // if(testPo2((NUM_HEAVY*factor+moreElem)*2)){
            //     OddEvenMergeSorter oeItemDESC(&smallerItem, sizeof(Item), false);
            //     oeItemDESC.sort(pData1, (NUM_HEAVY*factor+moreElem));
            //     oeItemDESC.sort(pData2, (NUM_HEAVY*factor+moreElem));
            // }else{
            //     // BitonicSorter bsItemASC(&greaterItem, sizeof(Item));
            //     // bsItemASC.sort(pData1, (NUM_HEAVY*factor+moreElem));
            //     // BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
            //     // bsItemDESC.sort(pData2, (NUM_HEAVY*factor+moreElem));
            //     BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
            //     bsItemDESC.sort(pData1, (NUM_HEAVY*factor+moreElem));
            //     bsItemDESC.sort(pData2, (NUM_HEAVY*factor+moreElem));
            // }
            ///////memcpy(pData1, ((uint8_t **)(in[8]))[0] + 2 + CUSKETCH_MEM, HEAVY_MEM);
            ///////memcpy(pData2, ((uint8_t **)(in[8]))[1] + 2 + CUSKETCH_MEM, HEAVY_MEM);
            //----------------------------------
            // //for merge the lower heavy part to the light part
            // pData = malloc_align((CUSKETCH_P2_MEM+HEAVY_MEM)*16, 64, &pData1);
            // memcpy(pData1, (uint8_t *)in[9],(CUSKETCH_P2_MEM+HEAVY_MEM)*16);
            // // level = ceil(log2(HEAVY_MEM*1));
            // // length = 1 << level;
            // pData2 = pData1 + CUSKETCH_P2_MEM*16;
            // // for(int i=HEAVY_MEM*1;i<length; i++){
            // //     ((Item *)pData2)[i].flowID = 0;
            // // }
            // printf("Test: factor = %d, moreElem = %d", factor, moreElem);
            break;
    case 1: printf("Do more tests after the data has been prepared:");
            // //---------------------------------------------------------
            // printf("Test Performance of sort vs. merge:");
            // // for(int i=0; i<10; i++){
            // //     printf("(%x %d) <-> (%x %d)", ((Item *)pData1)[i].flowID, ((Item *)pData1)[i].size,
            // //             ((Item *)pData2)[i].flowID, ((Item *)pData2)[i].size);
            // // }
            // if(testPo2((NUM_HEAVY*factor+moreElem)*2)){
            //     OddEvenMergeSorter oeItemDESC(&smallerItem, sizeof(Item), false);
            //     // oeItemDESC.sort(pData1, (NUM_HEAVY*factor+moreElem)*2);
            //     oeItemDESC.merge(pData1, (NUM_HEAVY*factor+moreElem)*2);
            // }else
            // {
            //     printf("Reverse the order\n");
            //     Item tmp;
            //     for(int k=0;k<(NUM_HEAVY*factor+moreElem)/2;k++){
            //         tmp = ((Item *)pData1)[k];
            //         ((Item *)pData1)[k] = ((Item *)pData1)[(NUM_HEAVY*factor+moreElem)-k];
            //         ((Item *)pData1)[(NUM_HEAVY*factor+moreElem)-k] = tmp;
            //     }
            //     BitonicSorter bsItemDESC(&smallerItem, sizeof(Item), false);
            //     // bsItemDESC.sort(pData1, (NUM_HEAVY*factor+moreElem)*2);
            //     bsItemDESC.merge(pData1, (NUM_HEAVY*factor+moreElem)*2);
            // }
            // // printf("After Detection");
            // // for(int i=0;i<20;i++){
            // //     printf("%x, %d <=> %x, %d\n", HeavyHitterList[i].flowID, HeavyHitterList[i].size,
            // //         HeavyHitterList[i+(NUM_HEAVY*factor+moreElem)*2-20].flowID, HeavyHitterList[i+(NUM_HEAVY*factor+moreElem)*2-20].size);
            // // }
            //---------------------------------------------------------
            // printf("Test Decryption of two skwtches:");
            // for(int i=0; i<100; i++){
            //     TestDecrypt((Message *)(in[8]), &ctx);
            // }
            //---------------------------------------------------------
            // printf("TestHeavyHitter:");
            // // for(int i=0; i<10; i++){
            // //     printf("(%x %d) <-> (%x %d)", ((Item *)pData1)[i].flowID, ((Item *)pData1)[i].size,
            // //             ((Item *)pData2)[i].flowID, ((Item *)pData2)[i].size);
            // // }
            // for(int i=0;i<10;i++){
            //     // OTestHeavyHitter(NUM_HEAVY*factor+moreElem);
            //     TestHeavyHitter(NUM_HEAVY*factor+moreElem);
            // }
            // // printf("After Detection");
            // // for(int i=0;i<20;i++){
            // //     printf("%x, %d <=> %x, %d\n", HeavyHitterList[i].flowID, HeavyHitterList[i].size,
            // //         HeavyHitterList[i+(NUM_HEAVY*factor+moreElem)*2-20].flowID, HeavyHitterList[i+(NUM_HEAVY*factor+moreElem)*2-20].size);
            // // }
            //---------------------------------------------------------
            // printf("TestHeavyChange:");
            // for(int i=0; i<10; i++){
            //     printf("(%x %d) <-> (%x %d)", ((Item *)pData1)[i].flowID, ((Item *)pData1)[i].size,
            //             ((Item *)pData2)[i].flowID, ((Item *)pData2)[i].size);
            // }
            // for(int i=0;i<10;i++){
            //     //numHC = TestHeavyChange((Item *)(pData1), (Item *)(pData2), NUM_HEAVY);
            //     numHC = OHeavyChange(NUM_HEAVY*factor+moreElem, BT_BUCKET_NUM*factor, 
            //             BT_BUCKET_MAX_NUM*factor, BT_BUCKET_MAX_SIZE*factor);
            //     //numHC = TestHeavyChange_NO_MAP((Item *)(pData1), (Item *)(pData2), NUM_HEAVY);
            // }
            // printf("After Detection, %d heavy change\n", numHC);
            // for(int i=numHC-10; i<numHC; i++){
            // // for(int i=0; i<10; i++){
            //     printf("(%x %d) <-> (%x %d)", HeavyChangeList[i].flowID, HeavyChangeList[i].size,
            //             ((Item *)pData2)[i].flowID, ((Item *)pData2)[i].size);
            // }
            //---------------------------------------------------------
            // printf("OUpdateCUSketch:");
            // // for(int i=0;i<180;i++){
            // //     printf("%d -> %d, %x, %d", i, pData1[i], 
            // //         ((Item *)pData2)[i].flowID, ((Item *)pData2)[i].size);
            // // }
            // for(int i=0;i<10;i++){
            //     //OUpdateCUSketch(pData1, CUSKETCH_MEM,(Item *)pData2, NUM_HEAVY);
            //     OUpdateCUSketch_BEx(pData1, CUSKETCH_P2_MEM*factor+moreElem,(Item *)pData2, NUM_HEAVY*factor+moreElem);
            //     //UpdateCUSketch_NO(pData1, CUSKETCH_MEM,(Item *)pData2, NUM_HEAVY);
            //     //UpdateCUSketch_Scan(pData1, CUSKETCH_MEM,(Item *)pData2, NUM_HEAVY);
            // }
            // printf("~~~\n");
            // for(int i=0;i<180;i++){
            //     printf("%d -> %d", i, pData1[i]);
            // }
            //---------------------------------------------------------
            // printf("TestMergeLightPart_AVX:");
            // //pData1 = (uint8_t *)in[9];
            // pData2 = pData1 + CUSKETCH_MEM*8;
            // // for(int i=0; i<100; i++){
            // //     printf("(%d %d)",pData1[i], pData2[i]);
            // // }
            // for(int i=0;i<10000;i++){
            //     TestMergeLightPart_AVX(pData1, pData2, CUSKETCH_MEM*16);
            //     //TestMergeLightPart(pData1, pData2);
            //     //TestMergeLightPart_NO(pData1, pData2);
            // }
            // // printf("~~\n");
            // // for(int i=0; i<100; i++){
            // //     printf("(%d %d)",pData1[i], pData2[i]);
            // // }
            //---------------------------------------------------------
            // printf("TestHeavyCountDist:");
            // HHList = (Item *)pData1;
            // // for(int i=0; i<100; i++){
            // //     printf("%d, %d | %x, %d", CountHeavy[i].size, CountHeavy[i].count,
            // //             HHList[i].flowID, HHList[i].size);
            // // }
            // for(int i=0;i<100;i++){
            //     // OItemCountDist_DESC(HHList, CountHeavy, NUM_HEAVY*factor+moreElem);
            //     OItemCountDist_DESC(HHList, CountHeavy, NUM_HEAVY/2+moreElem);
            //     // TestHeavyCountDist_SORT(HHList, NUM_HEAVY*factor+moreElem);
            //     //TestHeavyCountDist_SORT(HHList, NUM_HEAVY/2+100);
            //     //TestHeavyCountDist_SORT();
            //     //TestHeavyCountDist();
            //     //TestHeavyCountDist_NO();
            //     //TestHeavyCountDist_NO_MAP(HHList);
            //     //TestHeavyCountDist_NO_ORD(HHList);
            // }
            // // printf("~~~~\n");
            // // for(int i=0; i<100; i++){
            // //     printf("%d, %d | %x, %d", CountHeavy[i].size, CountHeavy[i].count,
            // //             HHList[i].flowID, HHList[i].size);
            // // }
            //---------------------------------------------------------
            // printf("Test Light Count Distribution:");
            // for(int i=0;i<10;i++){
            //     ClearCountLight();
            //     //TestLightCountDist_AVX();
            //     //TestLightCountDist();
            //     //TestLightCountDist_NO();
            //     TestLightCountDist_AVX((uint8_t *)in[9], CUSKETCH_MEM/2);
            // }
            //---------------------------------------------------------
            // printf("Test BuildSTable:");
            // for(int i=0;i<100;i++){
            //     //OBuildSTable();
            //     //BuildSTable_NO();
            //     OTestShuffle();
            // }
            //---------------------------------------------------------
            // topK = 200;
            // round = 10000;
            // printf("Test Response. (%d, %lu):", topK, round);
            // for(uint64_t i=0;i<round;i++){
            //     respHH = OResponseHeavyHitter(topK);
            //     free(respHH);
            //     respHH = NULL;
            //     // respHC = OResponseHeavyChange(gamma);
            //     // free(respHC);
            //     // respHC = NULL;
            // }
            // free(pData);
            // pData = NULL;
            break;
    case 2: //Generating PORAM:
            printf("Generating Global Sketch:\n"
                  "--Generating OCUSketch:");
            pOCUSketch = new FOSketch('C', in[7]);
            pOCUSketch->setObliv(in[4]);
            printf("--Generating OSTable:");
            pOSTable = new FOSketch('T', in[7]);
            pOSTable->setObliv(in[4]);
            // printf("Test PORAM Performance:\n");
            // // pOCUSketch = new FOSketch(1024*(cap_base+cap_del*in[2]), 8*(1<<in[1]), in[5], in[7]);
            // pOCUSketch = new FOSketch(CAPACITY_UNIT*in[2], 8*(1<<in[1]), in[5], in[7]);
            // pOCUSketch->setObliv(in[4]);
            if(!pOCUSketch->initOK() || !pOSTable->initOK()){
                free(pDataForCUSketch);
                pDataForCUSketch = NULL;
                free(pOSTable);
                pOSTable = NULL;
                printf("Init PORAMs Error, No more test!\n");
                noMoreTest = 10;
            }
            break;
    case 3: printf("Do more tests after the generation of OBS :");
            //printf("Test Insert Lower Heavy-hitters:");
            //mimic maxmerge the lower part to OCUSketch
            // for(int i=0;i<10;i++){
            //     pOCUSketch->BurstAccess(NUM_HEAVY);
            // }
            //printf("compared with linear scan...");
            //pOSTable->BurstAccess(20000);
            break;
    case 4: printf("Burst Access Test:");
            pOCUSketch->BurstAccess(in[3]);
            pOSTable->BurstAccess(in[3]);
            // pOCUSketch->BurstAccess(repeats*in[3]);
            // pOSTable->BurstAccess(repeats*in[3]);
            // pOCUSketch->LinearAccess(repeats*in[3]);
            // pOSTable->LinearAccess(repeats*in[3]);
            break;
    case 5: printf("Show Short Info:");
            pOCUSketch->shortInfo();
            pOSTable->shortInfo();
            break;
    case 6: printf("Free the Global Sketch:");
            pOCUSketch->free_OCU();
            pOCUSketch = NULL;
            pOSTable->free_OCU();
            pOSTable = NULL;
            printf("------------------------------");
            break;
    }
}
void ecall_bare(void *sa){n_ecall_bare++;ocall_bare(n_ecall_bare);}
void ecall_print_count(){printf("switchless: %d, bare: %d\n", n_ecall_bare_switchless, n_ecall_bare);}