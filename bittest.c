/* -*- mode: C; c-basic-offset: 4; -*-
 *
 * BITLIB - A simple little library for handling packed bitfields on
 * architectures that may require aligned memory access.
 *
 * This is a simple test program to check correct bitlib operation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bitlib.h"

int main(int argc, char * argv[])
{
  void * test_area = malloc(300);
  int i, j;

  /* First, for a range of bitfield sizes, check that we can set and
   * retrieve random unsigned data.
   */

  for (j=1; j<=32; j++) {
    unsigned int mask = j==32 ? 0xffffffff : ((1<<j) - 1);
    for (i=0; i<256*8; i++) {
      unsigned int testval = rand() & mask;
      set_unsigned_int_field(test_area, i, mask, testval);
      if (get_unsigned_int_field(test_area, i, mask) != testval) {
	fprintf(stderr, "set_unsigned_field(test_area, %d, %d, %d), returned %d\n",
		i, mask, testval, get_unsigned_int_field(test_area, i+j, mask));
      }
    }
  }

  /* Check that we don't set any extraneous bits. */

  bzero(test_area, 300);

  for (j=1; j<=32; j++) {
    unsigned int mask = j==32 ? 0xffffffff : ((1<<j) - 1);
    for (i=0; i<256*8; i++) {
      unsigned int testval = rand() & mask;
      set_unsigned_int_field(test_area, i, mask, testval);
      if (get_unsigned_int_field(test_area, i, mask) != testval) {
	fprintf(stderr, "set_unsigned_field(test_area, %d, %d, %d), returned %d\n",
		i, mask, testval, get_unsigned_int_field(test_area, i+j, mask));
      }
      if (get_unsigned_int_field(test_area, i+j, mask) != 0) {
	fprintf(stderr, "after set_unsigned_field(test_area, %d, %d, %d), get_unsigned_int_field(test_area, %d, %d) returned %d\n",
		i, mask, testval, i+j, mask, get_unsigned_int_field(test_area, i+j, mask));
      }
      if (i > j) {
	if (get_unsigned_int_field(test_area, i-j, mask) != 0) {
	  fprintf(stderr, "after set_unsigned_field(test_area, %d, %d, %d), get_unsigned_int_field(test_area, %d, %d) returned %d\n",
		  i, mask, testval, i-j, mask, get_unsigned_int_field(test_area, i-j, mask));
	}
      }
      set_unsigned_int_field(test_area, i, mask, 0);
    }
  }

  /* Check that we don't clear any extraneous bits. */

  memset(test_area, 0xff, 300);

  for (j=1; j<=32; j++) {
    unsigned int mask = j==32 ? 0xffffffff : ((1<<j) - 1);
    for (i=0; i<256*8; i++) {
      unsigned int testval = rand() & mask;
      set_unsigned_int_field(test_area, i, mask, testval);
      if (get_unsigned_int_field(test_area, i, mask) != testval) {
	fprintf(stderr, "set_unsigned_field(test_area, %d, %d, %d), returned %d\n",
		i, mask, testval, get_unsigned_int_field(test_area, i+j, mask));
      }
      if (get_unsigned_int_field(test_area, i+j, mask) != mask) {
	fprintf(stderr, "after set_unsigned_field(test_area, %d, %d, %d), get_unsigned_int_field(test_area, %d, %d) returned %d\n",
		i, mask, testval, i+j, mask, get_unsigned_int_field(test_area, i+j, mask));
      }
      if (i > j) {
	if (get_unsigned_int_field(test_area, i-j, mask) != mask) {
	  fprintf(stderr, "after set_unsigned_field(test_area, %d, %d, %d), get_unsigned_int_field(test_area, %d, %d) returned %d\n",
		  i, mask, testval, i-j, mask, get_unsigned_int_field(test_area, i-j, mask));
	}
      }
      set_unsigned_int_field(test_area, i, mask, mask);
    }
  }

  /* Check that we can set and retrieve random signed data. */

  for (j=1; j<=32; j++) {
    unsigned int mask = j==32 ? 0xffffffff : ((1<<j) - 1);
    for (i=0; i<256*8; i++) {
      int testval = (rand() & mask) - (mask >> 1) - 1;
      set_int_field(test_area, i, mask, testval);
      if (get_int_field(test_area, i, mask) != testval) {
	fprintf(stderr, "Error!: set_int_field(test_area, %d, %d, %d) returned %d (%d)\n",
		i, mask, testval, get_int_field(test_area, i, mask), get_unsigned_int_field(test_area, i, mask));
      }
    }
  }

  bzero(test_area, 300);

  for (j=1; j<=32; j++) {
    unsigned int mask = j==32 ? 0xffffffff : ((1<<j) - 1);
    for (i=0; i<256*8; i++) {
      int testval = (rand() & mask) - (mask >> 1) - 1;
      set_int_field(test_area, i, mask, testval);
      if (get_int_field(test_area, i, mask) != testval) {
	fprintf(stderr, "Error!: set_int_field(test_area, %d, %d, %d) returned %d (%d)\n",
		i, mask, testval, get_int_field(test_area, i, mask), get_unsigned_int_field(test_area, i, mask));
      }
      if (get_unsigned_int_field(test_area, i+j, mask) != 0) {
	fprintf(stderr, "after set_int_field(test_area, %d, %d, %d), get_unsigned_int_field(test_area, %d, %d) returned %d\n",
		i, mask, testval, i+j, mask, get_unsigned_int_field(test_area, i+j, mask));
      }
      set_unsigned_int_field(test_area, i, mask, 0);
    }
  }

  return 0;
}
