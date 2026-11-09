#include <assert.h>

int is_odd_prime(unsigned n)
{
  unsigned i;
  for(i = 3; i < n / 2; i += 2)
    if(n % i == 0)
      return 0;
  return 1;
}

void main()
{
  unsigned n;
  __CPROVER_assume(n == 5 || n == 7);
  int r = is_odd_prime(n);
  assert(r == 1);
}
