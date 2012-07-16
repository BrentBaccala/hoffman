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

typedef unsigned int bitoffset;

#ifdef HAVE_INTEL_ATOMIC_OPS

#define __bitlib_sync_and __sync_fetch_and_and
#define __bitlib_sync_or __sync_fetch_and_or

#else

#define __bitlib_sync_and(ptr, val) (*(ptr) &= (val))
#define __bitlib_sync_or(ptr, val) (*(ptr) |= (val))

#endif

inline unsigned int get_unsigned_int_field(void * ptr, bitoffset offset, unsigned int mask)
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
    /* 	| (*(iptr+1) << (8*sizeof(unsigned int) - offset)); */

    if (offset != 0) val |= (*(iptr+1) << (8*sizeof(unsigned int) - offset));

#if 0
    /* XXX maybe this would be faster if we made it unconditional */
    if ((mask >> (8*sizeof(unsigned int) - offset)) > 0) {
	iptr ++;
	val |= *iptr << offset;
    }
#endif

    val &= mask;

    return val;
}

inline void set_unsigned_int_field(void *ptr, bitoffset offset, unsigned int mask, unsigned int val)
{
    unsigned int * iptr;

    iptr = (unsigned int *) ptr;
    iptr += offset/(sizeof(unsigned int) * 8);
    offset %= sizeof(unsigned int) * 8;

    /* These operations have to be atomic, since the computer uses 32 or 64 bit words, and those
     * words will contain other entries that other threads may be working on, even though this entry
     * will be locked by the current thread.
     */

    __bitlib_sync_and(iptr, ~ (mask << offset));
    __bitlib_sync_or(iptr, (val & mask) << offset);

    if ((offset != 0) && (mask >> (8*sizeof(int) - offset)) > 0) {
	iptr ++;
	__bitlib_sync_and(iptr, ~ (mask >> (8*sizeof(unsigned int) - offset)));
	__bitlib_sync_or(iptr, (val & mask) >> (8*sizeof(unsigned int) - offset));
    }
}

inline int get_int_field(void * ptr, bitoffset offset, unsigned int mask)
{
    unsigned int val = get_unsigned_int_field(ptr, offset, mask);

    /* sign extend */
    if (val > (mask >> 1)) val |= (~ (mask >> 1));

    return (int) val;
}

inline void set_int_field(void *ptr, bitoffset offset, unsigned int mask, int val)
{
    set_unsigned_int_field(ptr, offset, mask, (unsigned int) val);
}

#endif
