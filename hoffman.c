/*
 * HOFFMAN - a chess endgame tablebase builder
 *
 * by Brent Baccala
 *
 * August, 2006
 *
 * no rights reserved; you may freely copy, modify, or distribute
 * HOFFMAN
 *
 * written in C for speed
 *
 * For those not up on Americana, the program is named after Trevor
 * Hoffman, an All Star baseball pitcher who specializes in "closing"
 * games.  It was written specifically for The World vs. Arno Nickel
 * game.
 *
 * This program will calculate a tablebase for up to four pieces
 * (called the 'mobile' pieces) in a static configuration of other
 * 'frozen' pieces.  The mobile pieces could possibly be pawns.  The
 * frozen pieces could possibly be kings.
 *
 * Three piece tablebases with no frozen pieces can also be built.
 * These are the only tablebases that are completely self contained
 * and don't depend on other tablebases (the 'futurebases').
 *
 * Feed this program a list of futurebases on the command line.
 *
 * Feed this program an XML control file on the command line.
 */

typedef struct {
} tablebase_description;

tablebase_description parse_XML()
{
}

int32 max_position(static_configuration *config)
{
}

int32 position_to_index(static_configuration *config, int side_to_move,
			int mobile1, int mobile2, int mobile3, int mobile4)
{
    /* This function, given a list of board positions for the mobile
     * pieces and an indication of which side is to move, returns an
     * index into the table.
     *
     * The reason we pass the static configuration in explicitly is
     * that we will need to use this function to calculate not only
     * indices into our own table, but also into future tables with
     * different static configs.  Actually, I'm not sure about this.
     * Maybe it's only the matching function index_to_position() that
     * we need for future tables.  In any event, we'll need this
     * function to probe tables when we want to actually use them.
     *
     * Initially, this function can be very simple (multiplying numbers
     * together), but to build smaller tables it can be more precise.
     *
     * For example, two kings can never be next to each other.  Pieces
     * can never be on top of each other, or on top of static pieces.
     * The side to move can not be in check.
     *
     * Returns either an index into the table, or -1 (probably) if
     * the position is illegal.
     *
     * Let's just ASSERT right now that this function can be used to
     * check for illegal positions.  In fact, it is our primary
     * function to check for illegal positions.  We call it and see if
     * it returns -1.
     */
}

/* OK, maybe not.  Maybe need to check index numbers, too. (Unless
 * all positions in the table are legal!)
 */

bool check_legality_of_index(static_configuration *config, int32 index)
{
}

/* How about if "position" is a structure containing an 8x8 char array
 * with ASCII characters representing each piece?
 *
 * Also need a flag to indicate which side is to move.
 *
 * The problem is we don't want to hunt through the whole thing just
 * to find a mobile piece.
 *
 * So how about an array as described, plus a flag, plus four numbers
 * (0-63) indicating the positions of the mobile pieces?  That way, we
 * can easily check if possible moves are legal by looking for pieces
 * that block our moving piece.
 *
 * Also, if we're not interested in massive speed optimization, we
 * don't need to worry about moving a piece that's pinned on our king,
 * for example.  The resulting position will already have been flagged
 * illegal in the table.
 *
 * We actually need to call this function a lot, so we want it to be
 * fast, but I don't want to optimize to the point where bugs can
 * creep in.
 *
 * So how about a static ASCII char array that contains the frozen
 * pieces and not the mobiles?  Everytime we call index_to_position,
 * we memcpy from the static array into the array in the position
 * structure.  Then we compute the positions of the mobile pieces and
 * plug their ASCII chars into the dynamic array at the right places.
 *
 */

int index_to_position(static_configuration *config, int32 index, int piece)
{
    /* Given an index and a mobile piece number (from 1 to 4)
     * returns the board position (1 to 64, or 0 to 63) where that
     * piece is in the index.  Obviously has to correspond to
     * position_to_index() and it's a big bug if it doesn't.
     */
}

/**** TABLE OPERATIONS ****/

/* "Designed to multi-thread"
 *
 * Keep atomic operations confined to single functions.  Design
 * functions so that functions calling them don't need to know the
 * details of table format, either.
 *
 * These "add one" functions (atomically) add one to the count in
 * question, subtract one from the total move count, and flag the
 * position as 'ready for propagation' (maybe this is just a move
 * count of zero) if the total move count goes to zero.
 */

add_one_to_white_wins(table, int32 position)
{
}

add_one_to_black_wins(table, int32 position)
{
}

add_one_to_white_draws(table, int32 position)
{
}

add_one_to_black_draws(table, int32 position)
{
}

calculate_all_possible_futuremoves()
{
    consider all possible captures;

    consider all possible pawn moves, including queening and knighting;

    put them in some kind of list or array;

    flag them according to our pruning instructions;

    sort them so that if the same table is used for several positions, they appear together on the list;
								
    later: strike them off the list as we process their respective files;
}

propagate_position_from_future_table(position)
{
    for (all possible captures and pawn moves in position) {

	check if one of our future tables matches this move;

	if so {
	    fetch result from future table;
	    if (result == white_wins) break;
	    if (result == white_draws) {
		result = white_draws;
		continue;
		/* keep looking for a win */
	    }
	    /* but whose move is it? */
	}

	else if move is flagged prune-our-move, decrement counts;

	else if move is flagged prune-his-move {

	    for (all possible responses to his move) {
		if (one of our future tables matches a white win) {
		    propagate a white win;
		}
		elsif (one of our future tables matches a white draw) {
		    propagate a white draw;
		} else {
		    /* this is where we vary for a more complex program */
		    propagate a black win;
		}
	    }
	}
    }

    /* This is where we make pruning decisions, if we don't want to
     * fully analyze out the tree past the table we're now building.
     * Of course, this will affect the accuracy of the table; the
     * table is a result of BOTH the position it was set up for AND
     * the pruning decisions (and any pruning decisions made on the
     * future tables used to calculate this one).
     *
     * We specify pruning in a simple way - by omitting future tables
     * for moves we don't want to consider.  This can be dangerous, so
     * we require this feature to be specifically enabled for each
     * move by a command-line switch.  Actually, we use two switches,
     * one to calculate a table for OUR SIDE to move, and another if
     * it is the OTHER SIDE to move.
     *
     * So, --prune-our-move e3e4 prunes a pawn move (assuming this is
     * a table with a static pawn on e3) by simply ignoring e3e4 as a
     * possible move.
     *
     * Pruning an opponent's move is more complex because we step a
     * half-move into the future and consider our own next move.  This
     * costs us little, since we can control our own move and
     * therefore don't have to consider all possibilities, and
     * improves a lot.  If future tables exist for any of our
     * responses, they are used.  If no such future tables exist, then
     * the move is regarded as a lost game.
     *
     * So, --prune-his-move e7e8 prunes a pawn promotion (assuming a
     * static pawn on e7) by considering all possible positions
     * resulting after the pawn promotion (to either Q or N) AND the
     * answering move.  The resulting game is regarded as a win for
     * white unless both Q and N promotions have an answer that
     * leads to another table with a win or draw for black.
     *
     * For example, let's say we're looking at a Q-and-P vs. Q-and-P
     * endgame.  There are four mobile pieces (2 Ks and 2 Qs), so we
     * can handle this.  But if one of the pawns queens, then we've
     * got a game with five mobile pieces, and that's too complex.
     * But we don't want to completely discard all possible enemy
     * promotions, if we can immediately capture the new queen (or the
     * old one).  So we specify something like --prune-his-move e7e8
     * and pass in a tablebase for a Q-and-P vs. Q endgame.
     *
     * We also check for immediate checkmates or stalemates.
     *
     * Question: do we really need to flag this at all?  Probably yes,
     * because we don't want this pruning to occur by accident.
     *
     * Another reason to flag it is that we want to label in the file
     * header that this pruning was done.  In particular, if we use a
     * pruned tablebase to compute another (earlier) pruned tablebase,
     * we want to make sure the pruning is consistent, i.e. "our" side
     * has to stay the same.  This can only be guaranteed if we
     * explicitly flag which side is which in the file header.
     *
     * Pruning doesn't affect the size of the resulting tablebase.  We
     * discard the extra information.  If the pruned move is actually
     * made in the game, then you have to calculate all possible next
     * moves and check your tablebases for them.  This seems
     * reasonable.
     *
     */

}

propagate_move_from_future_table()
{
    if (future_table resulted from capture) {
	/* need to consider pawn captures seperately? */

    } else if (future_table resulted from pawn move) {

	future_table could result from pawn queening;
	future_table could result from pawn knighting;

    }
}

propagate_moves_from_futurebases()
{
    for (all legal positions in our table) {
	propagate_position_from_future_table(position);
    }
}

propagate_moves_from_futurebases()
{
    calculate_all_possible_futuremoves();

    for (all futurebases on command line or control file) {
	propagate_moves_from_futurebase();
    }

    if (any futuremoves still unhandled) {
	die(error);
    }
}

propagate_move_within_table(table, index)
{

    ASSERT (table,index) == WIN/LOSS IN x MOVES or DRAW;

    /* We want to check to make sure the mate-in number of the
     * position in the database matches a mate-in variable in this
     * routine.  If we're propagating moves from a future table, we
     * might get tables with a whole range of mate-in counts, so we
     * want to make sure we go through them in order.
     */

    /* We've moving BACKWARDS in the game, so we want the pieces of
     * the player who is NOT TO PLAY here - this is the LAST move
     * we're considering, not the next move.
     */

    /* How about if "position" is just an 8x8 unsigned char array
     * with ASCII characters representing each pieces and the
     * high order bit set if the piece is "mobile"?
     *
     * Also need a flag to indicate which side is to move.
     *
     * The problem is we don't want to hunt through the whole thing
     * just to find a mobile piece.
     *
     * So how about an array as described, plus a flag, plus four
     * numbers (0-63) indicating the positions of the mobile pieces?
     * That way, we can easily check if possible moves are legal
     * by looking for pieces that block our moving piece.
     *
     * Also, if we're not interested in massive speed optimization,
     * we don't need to worry about moving a piece that's pinned
     * on our king, for example.  The resulting position will
     * already have been flagged illegal in the table.
     *
     */

    index_to_position(table, index);

    foreach (mobile piece of player NOT TO PLAY) {

	/* current_position[piece] is a number from 0 to 63 corresponding
	 * to the different squares on the chess board
	 */

	/* possible_moves returns a pointer to an array of possible
	 * new position numbers for this piece.  These are BACKWARDS
	 * moves in the game.  The positions returned are legal.
	 *
	 * possible_moves has to look at the entire current position
	 * to build this array, because otherwise it might move a
	 * piece "through" another piece.
	 */

	possible_moves(current_position[piece], type_of[piece]);

	foreach (new position) {

	}
    }
}

initialize_table(table)
{
    /* This is here because we don't want to be calling max_index()
     * everytime through the loop below
     */

    int32 max_index_static = max_index(table);

    for (int32 index=0; index < max_index_static; index++) {
	/* check_legality_of_index() might not work yet */
	if (! check_legality_of_index(table, index)) {
	    flag position as illegal;
	}
    }

    for (int32 index=0; index < max_index_static; index++) {
	if (check_legality_of_index(table, index)) {
	    if (is_checkmate(table,index)) {
		initialize_index_with_checkmate(index, movecnt);
	    } else if (is_stalemate(table,index)) {
		initialize_index_with_stalemate(index, movecnt);
	    } else {
		int movecnt;
		for (piece=0; piece<=3; piece++) {
		    forall (possible moves of this mobile piece) movecnt++;
		}
		for (;;) {
		    forall (possible moves of frozen pieces) movecnt++;
		}
		initialize_index_with_movecnt(index, movecnt);
	    }
	} else {
	    flag position as illegal;
	}
    }
}

mainloop()
{
    create_data_structure_from_control_file();

    initialize_table();

    max_moves_to_win_or_draw = propagate_moves_from_futurebases();

    moves_to_win_or_draw = 0;
    while (progress_made || moves_to_win_or_draw < max_moves_to_win_or_draw) {
	for (int32 index=0; index < max_index_static; index++) {
	    if (needs_propagation(table, index)
		&& moves_to_win_or_draw(table, index) == moves_to_win_or_draw){
		propagate_move_within_table(table, index);
		progress_made = 1;
	    }
	}
	moves_to_win_or_draw ++;
    }

    flag_everything_else_drawn_by_repetition();

    write_output_tablebase();
}
