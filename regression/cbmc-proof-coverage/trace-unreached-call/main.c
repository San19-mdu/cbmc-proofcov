unsigned h(unsigned p, unsigned q)
{
  if(p > q)
    return p - q;
  return q - p;
}

void main()
{
  unsigned a = 1;
  unsigned b;
  if(a == 1)
    b = 1;
  else
    b = h(3, 2);
  __CPROVER_assert(b > 0, "b");
}
