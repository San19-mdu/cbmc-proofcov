void main()
{
  int x = 4;
  int y = x * 3 + 1;
  int r;
  if(y > 0)
    r = 1;
  else
    r = 1;
  __CPROVER_assert(r == 1, "r");
}
