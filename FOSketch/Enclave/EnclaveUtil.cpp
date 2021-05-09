#include "EnclaveUtil.h"

#include "cuckoo_hash.h"
#include <assert.h>

#define assert(a)

using namespace std;

//Hash
//-----------------------------
struct Data
{
  uint32_t key;
  uint32_t value;
};

inline
cuckoo_hash *
create()
{
  cuckoo_hash *hash = new cuckoo_hash;
  if (! cuckoo_hash_init(hash, 1))
    return NULL;

  return hash;
}
inline
double
load_factor(cuckoo_hash *cont)
{
  /* Peek hash size.  */
  return (static_cast<double>(cuckoo_hash_count(cont))
          / (static_cast<size_t>(cont->bin_size) << cont->power));
}

inline
bool
insert(cuckoo_hash *cont, Data *d)
{
  if (cuckoo_hash_insert(cont, d->key, d->value)
      == CUCKOO_HASH_FAILED)
    return false;
  return true;
}

inline
int
lookup(cuckoo_hash *cont, const Data *d)
{
  const cuckoo_hash_item *it =
    cuckoo_hash_lookup(cont, d->key);
  if (it != NULL)
    {
      /* Let's be fair and access actual data.  */
      assert(it->value == d->value);
      return 1;
    }

  return 0;
}

inline
void
remove(cuckoo_hash *cont, const Data *d)
{
  cuckoo_hash_remove(cont,
                     cuckoo_hash_lookup(cont, d->key));
}

inline
size_t
traverse(cuckoo_hash *cont)
{
  size_t sum = 0;
  for (const cuckoo_hash_item *cuckoo_hash_each(it, cont))
    sum += it->value;

  return sum;
}

typedef cuckoo_hash cont_type;

void testCuckHash(uint32_t nKey){

  uint32_t count = nKey;

  int repeat = 1;
  // Allocate a bit more keys to do non-existent queries.
  uint32_t total = count * 1.1;

  Data *pdata = new Data[total];
  for (int i = 0; i < total; ++i)
    {
      //std::ostringstream key;
      pdata[i].key = i;
      pdata[i].value = i;
    }


  cont_type *cont = create();

  size_t sum = 0;
  for (int i = 0; i < count; ++i)
    {
      assert(insert(cont, &pdata[i])==true);
      sum += pdata[i].value;
    }
  printf("(%d %d %d %d %.3f)",count, cont->count, cont->power, cont->bin_size, load_factor(cont));

    //

  // std::cout << "insert: "
  //           << static_cast<double>(stop - start) / CLOCKS_PER_SEC << " sec"
  //           << std::endl;

  for (int j = 0; j < repeat; ++j)
    {
      int found = 0;
      for (int i = 0; i < total; ++i){
        found += lookup(cont, &pdata[i]);
      }
      assert(found == count);
    }

  cuckoo_hash_destroy(cont);
}

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
