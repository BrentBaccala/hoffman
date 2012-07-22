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
#include <limits.h>

#include "bitlib.h"

#if 0

#define UNSIGNED_TYPE		uint64_t
#define UNSIGNED_MAX		UINT64_MAX
#define PRIu			PRIx64
#define SET_UNSIGNED_FIELD	set_uint64_t_field
#define GET_UNSIGNED_FIELD	get_uint64_t_field

#else

#define UNSIGNED_TYPE		unsigned int
#define UNSIGNED_MAX		UINT_MAX
#define PRIu			"u"
#define SET_UNSIGNED_FIELD	set_unsigned_int_field
#define GET_UNSIGNED_FIELD	get_unsigned_int_field

#endif

#if 1
#define SIGNED_TYPE		int
#define SET_SIGNED_FIELD	set_int_field
#define GET_SIGNED_FIELD	get_int_field
#endif

int main(int argc, char * argv[])
{
  void * test_area = malloc(300);
  int i, j;

  /* First, for a range of bitfield sizes, check that we can set and
   * retrieve random unsigned data.
   */

  fprintf(stderr, "Testing unsigned type \n");

  for (j=1; j<=8*sizeof(UNSIGNED_TYPE); j++) {
      UNSIGNED_TYPE mask = j==8*sizeof(UNSIGNED_TYPE) ? UNSIGNED_MAX : ((1<<j) - 1);
      for (i=0; i<256*8; i++) {
	  UNSIGNED_TYPE testval = rand() & mask;
	  SET_UNSIGNED_FIELD(test_area, i, j, testval);
	  if (GET_UNSIGNED_FIELD(test_area, i, j) != testval) {
	      fprintf(stderr, "SET_UNSIGNED_FIELD(test_area, %d, %d, %" PRIu ") returned %" PRIu "\n",
		      i, j, testval, GET_UNSIGNED_FIELD(test_area, i, j));
	  }
      }
  }

  fprintf(stderr, "Testing that we don't set any extraneous bits\n");
  bzero(test_area, 300);

  for (j=1; j<=32; j++) {
      UNSIGNED_TYPE mask = j==8*sizeof(UNSIGNED_TYPE) ? UNSIGNED_MAX : ((1<<j) - 1);
      for (i=0; i<256*8; i++) {
	  UNSIGNED_TYPE testval = rand() & mask;
	  SET_UNSIGNED_FIELD(test_area, i, j, testval);
	  if (GET_UNSIGNED_FIELD(test_area, i, j) != testval) {
	      fprintf(stderr, "SET_UNSIGNED_FIELD(test_area, %d, %d, %" PRIu "), returned %" PRIu "\n",
		      i, j, testval, GET_UNSIGNED_FIELD(test_area, i, j));
	  }
	  if (GET_UNSIGNED_FIELD(test_area, i+j, j) != 0) {
	      fprintf(stderr, "after SET_UNSIGNED_FIELD(test_area, %d, %d, %" PRIu "), "
		      "GET_UNSIGNED_FIELD(test_area, %d, %d) returned %" PRIu "\n",
		      i, j, testval, i+j, j, GET_UNSIGNED_FIELD(test_area, i+j, j));
	  }
	  if (i > j) {
	      if (GET_UNSIGNED_FIELD(test_area, i-j, j) != 0) {
		  fprintf(stderr, "after SET_UNSIGNED_FIELD(test_area, %d, %d, %" PRIu "), "
			  "GET_UNSIGNED_FIELD(test_area, %d, %d) returned %" PRIu "\n",
			  i, j, testval, i-j, j, GET_UNSIGNED_FIELD(test_area, i-j,j));
	      }
	  }
	  SET_UNSIGNED_FIELD(test_area, i, j, 0);
      }
  }

  fprintf(stderr, "Testing that we don't clear any extraneous bits\n");
  memset(test_area, 0xff, 300);

  for (j=1; j<=32; j++) {
      UNSIGNED_TYPE mask = j==8*sizeof(UNSIGNED_TYPE) ? UNSIGNED_MAX : ((1<<j) - 1);
      for (i=0; i<256*8; i++) {
	  UNSIGNED_TYPE testval = rand() & mask;
	  SET_UNSIGNED_FIELD(test_area, i, j, testval);
	  if (GET_UNSIGNED_FIELD(test_area, i, j) != testval) {
	      fprintf(stderr, "SET_UNSIGNED_FIELD(test_area, %d, %d, %" PRIu "), returned %" PRIu "\n",
		      i, j, testval, GET_UNSIGNED_FIELD(test_area, i, j));
	  }
	  if (GET_UNSIGNED_FIELD(test_area, i+j, j) != mask) {
	      fprintf(stderr, "after SET_UNSIGNED_FIELD(test_area, %d, %d, %" PRIu "), "
		      "GET_UNSIGNED_FIELD(test_area, %d, %d) returned %" PRIu "\n",
		      i, j, testval, i+j, j, GET_UNSIGNED_FIELD(test_area, i+j, j));
	  }
	  if (i > j) {
	      if (GET_UNSIGNED_FIELD(test_area, i-j, j) != mask) {
		  fprintf(stderr, "after SET_UNSIGNED_FIELD(test_area, %d, %d, %" PRIu "), "
			  "GET_UNSIGNED_FIELD(test_area, %d, %d) returned %" PRIu "\n",
			  i, j, testval, i-j, j, GET_UNSIGNED_FIELD(test_area, i-j, j));
	      }
	  }
	  SET_UNSIGNED_FIELD(test_area, i, j, mask);
      }
  }

  /* Check that we can set and retrieve random signed data. */

#ifdef SIGNED_TYPE
  fprintf(stderr, "Testing signed type \n");

  for (j=1; j<=32; j++) {
      UNSIGNED_TYPE mask = j==8*sizeof(UNSIGNED_TYPE) ? UNSIGNED_MAX : ((1<<j) - 1);
      for (i=0; i<256*8; i++) {
	  SIGNED_TYPE testval = (rand() & mask) - (mask >> 1) - 1;
	  SET_SIGNED_FIELD(test_area, i, j, testval);
	  if (GET_SIGNED_FIELD(test_area, i, j) != testval) {
	      fprintf(stderr, "Error!: SET_SIGNED_FIELD(test_area, %d, %d, %d) returned %d (%d)\n",
		      i, j, testval, GET_SIGNED_FIELD(test_area, i, j), GET_UNSIGNED_FIELD(test_area, i, j));
	  }
      }
  }
#endif

  return 0;
}
