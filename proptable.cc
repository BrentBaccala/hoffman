/* -*- mode: C; fill-column: 100; c-basic-offset: 4; -*-
 */

#include <tpie/priority_queue.h> // for tpie::priority_queue
#include <tpie/tpie_assert.h> // for tp_assert macro
#include <tpie/tpie.h> // for tpie_init
#include <tpie/tpie_log.h>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>		/* for write(), lseek(), gethostname() */
#include <fcntl.h>		/* for O_RDONLY */
#include <errno.h>		/* for errno and strerror() */

#include <sys/time.h>

#include <inttypes.h>		/* C99 integer types */

#include "bitlib.h"

extern "C" {

#include "hoffman.h"

}

/* When propagating a change from one position to another, we go through this table to do it.  By
 * maintaining this table sorted, we avoid the random accesses that would be required to propagate
 * directly from one position to another.  It only makes sense to use a propagation table if the
 * tablebase can't fit in memory.  If the tablebase does fit in memory, we bypass almost this entire
 * section of code.
 *
 * We insert into the propagation table using an "address calculation insertion sort".  Knuth
 * described it by analogy to shelving books.  You're sorting the books as you place them onto the
 * shelf; that makes it an "insertion sort" (as opposed to something like an exchange sort, where
 * you place them first and then sort by swapping).  You look at the author's last name to try and
 * "guess" where it should go on the shelf - "Alfors" all the way to the left; "Munkres" about in
 * the middle; "van der Waerden" towards the right.  That's the "address calculation" part.
 *
 * We do this with indices, dividing them by a scaling factor to get an offset into the propagation
 * table (in memory).  This type of sort works well if the indices are well spread out, and not so
 * well in they are clumped together.  That's why we invert indices in a finite field - to spread
 * out the mating positions that naturally clump together into groups of similar positions.  Once we
 * start having to move things around too much to do an insertion, we write the current proptable
 * out to disk, zero out the memory, and start again fresh.
 *
 * Once we've got a bunch of proptables written to disk, we then need to read them back in.  We do
 * this "semi-sequentially" - each individual table is read sequentially, even though we need to
 * jump our reads around between them.  We run the entries from each table through a sort tree to
 * produce a single stream of sorted proptable entries, which are then committed into the tablebase.
 *
 * To optimize all of this, we simultaneously read one set of proptables and write another set while
 * making a single pass through the tablebase.
 */

class proptable_entry {

 public:
    index_t index;
    int dtm;
    unsigned int movecnt;
    boolean PTM_wins_flag;
    int futuremove;

    bool operator<(const proptable_entry &other) const {
	return index < other.index;
    }
};

typedef tpie::priority_queue<class proptable_entry> proptable;

proptable * input_proptable;
proptable * output_proptable;

void commit_proptable_entry(class proptable_entry propentry)
{
    commit_entry(propentry.index, propentry.dtm, propentry.PTM_wins_flag, propentry.movecnt, FUTUREVECTOR(propentry.futuremove));
}


/* proptable_pass()
 *
 * Commit an old set of proptables into the entries array while writing a new set.
 */

void proptable_pass(int target_dtm)
{
    index_t index;

    input_proptable = output_proptable;
    output_proptable = new proptable;

    /* fprintf(stderr, "proptable_pass(%d); size of proptable: %llu\n", target_dtm, input_proptable->size()); */

    for (index = 0; index <= max_index(current_tb); index ++) {

	futurevector_t futurevector = 0;
	futurevector_t possible_futuremoves = 0;

	if (target_dtm == 0) {
	    possible_futuremoves = initialize_tablebase_entry(current_tb, index);
	}

	if (! input_proptable->empty()) {

	    if (input_proptable->top().index < index) {
		fatal("Out-of-order entries in proptable\n");
	    }

	    while (! input_proptable->empty() && input_proptable->top().index == index) {

		class proptable_entry pt_entry = input_proptable->top();

		input_proptable->pop();

#ifdef DEBUG_MOVE
		if (index == DEBUG_MOVE)
		    fprintf(stderr, "Commiting sorting element 1: %0" PRIx64 " %0" PRIx64 " <-%d\n",
			    *((uint64_t *) SORTING_NETWORK_ELEM(1)), *(((uint64_t *) SORTING_NETWORK_ELEM(1)) + 1), proptable_num[1]);
#endif

	    /* These futuremoves might be moves into check, in which case they were discarded back
	     * during initialization.  So we only commit if they are possible.  Now some of the
	     * possibles might have been merged with impossibles, and if that's the case we hope
	     * that they all had multiplicity 1, so we can filter out the impossibles and still know
	     * what movecnt to use (by counting the possibles).
	     *
	     * XXX None of this really makes sense if the futurebase is dtm, since we should have
	     * known during backprop which of the positions were in-check and never processed them.
	     * So maybe we should change propagate_index_from_futurebase to drop positions with
	     * dtm=-1.  (what about dtm=1?)  Bitbases and unimplemented possibilities like Nalimov
	     * or dtr tablebases are more problematic.
	     */

		if (target_dtm != 0) {

		    commit_proptable_entry(pt_entry);

		} else if (FUTUREVECTOR(pt_entry.futuremove) & possible_futuremoves) {

		    commit_proptable_entry(pt_entry);

		    if (FUTUREVECTOR(pt_entry.futuremove) & futurevector) {
			global_position_t global;
			index_to_global_position(current_tb, pt_entry.index, &global);
			fatal("Futuremoves multiply handled: %s\n", global_position_to_FEN(&global));
		    }

		    futurevector |= FUTUREVECTOR(pt_entry.futuremove);

		}

	    }
	}

	/* Don't track futuremoves for illegal (DTM 1) positions */

	if ((target_dtm == 0) && (get_entry_DTM(index) != 1)) {

	    if ((futurevector & possible_futuremoves) != futurevector) {
		/* Commented out because if we're not using DTM this code will run for illegal positions */
#if 0
		global_position_t global;
		index_to_global_position(current_tb, index, &global);
		fprintf(stderr, "Futuremove discrepancy: %d %s\n", index, global_position_to_FEN(&global));
#endif
	    } else {
		finalize_futuremove(current_tb, index, possible_futuremoves ^ futurevector);
	    }
	}

	if (target_dtm != 0) back_propagate_index(index, target_dtm);

    }

    delete input_proptable;
}

void insert_new_propentry(index_t index, int dtm, unsigned int movecnt, boolean PTM_wins_flag, int futuremove)
{
    class proptable_entry pt_entry;

    pt_entry.index = index;
    pt_entry.dtm = dtm;
    pt_entry.movecnt = movecnt;
    pt_entry.PTM_wins_flag = PTM_wins_flag;
    pt_entry.futuremove = futuremove;

    output_proptable->push(pt_entry);

    /* fprintf(stderr, "Size of proptable: %llu\n", output_proptable->size()); */
}


int initialize_proptable(int proptable_MBs)
{
    tpie::tpie_init();
    tpie::get_memory_manager().set_limit(proptable_MBs * 1024 * 1024);

    tpie::get_log().set_level(tpie::LOG_DEBUG);

    output_proptable = new proptable;

    return 1;
}
