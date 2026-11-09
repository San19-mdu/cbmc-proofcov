int days(int month, int leap)
{
  switch(month)
  {
  case 2:
    return leap ? 29 : 28;
  case 4:
  case 6:
    return 30;
  default:
    return 31;
  }
}

void main()
{
  __CPROVER_assert(days(2, 1) == 29, "d");
}
