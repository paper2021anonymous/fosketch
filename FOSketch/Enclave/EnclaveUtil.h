#ifndef MEASUREMENT_ENCLAVEUTIL_H
#define MEASUREMENT_ENCLAVEUTIL_H

#include <string>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <math.h>
#include <numeric>
#include <unordered_map>
#include <vector>

#include "sgx_trts.h"
#include "Enclave_t.h"
#include "../Common/Message.h"
#include "../Common/Queue.h"


using namespace std;

void testCuckHash(uint32_t nKey);

// bool Init_OCU(size_t w_sk, size_t sizeBl, size_t sizeBk, uint8_t compStash);
// void LinearAccess(uint32_t repeats);
// void BurstAccess(uint32_t repeats);
// uint8_t access(size_t index);
// uint8_t OAccess(size_t index);
// void showInfo();
// void shortInfo();
// void setObliv(uint8_t val);
// void free_OCU();
// void OTestCount();
// void OTestGet();
// bool Init_BT();

void printf( const char *fmt, ...);

#endif //MEASUREMENT_ENCLAVEUTIL_H
