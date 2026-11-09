int nondet_int();

void main()
{
  int x = nondet_int();
  int r;
  if(x > 0 && x < 10)
    r = 1;
  else
    r = 0;
  __CPROVER_assert(r == 0 || r == 1, "r");
}
