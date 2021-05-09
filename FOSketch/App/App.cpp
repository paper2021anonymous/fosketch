#include <fstream>
#include <iostream>
#include <pthread.h>
#include <vector>
#include <algorithm>
#include <arpa/inet.h>
#include <sgx_uswitchless.h>
#include <assert.h>

//#include <time.h>
#include<unistd.h>
#include <sys/time.h>
#include <getopt.h>

#include "sgx_urts.h"
#include "Enclave_u.h"
#include "../../Common/Queue.h"

#define ENCLAVE_FILE "Enclave.signed.so"

using namespace std;

#define TRACE_FILE_PREFIX "sketch.data"
#define START_FILE_NO 1
#define END_FILE_NO 2
#define REPEATS 10000
#define DEFAULT_PACE 1000000

#define MICROSECOND 1000000

/* Global EID shared by multiple threads */
sgx_enclave_id_t global_eid = 0;
typedef struct _sgx_errlist_t {
    sgx_status_t err;
    const char *msg;
    const char *sug; /* Suggestion */
} sgx_errlist_t;


// setup OVS parameters
struct ctx_gcm_s global_ctx = {};


int global_process_flag = 0;


int ecall_switchless = 0;
int ocall_switchless = 0;
int repeats = REPEATS;

int n_ocall_bare_switchless = 0;
int n_ocall_bare = 0;
void ocall_bare_switchless(int count) {
    n_ocall_bare_switchless++;
}
void ocall_bare(int count) {
    n_ocall_bare++;
}

void ocall_print_count(){printf("switchless: %d, bare: %d\n", n_ocall_bare_switchless, n_ocall_bare);}



void ocall_print_string(const char *str) {
    printf("%s\n", str);
}


// void ocall_alloc_message(void *ptr, size_t size) {
//     Message *msg = (Message*) ptr;
//     msg->payload = (uint8_t*) malloc(size);
// }

/* Error code returned by sgx_create_enclave */
static sgx_errlist_t sgx_errlist[] = {
    {
        SGX_ERROR_UNEXPECTED,
        "Unexpected error occurred.",
        NULL
    },
    {
        SGX_ERROR_INVALID_PARAMETER,
        "Invalid parameter.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_MEMORY,
        "Out of memory.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_LOST,
        "Power transition occurred.",
        "Please refer to the sample \"PowerTransition\" for details."
    },
    {
        SGX_ERROR_INVALID_ENCLAVE,
        "Invalid enclave image.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ENCLAVE_ID,
        "Invalid enclave identification.",
        NULL
    },
    {
        SGX_ERROR_INVALID_SIGNATURE,
        "Invalid enclave signature.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_EPC,
        "Out of EPC memory.",
        NULL
    },
    {
        SGX_ERROR_NO_DEVICE,
        "Invalid SGX device.",
        "Please make sure SGX module is enabled in the BIOS, and install SGX driver afterwards."
    },
    {
        SGX_ERROR_MEMORY_MAP_CONFLICT,
        "Memory map conflicted.",
        NULL
    },
    {
        SGX_ERROR_INVALID_METADATA,
        "Invalid enclave metadata.",
        NULL
    },
    {
        SGX_ERROR_DEVICE_BUSY,
        "SGX device was busy.",
        NULL
    },
    {
        SGX_ERROR_INVALID_VERSION,
        "Enclave version was invalid.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ATTRIBUTE,
        "Enclave was not authorized.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_FILE_ACCESS,
        "Can't open enclave file.",
        NULL
    },
};

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret)
{
    size_t idx = 0;
    size_t ttl = sizeof sgx_errlist/sizeof sgx_errlist[0];

    for (idx = 0; idx < ttl; idx++) {
        if(ret == sgx_errlist[idx].err) {
            if(NULL != sgx_errlist[idx].sug)
                printf("Info: %s\n", sgx_errlist[idx].sug);
            printf("Error: %s\n", sgx_errlist[idx].msg);
            break;
        }
    }

    if (idx == ttl)
        printf("Error: Unexpected error occurred.\n");
}

/* Initialize the enclave:
 *   Call sgx_create_enclave to initialize an enclave instance
 */
///*
int initialize_enclave(const sgx_uswitchless_config_t* us_config)
{
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;

    const void* enclave_ex_p[32] = { 0 };

    enclave_ex_p[SGX_CREATE_ENCLAVE_EX_SWITCHLESS_BIT_IDX] = (const void*)us_config;
//sgx_create_enclave(ENCLAVE_FILE, SGX_DEBUG_FLAG, &token, &token_updated, &eid, NULL);
    ret = sgx_create_enclave_ex(ENCLAVE_FILE, SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL, SGX_CREATE_ENCLAVE_EX_SWITCHLESS, enclave_ex_p);
    if (ret != SGX_SUCCESS) {
        print_error_message(ret);
        return -1;
    }

    return 0;
}
//*/

//--------------For Fast Oblivious Sketch:----------------

uint8_t DataSketch[2*(TOTAL_MEM + 2)];
Message msgSketch;

inline bool greaterSItem(Item pi, Item pj){
    return pi.size > pj.size;
}
void readSketchFile(const char *trace_prefix) {
    uint8_t *pData = NULL;
    for(int datafileCnt = 0; datafileCnt <= 1; ++datafileCnt)
    {
        char datafileName[100];
        sprintf(datafileName, "%s%d", trace_prefix, datafileCnt);
        FILE *fin = fopen(datafileName, "rb");

        if(fin == NULL){
            perror("File(s) Not Found, Force to exit!\n");
            exit(-1);
        }
        pData = &DataSketch[datafileCnt*(TOTAL_MEM + 2)];
        size_t aaa = fread(pData, sizeof(uint8_t), (TOTAL_MEM + 2), fin);
        if(aaa != (TOTAL_MEM + 2)){
            printf("Data Format of %s is Wrong! aaa = %d\n", datafileName, aaa);
            exit(-1);
        }
        //sort the data:
        vector<Item> myvector ((Item *)(pData+2+CUSKETCH_MEM), (Item *)(pData+2+CUSKETCH_MEM)+NUM_HEAVY);
        sort (myvector.begin(), myvector.end(), greaterSItem);
        memcpy(pData+2+CUSKETCH_MEM, (uint8_t *)&(myvector[0]), HEAVY_MEM);

        // for(int i=0; i<10; i++){
        //     printf("%x, %d\n", ((Item *)(pData+2+CUSKETCH_MEM)+NUM_HEAVY-10)[i].flowID,
        //             ((Item *)(pData+2+CUSKETCH_MEM)+NUM_HEAVY-10)[i].size);
        // }

        fclose(fin);
    }
    printf("\n");
}

void FOS_test_ecall(Message * pMsgSketch, uint32_t cap,
        uint32_t rept,uint8_t isObliv,uint8_t sizeBlockH,uint8_t sizeBlockL,
        uint8_t sizeBk,uint32_t nKey=CUCKOO_KEYS, uint8_t compStash = 1,
        uint32_t tFactor=1, uint32_t tElmt=0) 
{
    srand (time(NULL));
    rept = rept < 1 ? 1 : rept;
    //rept = rept > 100 ? 100 : rept;
    sizeBlockH = sizeBlockH > 7 ? 7 : sizeBlockH;
    sizeBlockL = sizeBlockL < 1 ? 1 : sizeBlockL;
    sizeBk = sizeBk > 8 ? 8 : sizeBk;
    sizeBk = sizeBk < 2 ? 2 : sizeBk;
    uint8_t tmp;
    if(sizeBlockH < sizeBlockL){
        tmp = sizeBlockH;
        sizeBlockH = sizeBlockL;
        sizeBlockL = tmp;
    }
    //cap = cap > 18 ? 18 : cap;  // 2^15
    //cap = cap > 1920 ? 1920 : cap;
    char mark[8] = {'P', 'T', 'I', 'T', 'A', 'S', 'F', 'X'};
    uint64_t repeat = rept * REPEAT_BASE / 1000;
    uint8_t *pLargeData = NULL;
    Item *pLargeItems = NULL;

    // --------------------------------------------
    //Allocate enough mem for more test:
    // --------------------------------------------
    // //for light part counter distribution
    // //for merge two light parts
    // pLargeData = (uint8_t *)malloc(CUSKETCH_MEM*32);
    // for(int k=0;k<CUSKETCH_MEM*32;k++){
    //     pLargeData[k] = rand() % 256;
    // }
    // //for test PORAM performance
    // pLargeData = (uint8_t *)malloc(CAPACITY_UNIT*cap);
    // for(int k=0;k<CAPACITY_UNIT*cap;k++){
    //     pLargeData[k] = rand() % 256;
    // }
    // --------------------------------------------
    // //for merge the lower heavy part to the light part
    // pLargeData = (uint8_t *)malloc((CUSKETCH_P2_MEM+HEAVY_MEM)*16);
    // for(int k=0;k<CUSKETCH_P2_MEM*16;k++){
    //     pLargeData[k] = rand() % 256;
    // }
    // pLargeItems = (Item *)(pLargeData+CUSKETCH_P2_MEM*16);
    // for(int k=0;k<NUM_HEAVY*16;k++){
    //     pLargeItems[k].size = rand() % 500+255;
    //     pLargeItems[k].flowID = rand() % 36800;
    // }
    //------------------------------------
    // //for heavy part flow distribution
    // //heavy-change candidates
    // pLargeData = (uint8_t *)malloc(HEAVY_MEM*32);
    // for(int k=0;k<NUM_HEAVY*32;k++){
    //     ((Item *)pLargeData)[k].size = rand() % 500;
    //     ((Item *)pLargeData)[k].flowID = rand() % 36800;
    // }
    //------------------------------------
    // // for heavy-hitter candidates
    // pLargeData = (uint8_t *)malloc(HEAVY_MEM*32);
    // for(int k=0;k<NUM_HEAVY*32;k++){
    //     ((Item *)pLargeData)[k].size = rand() % 500+1;
    //     //((Item *)pLargeData)[k].flowID = rand() % 36800;
    // }

    for (int i = sizeBlockH; i>=sizeBlockL; i--) {
        //uint32_t total = (CAPACITY_BASE+CAPACITY_DELTA*cap);
        uint16_t block = 8*(1<<i);
        for(int j=0;j<=6;j++){
        // for(int j=0;j<=1;j++){
            if(j==2){
                printf("(%c %c %dB %dKB %dK)\n",
                    isObliv ?'O':'N',
                    compStash ? 'C':'N',
                    block,1UL*CAPACITY_UNIT/1024*cap, repeat);
                    // block,total, repeat);
            }
            struct timeval tval_before, tval_after, tval_result;
            if(j < 5) {
                gettimeofday(&tval_before, NULL);
            }
            //CAPACITY_BASE+CAPACITY_DELTA*sa[2] repeats: REPEAT_BASE*sa[3] block_size: 8*(1<<sa[1])
            
            // uint64_t sa[9] = {j,i,
            //         cap,rept,isObliv,sizeBk,nKey,compStash,
            //         (uint64_t)pMsgSketch};
            uint64_t sa[12] = {j,i,
                    cap,rept,isObliv,sizeBk,nKey,compStash,
                    (uint64_t)pMsgSketch, (uint64_t)pLargeData,
                    tFactor, tElmt};
            ecall_fos_test(global_eid, sa);

            if(j < 5 ) {
                gettimeofday(&tval_after, NULL);
                timersub(&tval_after, &tval_before, &tval_result);
                printf("(%c %ld.%06lds)\n", mark[j],
                    (long int)tval_result.tv_sec, (long int)tval_result.tv_usec);
            }
            //usleep(100000);
        }
    }
    //if test PORAM:
    free(pLargeData);
    pLargeData = NULL;
}

void showconfig() {
    printf("ecall_switchless is %d, ocall_switchless is %d, repeats is %d\n",
             ecall_switchless, ocall_switchless, repeats);
}

static void usage(char* binname)
{
    printf("%s [-e] [-o] [-f traceFilePrefix] [-c] [-r #] [-h]\n", binname);
    printf("\t-O : set controller Oblivious, is default\n");
    printf("\t-N : set controller Normal\n");
    printf("\t-C : set Stash Compressing, is default\n");
    printf("\t-c : set Stash Not Compressing\n");
    printf("\t-R : set access repeats, 1 default, %d*R times\n", REPEAT_BASE);
    printf("\t-f : pass traceFilePrefix, default: %s\n", TRACE_FILE_PREFIX);
    printf("\t-h : show usage.\n");
    printf("-------Following only works for test:--------\n");
    printf("\t-M : set memory size, %d default, (M)KB\n", CAPACITY_DEFALUT);
    printf("\t-H : set sizeBlockH, 3 default, Max block size: 8*(1<<H)\n");
    printf("\t-L : set sizeBlockL, 3 default, Min block size: 8*(1<<L)\n");
    printf("\t-B : set Bucket size, 5 default\n");
    printf("\t-E : Test more element, default 0\n");
}

int main(int argc, char **argv) {
    char * traceFilePrefix = TRACE_FILE_PREFIX;
    ecall_switchless = 0;
    ocall_switchless = 0;
    int creat_trace = 0;
    int cut_trace = 0;
    repeats = REPEATS;
    uint8_t isObliv = 1;
    // uint8_t capacity_index = 4;
    uint32_t capacity_index = CAPACITY_DEFALUT;
    uint32_t repeat_index = 1;
    uint8_t sizeBlockH = 3, sizeBlockL = 3, sizeBk = 5 ;

    uint32_t nKey = CUCKOO_KEYS;
    uint8_t compStash = 1;
    uint32_t tFactor = 1;
    uint32_t tElmt = 0;

    char ch;
    while ((ch = getopt(argc, argv,
            "e"
            "o"
            "f:"
            "t"
            "r:"
            "x"
            "O"
            "N"
            "C"
            "c"
            "M:"
            "R:"
            "H:"
            "L:"
            "B:"
            "K:"
            "F:"
            "E:"
            "h"
            )) != -1) {
        switch (ch) {
        case 'e': ecall_switchless = 1; break;
        case 'o': ocall_switchless = 1; break;
        case 'O': isObliv = 1; break;
        case 'N': isObliv = 0; break;
        case 'C': compStash = 1; break;
        case 'c': compStash = 0; break;
        case 'M': capacity_index = atoi(optarg); break;
        case 'R': repeat_index = atoi(optarg); break;
        case 'H': sizeBlockH = atoi(optarg); break;
        case 'L': sizeBlockL = atoi(optarg); break;
        case 'B': sizeBk = atoi(optarg); break;
        case 'K': nKey = atoi(optarg); break;
        case 'F': tFactor = atoi(optarg); break;
        case 'E': tElmt = atoi(optarg); break;
        case 'f': traceFilePrefix = optarg; break;
        case 't': creat_trace = 1; break;
        case 'x': cut_trace = 1; break;
        case 'r': repeats = atoi(optarg); break;
        case 'h': usage(argv[0]); exit(0); break;
        default:
            usage(argv[0]);
            exit(-1);
        }
    }

    readSketchFile(traceFilePrefix);

    // init ctx block
    alloc_gcm(&global_ctx);//STAT, &global_ctx
    pack_message(&msgSketch, STAT, &global_ctx, DataSketch, 2*(2+TOTAL_MEM), 0);

    // Configuration for Switchless SGX 
    sgx_uswitchless_config_t us_config = SGX_USWITCHLESS_CONFIG_INITIALIZER;
    us_config.num_uworkers = 2;
    us_config.num_tworkers = 2;

    // Initialize the enclave
    if(initialize_enclave(&us_config) < 0)
    {
        printf("Error: enclave initialization failed\n");
        return -1;
    }

    // initialise the enclave with message queues
    ecall_init(global_eid, global_ctx.key, GCM_KEY_SIZE);


    printf("Start Test\n");
    struct timeval tval_before, tval_after, tval_result;
    gettimeofday(&tval_before, NULL);

    printf("-----------------------------------------------\n");
    printf("Cap: %dKB, Rep: %dK, Obliv: %d, BlockH: %dB, BlockL: %dB\n",
                //CAPACITY_BASE+CAPACITY_DELTA*capacity_index,
                1UL*CAPACITY_UNIT*capacity_index/1024,
                REPEAT_BASE*repeat_index/1000, isObliv,
                8*(1<<sizeBlockH), 8*(1<<sizeBlockL));
    FOS_test_ecall(&msgSketch, capacity_index, repeat_index, isObliv,
        sizeBlockH,sizeBlockL,sizeBk,nKey,compStash,tFactor,tElmt);
    printf("-----------------------------------------------\n");

    gettimeofday(&tval_after, NULL);
    timersub(&tval_after, &tval_before, &tval_result);

    printf("Stop Test\n");
    printf("Total time elapsed: %ld.%06ld seconds in this test\n", (long int)tval_result.tv_sec, (long int)tval_result.tv_usec);

    // destroy the enclave
    sgx_destroy_enclave(global_eid);
}
