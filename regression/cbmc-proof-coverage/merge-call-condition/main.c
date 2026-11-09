unsigned h(unsigned p, unsigned q)
{
  if(p > q)
    return p - q;
  return q - p;
}

unsigned g(unsigned a, unsigned b)
{
  if(a < 2 || h(a, b) > 1)
    return 7;
  if(b > 100)
    return 9;
  return 0;
}

void main()
{
  __CPROVER_assert(g(5, 1) == 7, "r");
}
