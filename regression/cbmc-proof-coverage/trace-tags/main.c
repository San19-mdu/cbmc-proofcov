#include <assert.h>

void main()
{
  int a = 1;
  int b;

  if(a == 1)
    b = 1;
  else
    b = 2;

  assert(b > 0);
}
