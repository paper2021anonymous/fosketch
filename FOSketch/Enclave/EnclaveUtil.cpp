#include "EnclaveUtil.h"
#include <assert.h>

#define assert(a)

using namespace std;

void printf( const char *fmt, ...)
{
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
}

// //For Intel(R) SGX
// static int rand(void)
// {
//     int num=0;
//     sgx_read_rand((unsigned char*)&num, sizeof(int));
//     return num;
// }
