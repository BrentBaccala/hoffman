/*
 * HOFFMAN - a chess endgame tablebase builder
 *
 * by Brent Baccala
 *
 * written in C for speed
 *
 * This program will calculate a tablebase for up to four pieces
 * (called the 'mobile' pieces) in a static pawn configuration.  The
 * mobile pieces could possibly be pawns.  Two of the mobile pieces
 * (obviously) will be the two kings.
 *
 * Three piece tablebases with no pawns can also be built.  These are
 * the only tablebases that are completely self contained and don't
 * depend on other tablebases (the 'future' tablebases).
 *
 * Feed this program a list of future tables on command line.
 */

calculate_all_posible_future_tables()
{
    consider all possible captures;

    consider all possible pawn moves, including queening and knighting;

    /* Pawn queening or knighting can get us into more complicated
     * endgames that we can't handle, so we consider a few extra
     * special cases to try and simplify them down before completely
     * giving up on them.
     */

    if (no future table for a pawn queening or knighting) {
	go one half move forward;
	consider all immediate captures of new queen/knight {
	    if no future table for this, ABORT;
	}
	consider checking moves {
	    look for all legal responses to the check;
	    if no future tables for all of them, ABORT;
	}
	if any other moves are possible, ABORT;
    }

    /* This is where we make pruning decisions, if we don't want to
     * fully analyze out the tree past the table we're now building.
     * Of course, this will affect the accuracy of the table; the
     * table is a result of BOTH the position it was set up for AND
     * the decisions made to prune future tables (and any pruning
     * decisions made on the future tables that exist).
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
     * possible move (if no future table exists for the pawn on e4).
     *
     * On the other hand, --prune-his-move e7e8 prunes a pawn
     * promotion (assuming a static pawn on e7) by treating the e7e8
     * move as a lost game if no future table exists for the resulting
     * Q or N game (both must exist).
     *
     * The --prune-his-move switch wouldn't be all that useful in the
     * form described (because it would result in almost every
     * position being lost), but we continue to step into the future
     * for a number of moves determined by the --prune-depth switch
     * (default 1), always ending by considering our own moves.
     *
     * So, --prune-his-move e7e8 will actually consider all possible
     * positions resulting after the pawn promotion AND the answering
     * move.  If future tables exist for any of these cases, they are
     * used.  If no such future tables exist, the move is regarded as
     * a lost game, as before.
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
     * In the same situation, if we specify --prune-depth 2, then
     * we'll consider the pruned enemy move, all of our possible
     * answers, all possible enemy answers to that, and all of our
     * possible answers to that enemy answer.  Anything deeper than
     * this is impractical.  Even --prune-depth 2 may not be
     * practical, since the move tree for the pruned move has to
     * computed for each of the ~16 million positions in the table.
     *
     * Pruning could significantly increase the size of the resulting
     * tablebase if for every possible position in the table we need
     * to specify the response to the pruned move if it's made from
     * that position.  I'm thinking about discarding this extra
     * information, though.  If the pruned move is actually made in
     * the game, then you have to calculate all possible next moves
     * and check your tablebases for them.  This seems reasonable.
     *
     */

    if (no future table for a move) {
	go one half move forward;
	consider ALL POSSIBLE LEGAL MOVES {
	    recurse looking for future table;
	    if (moveswitch) future tables MUST EXIST;
	}
    }
    
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

propagate_move_within_table()
{

    /* We've moving BACKWARDS in the game, so we want the pieces of
     * the player who is NOT TO PLAY here - this is the LAST move
     * we're considering, not the next move.
     */

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
