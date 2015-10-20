#define F (1 << 14) // fixed point 1
#define INT_MAX_ ((1 << 31) - 1)
#define INT_MIN_ (-(1 << 31))
// x and y denote fixed_point numbers in 17.14 format
// n is an integer

int int_to_fp (int n);          /* convert int to FP */
int fp_to_int_round (int x);    /* FP into int, round */
int fp_to_int (int x);          /* FP to int, throw points */
int add_fp (int x, int y);      /* add 2 FPs */
int add_mixed (int x ,int n);   /* add 1 FP, 1 int */
int sub_fp (int x, int y);      /* subtract 2 FPs */
int sub_mixed (int x, int n);   /* subtract int from FP */
int mult_fp (int x, int y);     /* multiply 2 FPs */
int mult_mixed (int x, int n);  /* multiply FP, int */
int div_fp (int x, int y);      /* divide 2 FPs */
int div_mixed (int x, int n);   /* divide int by FP */

/* implementation */

int int_to_fp (int n)
{
  return n * F;
}

int fp_to_int_round (int x)
{
  return x / F;
}

int fp_to_int (int x)
{
  if( x >= 0 )
    return (x + F) / 2;
  else
    return (x - F) / 2;
}

int add_fp (int x, int y)
{
  return x + y;
}

int add_mixed (int x, int n)
{
  return x + n*F;
}

int sub_fp (int x, int y)
{
  return x - y;
}

int sub_mixed (int x, int n)
{
  return x - n*F;
}

int mult_fp (int x, int y)
{
  return ((int64_t)x) * y / F;
}

int mult_mixed (int x, int n)
{
  return x * n;
}

int div_fp (int x, int y)
{
  return ((int64_t)x) * F / y;
}

int div_mixed (int x, int n)
{
  return x / n;
}
