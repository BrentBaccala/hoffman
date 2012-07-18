/* -*- mode: C; fill-column: 100; c-basic-offset: 4; -*-
 */

#include <tpie/priority_queue.h> // for tpie::priority_queue
#include <tpie/tpie_assert.h> // for tp_assert macro
#include <tpie/tpie.h> // for tpie_init

extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>		/* for write(), lseek(), gethostname() */
#include <fcntl.h>		/* for O_RDONLY */
#include <errno.h>		/* for errno and strerror() */

#include <sys/time.h>

#include <inttypes.h>		/* C99 integer types */

#include "bitlib.h"
#include "hoffman.h"

typedef void proptable_entry_t;

struct format {
    uint8_t bits;
    uint8_t bytes;
    int locking_bit_offset;
    uint32_t dtm_mask;
    int dtm_offset;
    uint8_t dtm_bits;
    uint32_t movecnt_mask;
    int movecnt_offset;
    uint8_t movecnt_bits;
    uint32_t index_mask;
    int index_offset;
    uint8_t index_bits;
    uint64_t futurevector_mask;
    int futurevector_offset;
    uint8_t futurevector_bits;
    int flag_offset;
    int flag_type;
    int PTM_wins_flag_offset;
    int basic_offset;
    int capture_possible_flag_offset;
};

extern struct format proptable_format;

#define PROPTABLE_FORMAT_BYTES (proptable_format.bytes)
#define PROPTABLE_FORMAT_INDEX_MASK (proptable_format.index_mask)
#define PROPTABLE_FORMAT_INDEX_OFFSET (proptable_format.index_offset)
#define PROPTABLE_FORMAT_FUTUREVECTOR_BITS (proptable_format.futurevector_bits)
#define PROPTABLE_FORMAT_FUTUREVECTOR_MASK (proptable_format.futurevector_mask)
#define PROPTABLE_FORMAT_FUTUREVECTOR_OFFSET (proptable_format.futurevector_offset)
#define PROPTABLE_FORMAT_DTM_BITS (proptable_format.dtm_bits)
#define PROPTABLE_FORMAT_DTM_MASK (proptable_format.dtm_mask)
#define PROPTABLE_FORMAT_DTM_OFFSET (proptable_format.dtm_offset)
#define PROPTABLE_FORMAT_MOVECNT_MASK (proptable_format.movecnt_mask)
#define PROPTABLE_FORMAT_MOVECNT_OFFSET (proptable_format.movecnt_offset)
#define PROPTABLE_FORMAT_PTM_WINS_FLAG_OFFSET (proptable_format.PTM_wins_flag_offset)


void add_timeval(struct timeval *dest, struct timeval *src);
void subtract_timeval(struct timeval *dest, struct timeval *src);

int do_write_or_suspend(int fd, void *ptr, int length);

boolean index_to_global_position(tablebase_t *tb, index_t index, global_position_t *global);

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

#define MAX_ZEROOFFSET 25		/* Maximum wiggle room in our library insertion */

int proptable_entries = 0;
int proptable_merges = 0;

void * proptable;
int num_propentries = 0;

#define PROPTABLE_ELEM(n)  ((char *)proptable + PROPTABLE_FORMAT_BYTES * (n))

index_t get_propentry_index(proptable_entry_t *propentry)
{
    return get_unsigned_int_field(propentry, PROPTABLE_FORMAT_INDEX_OFFSET, PROPTABLE_FORMAT_INDEX_MASK);
}

void set_propentry_index(proptable_entry_t *propentry, index_t index)
{
    set_unsigned_int_field(propentry, PROPTABLE_FORMAT_INDEX_OFFSET, PROPTABLE_FORMAT_INDEX_MASK, index);
}

futurevector_t get_propentry_futurevector(proptable_entry_t *propentry)
{
    return get_futurevector_t_field(propentry, PROPTABLE_FORMAT_FUTUREVECTOR_OFFSET, PROPTABLE_FORMAT_FUTUREVECTOR_MASK);
}

void set_propentry_futurevector(proptable_entry_t *propentry, futurevector_t futurevector)
{
    set_futurevector_t_field(propentry, PROPTABLE_FORMAT_FUTUREVECTOR_OFFSET, PROPTABLE_FORMAT_FUTUREVECTOR_MASK, futurevector);
}

int get_propentry_dtm(proptable_entry_t *propentry)
{
    if (PROPTABLE_FORMAT_DTM_BITS > 0) {
	return get_int_field(propentry, PROPTABLE_FORMAT_DTM_OFFSET, PROPTABLE_FORMAT_DTM_MASK);
    } else {
	return 0;
    }
}

void set_propentry_dtm(proptable_entry_t *propentry, int distance)
{
    if (PROPTABLE_FORMAT_DTM_BITS > 0) {
	set_int_field(propentry, PROPTABLE_FORMAT_DTM_OFFSET, PROPTABLE_FORMAT_DTM_MASK, distance);
    }
}

int get_propentry_movecnt(proptable_entry_t *propentry)
{
    return get_unsigned_int_field(propentry, PROPTABLE_FORMAT_MOVECNT_OFFSET, PROPTABLE_FORMAT_MOVECNT_MASK);
}

void set_propentry_movecnt(proptable_entry_t *propentry, int movecnt)
{
    set_unsigned_int_field(propentry, PROPTABLE_FORMAT_MOVECNT_OFFSET, PROPTABLE_FORMAT_MOVECNT_MASK, movecnt);
}

int get_propentry_PTM_wins_flag(proptable_entry_t *propentry)
{
    /* This one is a little different.  A proptable can have either a PTM wins flag or a DTM field.
     * If the caller requests the PTM wins flag and it doesn't exist in this format, then use the
     * DTM field to "emulate" it.
     */

    if (PROPTABLE_FORMAT_PTM_WINS_FLAG_OFFSET != -1) {
	return get_bit_field(propentry, PROPTABLE_FORMAT_PTM_WINS_FLAG_OFFSET);
    } else {
	return (get_propentry_dtm(propentry) > 0) ? 1 : 0;
    }
}

void set_propentry_PTM_wins_flag(proptable_entry_t *propentry, int PTM_wins_flag)
{
    if (PROPTABLE_FORMAT_PTM_WINS_FLAG_OFFSET != -1) {
	set_bit_field(propentry, PROPTABLE_FORMAT_PTM_WINS_FLAG_OFFSET, PTM_wins_flag);
    }
}

void insert_at_propentry(int propentry, proptable_entry_t * pentry)
{
#ifdef DEBUG_MOVE
    if (get_propentry_index(pentry) == DEBUG_MOVE)
	fprintf(stderr, "insert_at_propentry; index=%d; propentry=%d\n",
		get_propentry_index(pentry), propentry);
#endif

    memcpy(PROPTABLE_ELEM(propentry), pentry, PROPTABLE_FORMAT_BYTES);

    proptable_entries ++;

#ifdef DEBUG_MOVE
    if (get_propentry_index(pentry) == DEBUG_MOVE)
	fprintf(stderr, "Propentry: 0%" PRIx64 " 0%" PRIx64 "\n",
		*((uint64_t *) pentry), *(((uint64_t *) pentry) + 1));
#endif
}

void merge_at_propentry(int propentry, proptable_entry_t *src)
{
    proptable_entry_t * dest = PROPTABLE_ELEM(propentry);
    int dest_dtm = get_propentry_dtm(dest);
    int src_dtm = get_propentry_dtm(src);

    proptable_merges ++;

#ifdef DEBUG_MOVE
    if (get_propentry_index(PROPTABLE_ELEM(propentry)) == DEBUG_MOVE)
	fprintf(stderr, "merge_at_propentry; index=%d; propentry=%d; src_dtm=%d\n",
		get_propentry_index(PROPTABLE_ELEM(propentry)), propentry, src_dtm);
#endif

    if (src_dtm > 0) {
	/* DTM > 0 - this move lets PTM mate from this position.  Update the proptable entry if
	 * either we don't have any PTM mates yet (table's dtm <= 0), or if this new mate is faster
	 * than the old one.
	 */
	if ((dest_dtm <= 0) || (src_dtm < dest_dtm)) {
	    set_propentry_dtm(dest, src_dtm);
	}
    } else if (src_dtm < 0) {
	/* DTM < 0 - this move lets PNTM mate from this position.  Update the proptable entry only
	 * if we don't have any PTM mates or draws (table's dtm < 0) and this PNTM mate is slower
	 * than the old one.
	 */
	if ((dest_dtm < 0) && (src_dtm < dest_dtm)) {
	    set_propentry_dtm(dest, src_dtm);
	}
    } else {
	/* DTM == 0 - this kind of move, which only shows up during futurebase backprop, since
	 * intratable is only done when positions are finalized as mates, lets PTM draw.  Update if
	 * everything we have so far is PNTM mates.
	 */
	if (dest_dtm < 0) {
	    set_propentry_dtm(dest, 0);
	}
    }

    /* PTM wins flag is logical OR of the two flags being combined. */
    if (get_propentry_PTM_wins_flag(src) && (! get_propentry_PTM_wins_flag(dest))) {
	set_propentry_PTM_wins_flag(dest, 1);
    }

    /* XXX need to check for overflow here, and that seems to require the possibility for a merge to
     * fail, meaning that this routine should return a failure indication and the code that calls it
     * needs to be changed to handle that.
     */
    set_propentry_movecnt(dest, get_propentry_movecnt(dest) + get_propentry_movecnt(src));

#if 0
    /* This might happen just because of symmetry issues */
    if (get_propentry_futurevector(dest) & get_propentry_futurevector(src)) {
	global_position_t global;
	index_to_global_position(current_tb, get_propentry_index(PROPTABLE_ELEM(propentry)), &global);
	fprintf(stderr, "Futuremoves multiply handled: %s\n", global_position_to_FEN(&global));
    }
#endif

    set_propentry_futurevector(dest, get_propentry_futurevector(dest) | get_propentry_futurevector(src));
}

void commit_proptable_entry(proptable_entry_t *propentry)
{
    index_t index = get_propentry_index(propentry);
    int dtm = get_propentry_dtm(propentry);
    uint8_t PTM_wins_flag = get_propentry_PTM_wins_flag(propentry);
    int movecnt = get_propentry_movecnt(propentry);
    futurevector_t futurevector = get_propentry_futurevector(propentry);

    commit_entry(index, dtm, PTM_wins_flag, movecnt, futurevector);
}


int num_proptables = 0;

/* proptable_full() - dump out to disk and empty table */

void proptable_full(void)
{
    char outfilename[256];
    struct timeval tv1, tv2;
    int proptable_output_fd = -1;
    int propentry;
    char *output_buffer;
    int output_buffer_size = 4096;
    int output_buffer_next;

    if (proptable_entries == 0) return;

    sprintf(outfilename, "propfile%04d_out", num_proptables);

    proptable_output_fd = open(outfilename, O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, 0666);

    if (proptable_output_fd == -1) {
	fatal("Can't open '%s' for writing propfile\n", outfilename);
	return;
    }

    /* XXX redo this by recording this data into an array and generating the XML when we're done */
#if 0
    xmlNodeAddContent(current_tb->current_pass_stats, BAD_CAST "\n      ");
    node = xmlNewChild(current_tb->current_pass_stats, NULL, BAD_CAST "proptable", NULL);
    snprintf(strbuf, sizeof(strbuf), "%d", proptable_entries);
    xmlNewProp(node, BAD_CAST "entries", BAD_CAST strbuf);
    snprintf(strbuf, sizeof(strbuf), "%d", proptable_merges);
    xmlNewProp(node, BAD_CAST "merges", BAD_CAST strbuf);
    snprintf(strbuf, sizeof(strbuf), "%d%%", (100*proptable_entries)/num_propentries);
    xmlNewProp(node, BAD_CAST "occupancy", BAD_CAST strbuf);
#endif

    /* Write the proptable out to disk, and zero it out in memory as we go.
     *
     * Due to the sparsity of the proptable (rarely more than 50% occupancy), we only write the
     * non-zero propentries.  Makes this code a little more complex because I do the buffering
     * myself, but tends to produce a big speed gain.
     *
     * Hitting a disk full condition is by no means out of the question here, so we use
     * do_write_or_suspend().
     */

    fprintf(stderr, "Writing proptable %d with %d entries (%d%% occupancy)\n",
	    num_proptables, proptable_entries, (100*proptable_entries)/num_propentries);

    gettimeofday(&tv1, NULL);

    output_buffer = (char *) malloc(output_buffer_size);
    if (output_buffer == NULL) {
	fatal("Can't malloc proptable output buffer\n");
	return;
    }

    output_buffer_next = 0;

    for (propentry = 0; propentry < num_propentries; propentry ++) {
	if (get_propentry_index(PROPTABLE_ELEM(propentry)) != PROPTABLE_FORMAT_INDEX_MASK) {
	    memcpy(output_buffer + output_buffer_next, PROPTABLE_ELEM(propentry), PROPTABLE_FORMAT_BYTES);
	    output_buffer_next += PROPTABLE_FORMAT_BYTES;
	    if (output_buffer_next == output_buffer_size) {
		do_write_or_suspend(proptable_output_fd, output_buffer, output_buffer_size);
		output_buffer_next = 0;
	    }
	    memset(PROPTABLE_ELEM(propentry), 0xff, PROPTABLE_FORMAT_BYTES);
	}
    }

    if (output_buffer_next != 0) {
	do_write_or_suspend(proptable_output_fd, output_buffer, output_buffer_next);
    }

    close(proptable_output_fd);
    free(output_buffer);

    gettimeofday(&tv2, NULL);
    subtract_timeval(&tv2, &tv1);
    add_timeval(&proptable_write_time, &tv2);
    proptable_writes ++;

    num_proptables ++;
    proptable_entries = 0;
    proptable_merges = 0;
}

void finalize_proptable_pass(void)
{
    int tablenum;
    char infilename[256];
    char outfilename[256];

    /* Flush out anything in the last proptable, and wait for its write to complete */
    proptable_full();

    /* Rename output propfiles to become the next pass's input propfiles */

    for (tablenum = 0; tablenum < num_proptables; tablenum ++) {
	sprintf(infilename, "propfile%04d_in", tablenum);
	sprintf(outfilename, "propfile%04d_out", tablenum);
	if (rename(outfilename, infilename) == -1) {
	    fatal("Can't rename '%s' to '%s': %s\n", outfilename, infilename, strerror(errno));
	    return;
	}
    }

    num_proptables = 0;
}

/* fetch_next_propentry()
 *
 * proptable_buffer[propbuf(tablenum, buffernum)]
 *         a malloc'ed buffer of PROPTABLE_BUFFER_SIZE bytes
 * proptable_buffer_index[tablenum]
 *         the local (relative to buffer) entry number of the NEXT entry in the buffer to be read
 * proptable_input_fds[tablenum]
 *         file descriptor of this proptable
 * proptable_current_buffernum[tablenum]
 *         current buffernum (from 0 to BUFFERS_PER_PROPTABLE) for this table
 */

int *proptable_input_fds = NULL;
proptable_entry_t **proptable_buffer;
proptable_entry_t **proptable_buffer_ptr;
proptable_entry_t **proptable_buffer_limit;

#define PROPTABLE_BUFFER_ENTRIES (1<<13)
#define PROPTABLE_BUFFER_BYTES (PROPTABLE_BUFFER_ENTRIES * PROPTABLE_FORMAT_BYTES)

void fetch_next_propentry(int tablenum, proptable_entry_t *dest)
{
    int ret;

    do {

	/* First, look for additional entries in the in-memory buffer.  Entries with all ones index
	 * (PROPTABLE_FORMAT_INDEX_MASK) are empty slots and are skipped.
	 */

	while ((char *)(proptable_buffer_ptr[tablenum]) + PROPTABLE_FORMAT_BYTES <= (char *)(proptable_buffer_limit[tablenum])) {

	    if (get_propentry_index(proptable_buffer_ptr[tablenum]) != PROPTABLE_FORMAT_INDEX_MASK) {
		memcpy(dest, proptable_buffer_ptr[tablenum], PROPTABLE_FORMAT_BYTES);
		proptable_buffer_ptr[tablenum] = (char *)(proptable_buffer_ptr[tablenum]) + PROPTABLE_FORMAT_BYTES;
		return;
	    }

	    proptable_buffer_ptr[tablenum] = (char*)(proptable_buffer_ptr[tablenum]) + PROPTABLE_FORMAT_BYTES;
	}

	/* Finished with this buffer.  Issue a read request for what its next contents should be
	 * (unless we've reached EOF).
	 */

	ret = read(proptable_input_fds[tablenum], proptable_buffer[tablenum], PROPTABLE_BUFFER_BYTES);
	if (ret == -1) {
	    perror("reading proptable");
	    return;
	}
	if (ret == 0) {
	    proptable_buffer_ptr[tablenum] = NULL;
	} else {
	    proptable_buffer_ptr[tablenum] = proptable_buffer[tablenum];
	    proptable_buffer_limit[tablenum] = (char *)(proptable_buffer[tablenum]) + ret;
	    if (ret % PROPTABLE_FORMAT_BYTES != 0) {
		fatal("Proptable read split a proptable entry\n");
	    }
	}

    } while (proptable_buffer_ptr[tablenum] != NULL);

    /* No, we're really at the end! */

    set_propentry_index(dest, PROPTABLE_FORMAT_INDEX_MASK);
}

futurevector_t initialize_tablebase_entry(tablebase_t *tb, index_t index);
void finalize_futuremove(tablebase_t *tb, index_t index, futurevector_t futurevector);

/* proptable_pass()
 *
 * Commit an old set of proptables into the entries array while writing a new set.
 */

void proptable_pass(int target_dtm)
{
    int i;
    int tablenum;
    int num_input_proptables = 0;

    void *sorting_network;
    int *proptable_num;
    int highbit;
    int network_node;

    char infilename[256];

#define SORTING_NETWORK_ELEM(n)  ((char *)sorting_network + PROPTABLE_FORMAT_BYTES * (n))

    index_t index;

    /* Count up (and open) input propfiles. */

    for (tablenum = 0; ; tablenum ++) {
	int fd;
	sprintf(infilename, "propfile%04d_in", tablenum);
	if ((fd = open(infilename, O_RDONLY | O_LARGEFILE)) == -1) break;
	proptable_input_fds = (int *) realloc(proptable_input_fds, (tablenum+1)*sizeof(int));
	if (proptable_input_fds == NULL) {
	    fatal("Can't realloc proptable_input_fds in proptable_pass()\n");
	    return;
	}
	proptable_input_fds[tablenum] = fd;
    }

    num_input_proptables = tablenum;

    for (highbit = 1; highbit <= num_input_proptables; highbit <<= 1);

    proptable_buffer = (proptable_entry_t **) calloc(num_input_proptables, sizeof(proptable_entry_t *));
    proptable_buffer_ptr = (proptable_entry_t **) calloc(num_input_proptables, sizeof(proptable_entry_t *));
    proptable_buffer_limit = (proptable_entry_t **) calloc(num_input_proptables, sizeof(proptable_entry_t *));

    if ((proptable_buffer == NULL) || (proptable_buffer_ptr == NULL) || (proptable_buffer_limit == NULL)) {
	fatal("Can't malloc proptable buffers in proptable_pass()\n");
	return;
    }

    /* Alloc and read initial buffers for all input proptables */

    for (tablenum = 0; tablenum < num_input_proptables; tablenum ++) {

	int ret;

	proptable_buffer[tablenum] = malloc(PROPTABLE_BUFFER_BYTES);
	if (proptable_buffer[tablenum] == NULL) {
	    fatal("Can't malloc proptable buffer: %s\n", strerror(errno));
	    return;
	}

	ret = read(proptable_input_fds[tablenum], proptable_buffer[tablenum], PROPTABLE_BUFFER_BYTES);
	if (ret == -1) {
	    fatal("initial read on proptable: %s\n", strerror(errno));
	    return;
	}
	if (ret == 0) {
	    warning("Empty input proptable ?\n");
	    proptable_buffer_ptr[tablenum] = NULL;
	} else {
	    proptable_buffer_ptr[tablenum] = proptable_buffer[tablenum];
	    proptable_buffer_limit[tablenum] = (char *)(proptable_buffer[tablenum]) + ret;
	    if (ret % PROPTABLE_FORMAT_BYTES != 0) {
		fatal("Proptable read split a proptable entry\n");
	    }
	}
    }


    /* Malloc the sorting network */

    sorting_network = malloc(2*highbit * PROPTABLE_FORMAT_BYTES);
    proptable_num = (int *) malloc(2*highbit * sizeof(int));

    if ((sorting_network == NULL) || (proptable_num == NULL)) {
	fatal("Can't malloc sorting network in proptable_pass()\n");
	return;
    }

    /* The sorting network.
     *
     * We need to read a bunch of proptables in sorted order.  Each proptable is itself sorted, but
     * now we need to impose a total sort on all of them.  We do this by maintaining a binary tree
     * (the sorting network), each node a proptable entry, with the input proptables 'feeding' the
     * leaves and each non-leaf the less-than comparison of the two nodes below it.  We also track
     * which proptable each entry came from originally.  Once initialized, we just read the root of
     * the tree to get the next proptable entry, then refill whichever leaf we got the entry from
     * (that's why we track proptables for each entry), and run back up the tree from that leaf
     * only, recomputing the comparisons.  And it's laid out in memory as an array, like this:
     *
     *                    /4
     *                /-2--5
     *               1
     *                \-3--6
     *                    \7
     *
     * so that we can run that last step (backing up the tree, from right to left, in the diagram),
     * just by right shifting the index.  I got this idea from Knuth.
     */

    /* Initialize the sorting network.
     *
     * First, fill in the upper half of the network with either the first entry from a proptable,
     * or an "infinite" entry for slots with no proptables.  Then, sort into the lower half
     * of the network.
     */

    for (i = 0; i < highbit; i ++) {
	if (i < num_input_proptables) {
	    fetch_next_propentry(i, SORTING_NETWORK_ELEM(highbit + i));
	    proptable_num[highbit + i] = i;
	} else {
	    set_propentry_index(SORTING_NETWORK_ELEM(highbit + i), PROPTABLE_FORMAT_INDEX_MASK);
	    proptable_num[highbit + i] = -1;
	}
    }

    for (network_node = highbit-1; network_node > 0; network_node --) {
	if (get_propentry_index(SORTING_NETWORK_ELEM(2*network_node))
	    < get_propentry_index(SORTING_NETWORK_ELEM(2*network_node + 1))) {
	    memcpy(SORTING_NETWORK_ELEM(network_node),
		   SORTING_NETWORK_ELEM(2*network_node), PROPTABLE_FORMAT_BYTES);
	    proptable_num[network_node] = proptable_num[2*network_node];
	} else {
	    memcpy(SORTING_NETWORK_ELEM(network_node),
		   SORTING_NETWORK_ELEM(2*network_node + 1), PROPTABLE_FORMAT_BYTES);
	    proptable_num[network_node] = proptable_num[2*network_node+1];
	}
    }

    /* Now, process the data through the sorting network.
     *
     * I once tried skipping to the index on the first proptable entry out of the sorting network,
     * but I forgot that this loop has to run on _every_ index, in order to check if it needs to be
     * finalized on this pass.
     */

    for (index = 0; index <= max_index(current_tb); index ++) {

	futurevector_t futurevector = 0;
	futurevector_t possible_futuremoves = 0;

	if (get_propentry_index(SORTING_NETWORK_ELEM(1)) < index) {
	    fatal("Out-of-order entries in sorting network: %llx %llx <-%d\n",
		  *((uint64_t *) SORTING_NETWORK_ELEM(1)), *(((uint64_t *) SORTING_NETWORK_ELEM(1)) + 1), proptable_num[1]);
	}

	if (target_dtm == 0) {
	    possible_futuremoves = initialize_tablebase_entry(current_tb, index);
	}

	while (get_propentry_index(SORTING_NETWORK_ELEM(1)) == index ) {

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

		commit_proptable_entry(SORTING_NETWORK_ELEM(1));

	    } else if (get_propentry_futurevector(SORTING_NETWORK_ELEM(1)) & possible_futuremoves) {

		if ((get_propentry_futurevector(SORTING_NETWORK_ELEM(1)) & possible_futuremoves)
		    != get_propentry_futurevector(SORTING_NETWORK_ELEM(1))) {

		    int propentry_futuremoves=0;
		    futurevector_t propentry_futurevector = get_propentry_futurevector(SORTING_NETWORK_ELEM(1));

		    while (propentry_futurevector != 0) {
			if ((propentry_futurevector & 1) != 0) propentry_futuremoves ++;
			propentry_futurevector >>= 1;
		    }

		    if (get_propentry_movecnt(SORTING_NETWORK_ELEM(1)) == propentry_futuremoves) {

			int propentry_possible_futuremoves = 0;
			futurevector_t propentry_possible_futurevector
			    = get_propentry_futurevector(SORTING_NETWORK_ELEM(1)) & possible_futuremoves;
			set_propentry_futurevector(SORTING_NETWORK_ELEM(1), propentry_possible_futurevector);

			while (propentry_possible_futurevector != 0) {
			    if ((propentry_possible_futurevector & 1) != 0) propentry_possible_futuremoves ++;
			    propentry_possible_futurevector >>= 1;
			}

			set_propentry_movecnt(SORTING_NETWORK_ELEM(1), propentry_possible_futuremoves);
		    } else {
			global_position_t global;
			index_to_global_position(current_tb, get_propentry_index(SORTING_NETWORK_ELEM(1)), &global);
			warning("Mixed possible/impossible futuremoves: %s\n", global_position_to_FEN(&global));
		    }
		}

		commit_proptable_entry(SORTING_NETWORK_ELEM(1));

		if (get_propentry_futurevector(SORTING_NETWORK_ELEM(1)) & futurevector) {
		    global_position_t global;
		    index_to_global_position(current_tb, get_propentry_index(SORTING_NETWORK_ELEM(1)), &global);
		    fatal("Futuremoves multiply handled: %s\n", global_position_to_FEN(&global));
		}

		futurevector |= get_propentry_futurevector(SORTING_NETWORK_ELEM(1));

	    }

	    fetch_next_propentry(proptable_num[1], SORTING_NETWORK_ELEM(highbit + proptable_num[1]));

	    network_node = highbit + proptable_num[1];

	    while (network_node > 1) {
		network_node >>= 1;
		if (get_propentry_index(SORTING_NETWORK_ELEM(2*network_node))
		    < get_propentry_index(SORTING_NETWORK_ELEM(2*network_node+1))) {
		    memcpy(SORTING_NETWORK_ELEM(network_node),
			   SORTING_NETWORK_ELEM(2*network_node), PROPTABLE_FORMAT_BYTES);
		    proptable_num[network_node] = proptable_num[2*network_node];
		} else {
		    memcpy(SORTING_NETWORK_ELEM(network_node),
			   SORTING_NETWORK_ELEM(2*network_node + 1), PROPTABLE_FORMAT_BYTES);
		    proptable_num[network_node] = proptable_num[2*network_node+1];
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

    for (tablenum = 0; tablenum < num_input_proptables; tablenum ++) {
	close(proptable_input_fds[tablenum]);
	sprintf(infilename, "propfile%04d_in", tablenum);
	unlink(infilename);
    }

    for (tablenum = 0; i < num_input_proptables; tablenum ++) {
	free(proptable_buffer[tablenum]);
    }

    free(proptable_buffer);
    free(proptable_buffer_ptr);
    free(proptable_buffer_limit);
    free(proptable_input_fds);

    proptable_buffer = NULL;
    proptable_buffer_ptr = NULL;
    proptable_buffer_limit = NULL;
    proptable_input_fds = NULL;

    free(sorting_network);
    free(proptable_num);
}

void insert_into_proptable(proptable_entry_t *pentry)
{
    int propentry;
    int zerooffset;
    index_t index = get_propentry_index(pentry);
    static int scaling_factor = 0;

    /* I had a bug here with the scaling_factor rounding down - that's why we increment by one */

    if (scaling_factor == 0) {
	scaling_factor = max_index(current_tb) / num_propentries;
	scaling_factor ++;
	info("Scaling factor %d\n", scaling_factor);
    }

 retry:

    /* We need an index into the proptable that maintains the index sort order of the entries. */

    propentry = index / scaling_factor;

    if (get_propentry_index(PROPTABLE_ELEM(propentry)) == PROPTABLE_FORMAT_INDEX_MASK) {
	/* empty slot: insert at propentry */
	/* proptable[propentry] = entry; */
	insert_at_propentry(propentry, pentry);
	return;
    } else if (get_propentry_index(PROPTABLE_ELEM(propentry)) == index) {
	/* entry at slot with identical index: merge at propentry */
	/* proptable[propentry] += entry; */
	merge_at_propentry(propentry, pentry);
	return;
    } else if (get_propentry_index(PROPTABLE_ELEM(propentry)) > index) {
	/* entry at slot greater than index to be inserted */
	while ((get_propentry_index(PROPTABLE_ELEM(propentry)) != PROPTABLE_FORMAT_INDEX_MASK)
	       && (get_propentry_index(PROPTABLE_ELEM(propentry)) > index) && (propentry > 0)) propentry --;
	if (get_propentry_index(PROPTABLE_ELEM(propentry)) == PROPTABLE_FORMAT_INDEX_MASK) {
	    /* empty slot at lower end of a block all gt than index: insert there */
	    /* proptable[propentry] = entry; */
	    insert_at_propentry(propentry, pentry);
	    return;
	} else if (get_propentry_index(PROPTABLE_ELEM(propentry)) == index) {
	    /* identical slot in a block: merge there */
	    /* proptable[propentry] += entry; */
	    merge_at_propentry(propentry, pentry);
	    return;
	} else if (get_propentry_index(PROPTABLE_ELEM(propentry)) > index) {
	    /* we're at the beginning of the table and the first entry is gt index */
	    for (zerooffset = 1; zerooffset <= MAX_ZEROOFFSET; zerooffset ++) {
		if (get_propentry_index(PROPTABLE_ELEM(zerooffset)) == PROPTABLE_FORMAT_INDEX_MASK) {
		    /* proptable[1:zerooffset] = proptable[0:zerooffset-1]; */
		    memmove((char *)proptable + PROPTABLE_FORMAT_BYTES, proptable,
			    (zerooffset) * PROPTABLE_FORMAT_BYTES);
		    /* proptable[0] = entry; */
		    insert_at_propentry(0, pentry);
		    return;
		}
	    }
	    /* ran out of space - table is "full" */
	    proptable_full();
	    goto retry;
	} else {
	    /* still in the block; propentry is lt index and propentry+1 is gt index: fall through */
	}
    } else {
	/* entry at slot less than index to be inserted */
	while ((get_propentry_index(PROPTABLE_ELEM(propentry)) != PROPTABLE_FORMAT_INDEX_MASK)
	       && (get_propentry_index(PROPTABLE_ELEM(propentry)) < index)
	       && (propentry < num_propentries - 1)) propentry ++;
	if (get_propentry_index(PROPTABLE_ELEM(propentry)) == PROPTABLE_FORMAT_INDEX_MASK) {
	    /* empty slot at upper end of a block all lt than index: insert there */
	    /* proptable[propentry] = entry; */
	    insert_at_propentry(propentry, pentry);
	    return;
	} else if (get_propentry_index(PROPTABLE_ELEM(propentry)) == index) {
	    /* identical slot in a block: merge there */
	    /* proptable[propentry] += entry; */
	    merge_at_propentry(propentry, pentry);
	    return;
	} else if (get_propentry_index(PROPTABLE_ELEM(propentry)) < index) {
	    /* we're at the end of the table and the last entry is lt index */
	    for (zerooffset = 1; zerooffset <= MAX_ZEROOFFSET; zerooffset ++) {
		if (get_propentry_index(PROPTABLE_ELEM(num_propentries - 1 - zerooffset)) == PROPTABLE_FORMAT_INDEX_MASK) {
		    /* proptable[num_propentries-zerooffset-1 : num_propentrys-2]
		     *    = proptable[num_propentries-zerooffset : num_propentries-1];
		     */
		    memmove((char *)proptable + (num_propentries - 1 - zerooffset) * PROPTABLE_FORMAT_BYTES,
			    (char *)proptable + (num_propentries - zerooffset) * PROPTABLE_FORMAT_BYTES,
			    (zerooffset) * PROPTABLE_FORMAT_BYTES);
		    /* proptable[num_propentries-1] = entry; */
		    insert_at_propentry(num_propentries - 1, pentry);
		    return;
		}
	    }
	    /* ran out of space - table is "full" */
	    proptable_full();
	    goto retry;
	} else {
	    /* propentry is gt index and propentry-1 is lt index */
	    propentry --;
	}
    }

    /* We found a boundary within a block: propentry is lt index and propentry+1 is gt index */

    for (zerooffset = 1; zerooffset <= MAX_ZEROOFFSET; zerooffset ++) {
	if ((propentry + zerooffset < num_propentries - 1)
	    && (get_propentry_index(PROPTABLE_ELEM(propentry+zerooffset)) == PROPTABLE_FORMAT_INDEX_MASK)) {
	    /* proptable[propentry+2 : propentry+zerooffset]
	     *    = proptable[propentry+1 : propentry+zerooffset-1];
	     */
	    memmove((char *)proptable + PROPTABLE_FORMAT_BYTES * (propentry + 2),
		    (char *)proptable + PROPTABLE_FORMAT_BYTES * (propentry + 1),
		    (zerooffset-1) * PROPTABLE_FORMAT_BYTES);
	    /* proptable[propentry+1] = entry; */
	    insert_at_propentry(propentry+1, pentry);
	    return;
	}
	if ((propentry - zerooffset >= 0)
	    && (get_propentry_index(PROPTABLE_ELEM(propentry-zerooffset)) == PROPTABLE_FORMAT_INDEX_MASK)) {
	    /* proptable[propentry-zerooffset : propentry-1]
	     *    = proptable[propentry-zerooffset+1 : propentry];
	     */
	    memmove((char *)proptable + PROPTABLE_FORMAT_BYTES * (propentry - zerooffset),
		    (char *)proptable + PROPTABLE_FORMAT_BYTES * (propentry - zerooffset + 1),
		    zerooffset * PROPTABLE_FORMAT_BYTES);
	    /* proptable[propentry] = entry; */
	    insert_at_propentry(propentry, pentry);
	    return;
	}
    }

    /* zerooffset > MAX_ZEROOFFSET: ran of space - table is "full" */
    proptable_full();
    goto retry;
}

void insert_new_propentry(index_t index, int dtm, unsigned int movecnt, boolean PTM_wins_flag, int futuremove)
{
    char entry[MAX_FORMAT_BYTES];
    void *ptr = entry;

    /* If we're running multi-threaded, then there is a possibility that two different threads will
     * try to insert into the proptable at the same time.
     */

#ifdef USE_THREADS
    static pthread_mutex_t commit_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef USE_THREADS
    pthread_mutex_lock_instrumented(&commit_lock);
#endif

    memset(ptr, 0, PROPTABLE_FORMAT_BYTES);

    set_propentry_index(ptr, index);
    set_propentry_dtm(ptr, dtm);
    set_propentry_movecnt(ptr, movecnt);

    if (futuremove != NO_FUTUREMOVE) {
	set_propentry_futurevector(ptr, FUTUREVECTOR(futuremove));
    }

    set_propentry_PTM_wins_flag(ptr, PTM_wins_flag);

#ifdef DEBUG_MOVE
    if (index == DEBUG_MOVE)
	fprintf(stderr, "Propentry: %0" PRIx64 " %0" PRIx64 "\n",
		*((uint64_t *) ptr), *(((uint64_t *) ptr) + 1));
#endif

    insert_into_proptable(ptr);

#ifdef USE_THREADS
    pthread_mutex_unlock(&commit_lock);
#endif
}

int initialize_proptable(int proptable_MBs)
{
    num_propentries = proptable_MBs * 1024 * 1024 / PROPTABLE_FORMAT_BYTES;

    proptable = malloc(num_propentries * PROPTABLE_FORMAT_BYTES);
    if (proptable == NULL) {
	fatal("Can't malloc proptable: %s\n", strerror(errno));
	return 0;
    } else {
	int kilobytes = (num_propentries * PROPTABLE_FORMAT_BYTES)/1024;
	if (kilobytes < 1024) {
	    info("Malloced %dKB for proptable\n", kilobytes);
	} else {
	    info("Malloced %dMB for proptable\n", kilobytes/1024);
	}
    }

    /* POSIX doesn't guarantee that the memory will be zeroed (but Linux seems to zero it) */
    memset(proptable, 0xff, num_propentries * PROPTABLE_FORMAT_BYTES);

    tpie::tpie_init();
    tpie::get_memory_manager().set_limit(1<<30);

    return 1;
}


}
