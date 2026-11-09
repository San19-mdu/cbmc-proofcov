int dec(int a, int b, int c)
{
  int r;
  if((a > 0 && b > 0) || c > 0)
    r = 1;
  else
    r = 0;
  return r;
}

void main()
{
  int r = dec(1, 0, 0);
  __CPROVER_assert(r == 0, "r");
}
