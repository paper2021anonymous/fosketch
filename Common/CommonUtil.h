//
// Created by shangqi on 8/2/20.
//

#ifndef MEASUREMENT_COMMONUTIL_H
#define MEASUREMENT_COMMONUTIL_H

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define MAX_POOL_SIZE 16384

#define ORAM_DATA_SIZE 1
#define ORAM_BUCKET_SIZE 5
#define ORAM_STASH_SIZE 105  // fp rate 2^(-128) accroding to the PathORAM paper

#define GCM_KEY_SIZE	16
#define GCM_IV_SIZE     12
#define GCM_MAC_SIZE    16


//VIVIAN
//#define NEED_TEST
typedef uint16_t bid_t;

#define DUMMY_FLAG (numeric_limits<bid_t>::max())
#define DUMMY_FLOW (numeric_limits<uint32_t>::max())
#define MAX_SIZE (numeric_limits<uint32_t>::max())

typedef struct Block64{
    bid_t blockID;
    bid_t pos;
    uint8_t data[64-2*sizeof(bid_t)];
}Block64;

typedef struct Item
{
    uint32_t flowID;
    uint32_t size;
}Item;

typedef struct CDist
{
    uint32_t size;
    uint32_t count;
}CDist;

//#define REPEAT_BASE 64*(1<<10)
#define REPEAT_BASE 10000
// #define CAPACITY_BASE 100
// #define CAPACITY_DELTA 50
#define CAPACITY_UNIT 1024
#define CAPACITY_DEFALUT 450

//#define TOTAL_MEM 600 * 1024    // 600 KB
//#define HEAVY_MEM (150 * 1024)
//#define BUCKET_NUM (HEAVY_MEM / 64)

//cache line version
#define CACHE_LINE 64

//From Merge Sketch!!!
//#define TOTAL_MEM 600 * 1024    // 600 KB
//(450 * 1024 = 460800)
#define CUSKETCH_MEM 460800
// #define CUSKETCH_MEM 491520
//(480 * 1024 = 491520)
#define CUSKETCH_P2_MEM 491520
//each bucket contain 64Bytes, with only 7 flowID-size pair
//Elastic Sketch and OblivSketch setting
//#define HEAVY_MEM (150 * 1024 / 64 * 7 * 8)
//If the num of heavy hitter is 16800, the PORAM is not efficient.
//set to 128KB saving alot of memory:
//(T O C 64B 5 224KB 3823 11 7 S146 1.490324MB 10000 +0 S3)
//#define HEAVY_MEM (128 * 1024 / 64 * 7 * 8)
//HEAVY_MEM set to 131072 has better performance, saving 0.4s:
//(T O C 64B 5 256KB 4370 12 7 S210 2.774754MB 10000 +0 S2)
#define HEAVY_MEM 131072
//Elastic Sketch and OblivSketch setting
//#define HEAVY_MEM 134400
//(CUSKETCH_MEM+HEAVY_MEM)
#define TOTAL_MEM (CUSKETCH_MEM+HEAVY_MEM)
#define HEAVY_KEY_SIZE 4
#define HEAVY_VALUE_SIZE 4
//(HEAVY_KEY_SIZE+HEAVY_VALUE_SIZE)
#define HEAVY_PAIR_SIZE 8
//(HEAVY_MEM / HEAVY_PAIR_SIZE)
//16384
#define NUM_HEAVY (HEAVY_MEM / HEAVY_PAIR_SIZE)
//#define NUM_HEAVY 16800
//#define SIZE_TABLE HEAVY_MEM
#define SIZE_BUCKET 5
#define SIZE_BLOCK 64

//for heavy hitter shuffle
//#define SORT_BLOCK_SIZE (HEAVY_PAIR_SIZE+4)
typedef struct shufflePair{
    bid_t index;
    bid_t dum0;
    uint32_t dum1;
    Item elem;
}ShufflePair;
#define SHUFF_PAIR_SIZE (sizeof(ShufflePair))
//for bucket table
#define BT_FACTOR 4
#define BT_BUCKET_SIZE 16
#define BT_BUCKET_MAX_SIZE 32
#define BT_BUCKET_MAX_NUM 100
//(HEAVY_PAIR_SIZE)
#define BT_BLOCK_SIZE HEAVY_PAIR_SIZE
#define BT_BUCKET_ADD 0
#define BT_BUCKET_NUM (NUM_HEAVY * 2 * BT_FACTOR / BT_BUCKET_SIZE)
//#define BT_BUCKET_NUM 2100
#define BT_TABLE_MEM ((BT_BUCKET_NUM+BT_BUCKET_ADD)*BT_BUCKET_SIZE*BT_BLOCK_SIZE)
//for stash bucket table
//#define ST_FACTOR 2
#define ST_BLOCK_SIZE 8
#define ST_BUCKET_SIZE 7
//#define ST_TABLE_MEM (NUM_HEAVY*ST_FACTOR*ST_BLOCK_SIZE)
//Saving memory greatly!
//Note that the fact ST_FACTOR is smaller than 2 and is 1.75 good!!!
#define ST_TABLE_MEM 229376
#define ST_BUCKET_NUM 4096
//#define ST_BUCKET_NUM (NUM_HEAVY*ST_FACTOR / ST_BUCKET_SIZE)
#define ST_STASH_NUM 1000
#define ST_STASH_MEM (ST_STASH_NUM*ST_BLOCK_SIZE)

//#define MAX_STASH_SIZE (128*1024)
#define MAX_STASH_NUM 105
//#define MAX_STASH_SIZE (SIZE_BLOCK*MAX_STASH_NUM)
// #define DEFAULT_STASH_LENGTH 25
#define DEFAULT_STASH_LENGTH 5
// #define DEFAULT_STASH_LENGTH MAX_STASH_NUM
#define PACE_STASH 5
#define DUMMY_FLAG (numeric_limits<bid_t>::max())
//#define ROUNDS 1
#define ROUNDS 1

inline bool testPo2(uint64_t num){
    uint8_t level = ceil(log2(num));
    if(num == (1L << level)) return true;
    return false;
}
//---------------------------------------------------------

struct FIVE_TUPLE {
    char key[13];
};

struct FLOW_KEY {   // 13 bytes
    // 8 (4*2) bytes
    uint32_t src_ip;  // source IP address
    uint32_t dst_ip;
    // 4 (2*2) bytes
    uint16_t src_port;
    uint16_t dst_port;
    // 1 bytes
    uint8_t proto;
};

#define COUNTER_PER_BUCKET 8

#define bool_extend(val) (-(val) >> 32)
#define get_min(x, y) ((uint32_t) y & bool_extend(x > y)) | ((uint32_t) x & bool_extend(x <= y))
#define selector(x, y, bit) ((uint32_t) x & bool_extend(bit)) | ((uint32_t) y & bool_extend(!bit))
#define swap_threshold(negative_val, val) (negative_val > (val << 3))
#define get_flag(val) (((uint32_t)(val) & 0x80000000) == 0x80000000)
#define get_val(val) ((uint32_t)((val) & 0x7FFFFFFF))

// sketch definitions
//#define TOTAL_MEM 600 * 1024    // 600 KB
#define FLOW_KEY_SIZE 4

#define FLOW_ID_SIZE sizeof(struct FIVE_TUPLE)
#define HEAVY_HITTER_SIZE 20
#define HEAVY_CHANGE_THRESHOLD 0.05

#ifdef __cplusplus
extern "C" {
#endif

struct ctx_gcm_s {
    unsigned char key[GCM_KEY_SIZE];	///< encryption key
    unsigned char IV[GCM_IV_SIZE];	///< should be incremeted at both sides
};

void incr_ctr(char *ctr, int size);

int gcm_encrypt(unsigned char *plaintext, int plaintext_len,
                unsigned char *key, unsigned char *iv,
                unsigned char *ciphertext,
                unsigned char *mac);

int gcm_decrypt(unsigned char *ciphertext, int ciphertext_len,
                unsigned char *mac,
                unsigned char *key, unsigned char *iv,
                unsigned char *plaintext);

void alloc_gcm(struct ctx_gcm_s *ctx);

#ifdef __cplusplus
}
#endif

#endif //MEASUREMENT_COMMONUTIL_H
