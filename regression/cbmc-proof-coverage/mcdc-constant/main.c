int halvings(int n)
{
  int steps = 0;
  while(1)
  {
    if(n <= 1)
      break;
    n /= 2;
    steps++;
  }
  return steps;
}

void main()
{
  __CPROVER_assert(halvings(4) == 2, "steps");
}
