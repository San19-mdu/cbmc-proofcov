int g(int a, int b, int c)
{
  int t = 0;
  int u = c > 0 ? (a > 1 || b > 1) : (a < 0 && b < 0);
  if(c < 0)
    t = a > 0 && b > 0;
  if(t || c > 5) t = 2; if(b > 3) t = 3;
  return (a > 0 || b > 0) && c != 3;
}

void main()
{
  __CPROVER_assert(g(1, 0, 2) == 1, "r");
}
