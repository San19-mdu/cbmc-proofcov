int count_small(int a, int b, int c)
{
  int v[3];
  int i, n = 0;
  v[0] = a;
  v[1] = b;
  v[2] = c;
  for(i = 0; i < 3; i++)
    if(v[i] > 0 && v[i] < 10)
      n++;
  return n;
}

void main()
{
  __CPROVER_assert(count_small(5, 20, -1) == 1, "n");
}
