#include <assert.h>

int main()
{
  int x;
  int y = x + 1;
  assert(y > x);
  return 0;
}
