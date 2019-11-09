#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

void bx_panic()
{
  fprintf(stderr, "RUNTIME PANIC!\n");
  exit(-1);
}

void bx_print_int(int64_t x)
{
  printf("%ld\n", x);
}

void bx_print_bool(int64_t x)
{
  printf("%s\n", x == 0 ? "false" : "true");
}
