/* -*- mode: C; c-basic-offset: 4; -*-
 *
 * BITLIB - A simple little library for handling packed bitfields on
 * architectures that may require aligned memory access.
 *
 * For multi-threaded applications, we want to use atomic operations
 * so that different threads can work on different bitfields within
 * the same machine word.  To this end, use the autoconf macro
 * CHECK_INTEL_ATOMIC_OPS which defines HAVE_INTEL_ATOMIC_OPS if we
 * have the necessary functions.  Otherwise, we'll just use standard C
 * operators, cross our fingers, and hope for the best.
 *
 * We don't do any overflow checking on the arguments, but will
 * silently mask off anything outside the bitfield.
 *
 * Usage:
 *
 * #include "bitlib.h"
 *
 * TYPE get_TYPE_field(void * ptr, bitoffset offset, unsigned TYPE mask)
 * void set_TYPE_field(void * ptr, bitoffset offset, unsigned TYPE mask, TYPE val)
 *
 * TYPE is currently either "unsigned_int" or "int".
 *
 * The offsets can be quite large; they are not limited to 32 or
 * anything like that.
 *
 * Bitlib is public domain.  You can do anything with it that you'd like.
 *
 * by Brent Baccala    July, 2012
 */

#ifndef _BITLIB_H
#define _BITLIB_H       1

#include <inttypes.h>		/* C99 integer types */

typedef unsigned int bitoffset;

/* XXX If we don't have __sync_fetch_and_and_8 and
 * __sync_fetch_and_or_8, we might have problems with 64 bit words.
 * Should emulate instead, but right now we don't.  We'd have to check
 * endianness to emulate correctly.
 */

#ifdef HAVE_INTEL_ATOMIC_OPS

#define __bitlib_sync_and __sync_fetch_and_and
#define __bitlib_sync_or __sync_fetch_and_or

#else

#define __bitlib_sync_and(ptr, val) (*(ptr) &= (val))
#define __bitlib_sync_or(ptr, val) (*(ptr) |= (val))

#endif

#define CREATE_UNSIGNED_FIELD_FUNCTIONS(TYPE, TYPE_WITH_UNDERSCORES)					\
													\
inline TYPE get_ ## TYPE_WITH_UNDERSCORES ## _field(void * ptr, bitoffset offset, TYPE mask)		\
{													\
    TYPE * iptr;											\
    TYPE val;												\
													\
    /* It is truly amazing how much of a difference commenting out this next idiot check makes in	\
     * gcc 4.0.4's ability to optimize this code, at least on i386.					\
     */													\
													\
    /* if (offset == -1) return 0; */									\
													\
    iptr = (TYPE *) ptr;										\
    iptr += offset/(8*sizeof(TYPE));									\
    offset %= 8*sizeof(TYPE);										\
													\
    /* little endian - bits counted from LSB								\
     *													\
     *          iptr+1           iptr									\
     *    [----------iiiiii][iiiiii--------]								\
     *                            | offset |								\
     */													\
													\
    val = (*iptr >> offset);										\
    /* 	| (*(iptr+1) << (8*sizeof(unsigned int) - offset)); */						\
													\
    if (offset != 0) val |= (*(iptr+1) << (8*sizeof(TYPE) - offset));					\
													\
    /* XXX maybe this would be faster if we made it unconditional */					\
    /*													\
    if ((mask >> (8*sizeof(TYPE) - offset)) > 0) {							\
	iptr ++;											\
	val |= *iptr << offset;										\
    }													\
    */													\
													\
    val &= mask;											\
													\
    return val;												\
}													\
													\
inline void set_ ## TYPE_WITH_UNDERSCORES ## _field(void *ptr, bitoffset offset, TYPE mask, TYPE val)	\
{													\
    TYPE * iptr;											\
													\
    iptr = (TYPE *) ptr;										\
    iptr += offset/(sizeof(TYPE) * 8);									\
    offset %= sizeof(TYPE) * 8;										\
													\
    /* These operations have to be atomic, since the computer uses 32 or 64 bit words, and those	\
     * words will contain other entries that other threads may be working on, even though this entry	\
     * will be locked by the current thread.								\
     */													\
													\
    __bitlib_sync_and(iptr, ~ (mask << offset));							\
    __bitlib_sync_or(iptr, (val & mask) << offset);							\
													\
    if ((offset != 0) && (mask >> (8*sizeof(TYPE) - offset)) > 0) {					\
	iptr ++;											\
	__bitlib_sync_and(iptr, ~ (mask >> (8*sizeof(TYPE) - offset)));					\
	__bitlib_sync_or(iptr, (val & mask) >> (8*sizeof(TYPE) - offset));				\
    }													\
}

#define CREATE_SIGNED_FIELD_FUNCTIONS(TYPE, TYPE_WITH_UNDERSCORES)					\
													\
inline TYPE get_ ## TYPE ## _field(void * ptr, bitoffset offset, unsigned TYPE mask)			\
{													\
    unsigned TYPE val = get_unsigned_ ## TYPE_WITH_UNDERSCORES ## _field(ptr, offset, mask);		\
													\
    /* sign extend */											\
    if (val > (mask >> 1)) val |= (~ (mask >> 1));							\
													\
    return (TYPE) val;											\
}													\
													\
inline void set_ ## TYPE_WITH_UNDERSCORES ## _field(void *ptr, bitoffset offset, unsigned TYPE mask, TYPE val)	\
{													\
    set_unsigned_ ## TYPE_WITH_UNDERSCORES ## _field(ptr, offset, mask, (unsigned TYPE) val);		\
}

#define CREATE_FIELD_FUNCTIONS(TYPE, TYPE_WITH_UNDERSCORES)						\
    CREATE_UNSIGNED_FIELD_FUNCTIONS(unsigned TYPE, unsigned_ ## TYPE_WITH_UNDERSCORES)			\
    CREATE_SIGNED_FIELD_FUNCTIONS(TYPE, TYPE_WITH_UNDERSCORES)

/* Now use the macros above to actually create the inline functions themselves */

CREATE_FIELD_FUNCTIONS(int, int)
CREATE_UNSIGNED_FIELD_FUNCTIONS(uint32_t, uint32_t)
CREATE_UNSIGNED_FIELD_FUNCTIONS(uint64_t, uint64_t)

/* Optimized versions for single bit fields */

inline unsigned int get_bit_field(void * ptr, bitoffset offset)
{
    unsigned int * iptr;
    unsigned int val;

    /* It is truly amazing how much of a difference commenting out this next idiot check makes in
     * gcc 4.0.4's ability to optimize this code, at least on i386.
     */

    /* if (offset == -1) return 0; */

    iptr = (unsigned int *) ptr;
    iptr += offset/(8*sizeof(unsigned int));
    offset %= 8*sizeof(unsigned int);

    /* little endian - bits counted from LSB
     *
     *          iptr+1           iptr
     *    [----------iiiiii][iiiiii--------]
     *                            | offset |
     */

    val = (*iptr >> offset);

    return (val & 1);
}

inline void set_bit_field(void *ptr, bitoffset offset, unsigned int val)
{
    unsigned int * iptr;

    iptr = (unsigned int *) ptr;
    iptr += offset/(sizeof(unsigned int) * 8);
    offset %= sizeof(unsigned int) * 8;

    /* These operations have to be atomic, since the computer uses 32 or 64 bit words, and those
     * words will contain other entries that other threads may be working on, even though this entry
     * will be locked by the current thread.
     */

    __bitlib_sync_and(iptr, ~ (1 << offset));
    __bitlib_sync_or(iptr, (val & 1) << offset);
}

/* spinlock on a bit and return 1 if we had to spin, 0 otherwise
 *
 * Be careful - this function can spin forever if you're not careful!
 */

inline unsigned int spinlock_bit_field(void *ptr, bitoffset offset)
{
    unsigned int * iptr;

    iptr = (unsigned int *) ptr;
    iptr += offset/(sizeof(unsigned int) * 8);
    offset %= sizeof(unsigned int) * 8;

    if (__bitlib_sync_or(iptr, 1 << offset) & (1 << offset)) {
	while (__bitlib_sync_or(iptr, 1 << offset) & (1 << offset));
	return 1;
    } else {
	return 0;
    }
}

#endif