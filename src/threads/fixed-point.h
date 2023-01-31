#ifndef __THREAD_FIXED_POINT_H
#define __THREAD_FIXED_POINT_H

#define F 16384

#define to_real(n) (n*F)
#define to_int(n) (n/F)

/* Multiply real x by real y */
extern inline int32_t
multiply_real (int32_t x, int32_t y)
{
  return (int32_t) (((int64_t)x) * y / F);
}

/* Divide real x by real y */
extern inline int32_t
add_real (int32_t x, int32_t y)
{
  return x+y;
}

/* Add real x by real y */
extern inline int32_t
divide_real (int32_t x, int32_t y)
{
  return (int32_t) (((int64_t)x) * F / y);
}

/* Substract real x by real y */
extern inline int32_t
sub_real (int32_t x, int32_t y)
{
  return x-y;
}

extern inline int
round_r_to_int (int32_t x)
{
  if (x >= 0)
    return ((x + F/2) /F);
  else
    return ((x - F/2) /F);
}
#endif /* thread/fixed_point.h */