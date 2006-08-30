/* -*- mode: C; eval: (c-set-style "stroustrup"); fill-column: 100 -*-
 *
 * HOFFMAN - a chess endgame tablebase builder
 *
 * by Brent Baccala
 *
 * August, 2006
 *
 * no rights reserved; you may freely copy, modify, or distribute HOFFMAN
 *
 * written in C for speed
 *
 * This program is formated for a (minimum) 100 character wide display.
 *
 * For those not up on Americana, the program is named after Trevor Hoffman, an All Star baseball
 * pitcher who specializes in "closing" games.  It was written specifically for The World vs. Arno
 * Nickel game.
 *
 * This program will calculate a tablebase for up to four pieces (called the 'mobile' pieces) in a
 * static configuration of other 'frozen' pieces.  The mobile pieces could possibly be pawns.  The
 * frozen pieces could possibly be kings.
 *
 * Three piece tablebases with no frozen pieces can also be built.  These are the only tablebases
 * that are completely self contained and don't depend on other tablebases (the 'futurebases').
 *
 * Feed this program a list of futurebases on the command line.
 *
 * Feed this program an XML control file on the command line.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* Maximum number of mobile pieces; used to simplify various arrays
 *
 * "8" may seem absurd, but it's probably about right.  "4" is easily doable in memory.  "5"
 * requires sweeping passes across a file on disk.  "6" and "7" are worse than "5", but doable with
 * severe restrictions on the movements of the pieces.  So "8" is enough.
 */

#define MAX_MOBILES 8

typedef struct {
    int num_mobiles;
} tablebase_description;

tablebase_description parse_XML()
{
}

typedef tablebase_description static_configuration;
typedef tablebase_description tablebase;

int32 max_position(static_configuration *config)
{
}

int32 position_to_index(static_configuration *config, int side_to_move,
			int mobile1, int mobile2, int mobile3, int mobile4)
{
    /* This function, given a list of board positions for the mobile pieces and an indication of
     * which side is to move, returns an index into the table.
     *
     * The reason we pass the static configuration in explicitly is that we will need to use this
     * function to calculate not only indices into our own table, but also into future tables with
     * different static configs.  Actually, I'm not sure about this.  Maybe it's only the matching
     * function index_to_position() that we need for future tables.  In any event, we'll need this
     * function to probe tables when we want to actually use them.
     *
     * Initially, this function can be very simple (multiplying numbers together), but to build
     * smaller tables it can be more precise.
     *
     * For example, two kings can never be next to each other.  Pieces can never be on top of each
     * other, or on top of static pieces.  The side to move can not be in check.
     *
     * Returns either an index into the table, or -1 (probably) if the position is illegal.
     *
     * Let's just ASSERT right now that this function can be used to check for illegal positions.
     * In fact, it is our primary function to check for illegal positions.  We call it and see if it
     * returns -1.
     */
}

/* OK, maybe not.  Maybe need to check index numbers, too. (Unless all positions in the table are
 * legal!)
 */

bool check_legality_of_index(static_configuration *config, int32 index)
{
}

/* How about if "position" is a structure containing an 8x8 char array with ASCII characters
 * representing each piece?  No.  Too slow.  Position manipulation is at the core of this program.
 * It has to be fast.
 *
 * So we use a 64-bit vector with one bit for each board position, in addition to a flag to indicate
 * which side is to move and four numbers (0-63) indicating the positions of the mobile pieces.
 * That way, we can easily check if possible moves are legal by looking for pieces that block our
 * moving piece.
 *
 * Also need a quick way to check captures.  Do this using a black_vector and a white_vector.
 *
 * We don't worry about moving a piece that's pinned on our king, for example.  The resulting
 * position will already have been flagged illegal in the table.
 *
 * We actually need to call this function a lot, so we want it to be fast, but I don't want to
 * optimize to the point where bugs can creep in.
 *
 * So how about a static 64-bit vector with bits set for the frozen pieces but not the mobiles?
 * Everytime we call index_to_position, copy from the static vector into the position structure.
 * Then we compute the positions of the mobile pieces and plug their bits into the structure's
 * vector at the right places.
 *
 */

typedef struct {
    int64 board_vector;
    int64 white_vector;
    int64 black_vector;
    short side_to_move;
    short mobile_piece_position[MAX_MOBILES];
} position;

/* any reason to do this?  just for one mobile? */
int index_to_mobile_position(static_configuration *config, int32 index, int piece)
{}

boolean index_to_position(static_configuration *config, int32 index)
{
    /* Given an index, returns the board position.  Obviously has to
     * correspond to position_to_index() and it's a big bug if it
     * doesn't.
     */
}

/**** TABLE OPERATIONS ****/

/* "Designed to multi-thread"
 *
 * Keep atomic operations confined to single functions.  Design functions so that functions calling
 * them don't need to know the details of table format, either.
 *
 * These "add one" functions (atomically) add one to the count in question, subtract one from the
 * total move count, and flag the position as 'ready for propagation' (maybe this is just a move
 * count of zero) if the total move count goes to zero.
 */

void add_one_to_white_wins(table, int32 position)
{
}

void add_one_to_black_wins(table, int32 position)
{
}

void add_one_to_white_draws(table, int32 position)
{
}

void add_one_to_black_draws(table, int32 position)
{
}

/***** MOVEMENTS *****/

/* The idea here is to calculate piece movements, and to do it FAST.
 *
 * We build a table of "movements" organized into "directions".  Each direction is just that - the
 * direction that a piece (like a queen) moves.  When we want to check for what movements are
 * possible in a given direction, we run through the direction until we "hit" another pieces - until
 * the bit in the vector matches something already in the position vector.  At the end of the
 * direction, an all-ones vector will "hit" the end of the board and end the direction.  I know,
 * kinda confusing.  It's because it's designed to be fast; we have to do this a lot.
 */

struct movement {
    int64 vector;
    short square;
};

/* seven possible pieces: KQRBNP, plus pawn that can be captured en passant 64 possible squares, up
 * to 8 directions per piece, up to 7 movements in one direction
 */

#define NUM_PIECES 7
#define NUM_SQUARES 64
#define NUM_DIR 8
#define NUM_MOVEMENTS 7

#define KING 0
#define QUEEN 1
#define ROOK 2
#define BISHOP 3
#define KNIGHT 4
#define PAWN 5
#define PAWNep 6

/* we add one to NUM_MOVEMENTS to leave space at the end for the all-ones bitmask that signals the
 * end of the list
 */

struct movement movements[NUM_PIECES][NUM_SQUARES][NUM_DIR][NUM_MOVEMENTS+1];

/* How many different directions can each piece move in?  Knights have 8 directions because they
 * can't be blocked in any of them.
 */

int number_of_movement_directions[7] = {8,8,4,4,8,1,1};
int maximum_movements_in_one_direction[7] = {1,7,7,7,1,2,1};

int64 bitvector[64];
int64 allones_bitvector = 0xffffffff;  /* hehehe */

void init_movements()
{
    int square, piece, dir, mvmt;

    /* XXX This won't work for a 64-bit shift, but this is the idea */

    for (square=0; square < NUM_SQUARES; square++) {
	bitvector = 1 << square;
    }

    for (square=0; square < NUM_SQUARES; square++) {
	for (piece=0; piece < NUM_DIR; piece++) {

	    int current_square = square;
	    int knight_mvmt=0;

	    for (dir=0; dir < number_of_movement_directions[piece]; dir++) {

		for (mvmt=0;
		     mvmt < maximum_movements_in_one_direction[piece];
		     mvmt++) {

#define RIGHT_MOVEMENT_POSSIBLE ((current_square%8)<7)
#define RIGHT2_MOVEMENT_POSSIBLE ((current_square%8)<6)
#define LEFT_MOVEMENT_POSSIBLE ((current_square%8)>0)
#define LEFT_MOVEMENT_POSSIBLE ((current_square%8)>1)
#define UP_MOVEMENT_POSSIBLE (current_square<56)
#define UP2_MOVEMENT_POSSIBLE (current_square<48)
#define DOWN_MOVEMENT_POSSIBLE (current_square>7)
#define DOWN2_MOVEMENT_POSSIBLE (current_square>15)

		    switch (movementdir[piece][dir]) {
		    case RIGHT:
			if (RIGHT_MOVEMENT_POSSIBLE) {
			    current_square++;
			    movements[piece][square][dir][mvmt].square
				= current_square;
			    movements[piece][square][dir][mvmt].vector
				= bitvector[current_square];
			} else {
			    movements[piece][square][dir][mvmt].vector
				= allones_bitvector;
			}
			break;
		    case LEFT:
			if (LEFT_MOVEMENT_POSSIBLE) {
			    current_square--;
			    movements[piece][square][dir][mvmt].square
				= current_square;
			    movements[piece][square][dir][mvmt].vector
				= bitvector[current_square];
			} else {
			    movements[piece][square][dir][mvmt].vector
				= allones_bitvector;
			}
			break;
		    case UP:
			if (UP_MOVEMENT_POSSIBLE) {
			    current_square+=8;
			    movements[piece][square][dir][mvmt].square
				= current_square;
			    movements[piece][square][dir][mvmt].vector
				= bitvector[current_square];
			} else {
			    movements[piece][square][dir][mvmt].vector
				= allones_bitvector;
			}
			break;
		    case DOWN:
			if (DOWN_MOVEMENT_POSSIBLE) {
			    current_square-=8;
			    movements[piece][square][dir][mvmt].square
				= current_square;
			    movements[piece][square][dir][mvmt].vector
				= bitvector[current_square];
			} else {
			    movements[piece][square][dir][mvmt].vector
				= allones_bitvector;
			}
			break;
		    case DIAG_UL:
			if (LEFT_MOVEMENT_POSSIBLE
			    && UP_MOVEMENT_POSSIBLE) {
			    current_square+=8;
			    current_square--;
			    movements[piece][square][dir][mvmt].square
				= current_square;
			    movements[piece][square][dir][mvmt].vector
				= bitvector[current_square];
			} else {
			    movements[piece][square][dir][mvmt].vector
				= allones_bitvector;
			}
			break;
		    case DIAG_UR:
			if (RIGHT_MOVEMENT_POSSIBLE
			    && UP_MOVEMENT_POSSIBLE) {
			    current_square+=8;
			    current_square++;
			    movements[piece][square][dir][mvmt].square
				= current_square;
			    movements[piece][square][dir][mvmt].vector
				= bitvector[current_square];
			} else {
			    movements[piece][square][dir][mvmt].vector
				= allones_bitvector;
			}
			break;
		    case DIAG_DL:
			if (LEFT_MOVEMENT_POSSIBLE
			    && DOWN_MOVEMENT_POSSIBLE) {
			    current_square-=8;
			    current_square--;
			    movements[piece][square][dir][mvmt].square
				= current_square;
			    movements[piece][square][dir][mvmt].vector
				= bitvector[current_square];
			} else {
			    movements[piece][square][dir][mvmt].vector
				= allones_bitvector;
			}
			break;
		    case DIAG_DR:
			if (RIGHT_MOVEMENT_POSSIBLE
			    && DOWN_MOVEMENT_POSSIBLE) {
			    current_square-=8;
			    current_square++;
			    movements[piece][square][dir][mvmt].square
				= current_square;
			    movements[piece][square][dir][mvmt].vector
				= bitvector[current_square];
			} else {
			    movements[piece][square][dir][mvmt].vector
				= allones_bitvector;
			}
			break;
		    case KNIGHT:
			current_square=square;
			switch (mvmt) {
			case 0:
			    if (RIGHT2_MOVEMENT_POSSIBLE
				&& UP_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square
				    = square + 2 + 8;
				movements[piece][square][dir][0].vector
				    = bitvector[square + 2 + 8];
				movements[piece][square][dir][1].vector
				    = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].vector
				    = allones_bitvector;
			    }
			    break;
			case 1:
			    if (RIGHT2_MOVEMENT_POSSIBLE
				&& DOWN_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square
				    = square + 2 - 8;
				movements[piece][square][dir][0].vector
				    = bitvector[square + 2 - 8];
				movements[piece][square][dir][1].vector
				    = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].vector
				    = allones_bitvector;
			    }
			    break;
			case 2:
			    if (LEFT2_MOVEMENT_POSSIBLE
				&& UP_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square
				    = square - 2 + 8;
				movements[piece][square][dir][0].vector
				    = bitvector[square - 2 + 8];
				movements[piece][square][dir][1].vector
				    = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].vector
				    = allones_bitvector;
			    }
			    break;
			case 3:
			    if (LEFT2_MOVEMENT_POSSIBLE
				&& DOWN_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square
				    = square - 2 - 8;
				movements[piece][square][dir][0].vector
				    = bitvector[square - 2 - 8];
				movements[piece][square][dir][1].vector
				    = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].vector
				    = allones_bitvector;
			    }
			    break;
			case 4:
			    if (RIGHT_MOVEMENT_POSSIBLE
				&& UP2_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square
				    = square + 1 + 16;
				movements[piece][square][dir][0].vector
				    = bitvector[square + 1 + 16];
				movements[piece][square][dir][1].vector
				    = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].vector
				    = allones_bitvector;
			    }
			    break;
			case 5:
			    if (RIGHT_MOVEMENT_POSSIBLE
				&& DOWN2_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square
				    = square + 1 - 16;
				movements[piece][square][dir][0].vector
				    = bitvector[square + 1 - 16];
				movements[piece][square][dir][1].vector
				    = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].vector
				    = allones_bitvector;
			    }
			    break;
			case 6:
			    if (LEFT_MOVEMENT_POSSIBLE
				&& UP2_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square
				    = square - 1 + 16;
				movements[piece][square][dir][0].vector
				    = bitvector[square - 1 + 16];
				movements[piece][square][dir][1].vector
				    = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].vector
				    = allones_bitvector;
			    }
			    break;
			case 7:
			    if (LEFT_MOVEMENT_POSSIBLE
				&& DOWN2_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square
				    = square - 1 - 16;
				movements[piece][square][dir][0].vector
				    = bitvector[square - 1 - 16];
				movements[piece][square][dir][1].vector
				    = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].vector
				    = allones_bitvector;
			    }
			    break;
			}
			break;

		    case PAWN:
		    case PAWNep:
			/* Oh, we need to distinguish between forward/backward here as well as white
			 * and black pawns...
			 */
			break;
		    }
		}
	    }
	}
    }
}

#ifdef FUTUREBASES

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

    /* This is where we make pruning decisions, if we don't want to fully analyze out the tree past
     * the table we're now building.  Of course, this will affect the accuracy of the table; the
     * table is a result of BOTH the position it was set up for AND the pruning decisions (and any
     * pruning decisions made on the future tables used to calculate this one).
     *
     * We specify pruning in a simple way - by omitting future tables for moves we don't want to
     * consider.  This can be dangerous, so we require this feature to be specifically enabled for
     * each move by a command-line switch.  Actually, we use two switches, one to calculate a table
     * for OUR SIDE to move, and another if it is the OTHER SIDE to move.
     *
     * So, --prune-our-move e3e4 prunes a pawn move (assuming this is a table with a static pawn on
     * e3) by simply ignoring e3e4 as a possible move.
     *
     * Pruning an opponent's move is more complex because we step a half-move into the future and
     * consider our own next move.  This costs us little, since we can control our own move and
     * therefore don't have to consider all possibilities, and improves a lot.  If future tables
     * exist for any of our responses, they are used.  If no such future tables exist, then the move
     * is regarded as a lost game.
     *
     * So, --prune-his-move e7e8 prunes a pawn promotion (assuming a static pawn on e7) by
     * considering all possible positions resulting after the pawn promotion (to either Q or N) AND
     * the answering move.  The resulting game is regarded as a win for white unless both Q and N
     * promotions have an answer that leads to another table with a win or draw for black.
     *
     * For example, let's say we're looking at a Q-and-P vs. Q-and-P endgame.  There are four mobile
     * pieces (2 Ks and 2 Qs), so we can handle this.  But if one of the pawns queens, then we've
     * got a game with five mobile pieces, and that's too complex.  But we don't want to completely
     * discard all possible enemy promotions, if we can immediately capture the new queen (or the
     * old one).  So we specify something like --prune-his-move e7e8 and pass in a tablebase for a
     * Q-and-P vs. Q endgame.
     *
     * We also check for immediate checkmates or stalemates.
     *
     * Question: do we really need to flag this at all?  Probably yes, because we don't want this
     * pruning to occur by accident.
     *
     * Another reason to flag it is that we want to label in the file header that this pruning was
     * done.  In particular, if we use a pruned tablebase to compute another (earlier) pruned
     * tablebase, we want to make sure the pruning is consistent, i.e. "our" side has to stay the
     * same.  This can only be guaranteed if we explicitly flag which side is which in the file
     * header.
     *
     * Pruning doesn't affect the size of the resulting tablebase.  We discard the extra
     * information.  If the pruned move is actually made in the game, then you have to calculate all
     * possible next moves and check your tablebases for them.  This seems reasonable.
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

#endif

void propagate_move_within_table(tablebase tb, int index)
{

    /* ASSERT (table,index) == WIN/LOSS IN x MOVES or DRAW; */

    /* We want to check to make sure the mate-in number of the position in the database matches a
     * mate-in variable in this routine.  If we're propagating moves from a future table, we might
     * get tables with a whole range of mate-in counts, so we want to make sure we go through them
     * in order.
     */

    parent_position = index_to_position(tb, index);

    /* foreach (mobile piece of player NOT TO PLAY) { */

    for (piece = 0; piece < tb.num_mobiles; piece++) {

	/* We've moving BACKWARDS in the game, so we want the pieces of the player who is NOT TO
	 * PLAY here - this is the LAST move we're considering, not the next move.
	 */

	if (color_of_mobile_piece(piece) == parent_position.side_to_move)
	    continue;

	/* current_position[piece] is a number from 0 to 63 corresponding to the different squares
	 * on the chess board
	 */

	/* possible_moves returns a pointer to an array of possible new position numbers for this
	 * piece.  These are BACKWARDS moves in the game.  The positions returned are legal.
	 *
	 * possible_moves has to look at the entire current position to build this array, because
	 * otherwise it might move a piece "through" another piece.
	 */

	/* possible_moves(current_position[piece], type_of[piece]); */

	for (dir = 0; dir < number_of_movement_directions[type_of[piece]]; dir++) {

	    current_position = parent_position;

	    /* This code IGNORES ALL CAPTURES because that board_vector check stops when we hit
	     * another piece.  Need to check that other piece's color.
	     */

	    for (movementptr = &movements[type_of[piece]][piece position][dir];
		 (movementptr->vector & current_position.board_vector) == 0;
		 movementptr++) {

		current_position.mobile_piece_position[piece]
		    = movementptr->square;

		position_to_index();

		/* Parent position is the FUTURE position */

		/* all of these subroutines have to propagate if changed */

		if (is_white_to_move(parent_position)) {
		    /* ...then this position is BLACK TO MOVE */
		    if (does_white_win(parent_position)) {
			/* parent position is WHITE MOVES AND WINS */
			add_one_to_white_wins();
		    } else if (does_black_win(parent_position)) {
			/* parent position is WHITE MOVES AND BLACK WINS */
			black_wins();
		    } else if (does_white_draw(parent_position)) {
			/* parent position is WHITE MOVES AND DRAWS */
			add_one_to_white_draws();
		    } else if (does_black_draw(parent_position)) {
			/* parent position is WHITE MOVES AND BLACK DRAWS */
			black_draws();
		    }
		} else {
		    /* or this position is WHITE TO MOVE */
		    if (does_black_win(parent_position)) {
			/* parent position is BLACK MOVES AND WINS */
			add_one_to_black_wins();
		    } else if (does_white_win(parent_position)) {
		        /* parent position is BLACK MOVES AND WHITE WINS */
			white_wins();
		    } else if (does_black_draw(parent_position)) {
			/* parent position is BLACK MOVES AND DRAWS */
			add_one_to_black_draws();
		    } else if (does_white_draw(parent_position)) {
			/* parent position is BLACK MOVES AND WHITE DRAWS */
			black_draws();
		    }
		}
	    }
	}

	/* Check for captures */


    }
}

initialize_tablebase(tablebase tb)
{
    /* This is here because we don't want to be calling max_index() everytime through the loop below
     */

    int32 max_index_static = max_index(table);

    for (int32 index=0; index < max_index_static; index++) {

	/* check_legality_of_index() might not work yet */

	if (! check_legality_of_index(tb, index)) {

	    initialize_index_as_illegal(tb, index);

	} else {

	    /* Now we need to count moves.  FORWARD moves. */
	    int movecnt = 0;

	    for (piece = 0; piece < tb.num_mobiles; piece++) {

		for (dir = 0; dir < number_of_movement_directions[type_of[piece]]; dir++) {

		    current_position = parent_position;

		    for (movementptr = &movements[type_of[piece]][piece position][dir];
			 (movementptr->vector & current_position.board_vector) == 0;
			 movementptr++) {

			movecnt ++;
		    }

		    /* Now check to see if the movement ended because we hit against another piece
		     * of the opposite color.  If so, add another move for the capture.
		     *
		     * Actually, we check to see that we DIDN'T hit a piece of our OWN color.  The
		     * difference is that this way we don't register a capture if we hit the end of
		     * the list of movements in a given direction.
		     *
		     * We also check to see if the capture was against the enemy king! in which case
		     * this position is a "mate in 0" (i.e, illegal)
		     *
		     * XXX ASSUMES THAT THE ENEMY KING IS ONE OF THE MOBILE PIECES XXX
		     */

		    if (current_position.side_to_move == WHITE) {
			if ((movementptr->vector & current_position.white_vector) == 0) {
			    movecnt ++;
			    if (movementptr->square ==
				current_position.mobile_piece_position[BLACK_KING]) {
				initialize_index_with_mated(index);
				goto mated;
			    }
			}
		    } else {
			if ((movementptr->vector & current_position.black_vector) == 0) {
			    movecnt ++;
			    if (movementptr->square ==
				current_position.mobile_piece_position[WHITE_KING]) {
				initialize_index_with_mated(index);
				goto mated;
			    }
			}
		    }

		}

#ifdef FROZEN_PIECES
		for (;;) {
		    forall (possible moves of frozen pieces) movecnt++;
		}
#endif
		if (movecnt == 0) initialize_index_with_stalemated(index);
		else initialize_index_with_movecnt(index, movecnt);

	    mated:
				
	    }
	}
    }
}

mainloop()
{
    create_data_structure_from_control_file();

    init_movements();

    initialize_tablebase(tb);

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
