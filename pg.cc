/* -*- mode: C++; fill-column: 100; c-basic-offset: 4; -*-
 *
 * <pawngen pawns-required="NUM" white-queens-required="NUM" black-queens-required="NUM"
 *          white-captures-allowed="NUM" black-captures-allowed="NUM"/>
 *
 * <pawngen white-pawn-locations="STR" white-pawns-required="NUM" white-queens-required="NUM" white-captures-allowed="NUM"
 *          black-pawn-locations="STR" black-pawns-required="NUM" black-queens-required="NUM" black-captures-allowed="NUM"/>
 *
 * add stripe="RANGE" or from="NUM" to="NUM"
 *
 * what about futurebases that might invalidate part of the range?
 *   pawns-required should take care of that
 */

#include <iostream>
#include <cstdint>
#include <queue>
#include <set>

int pawns_required = 6;

class pawn_position {

    friend bool operator< (const pawn_position & LHS, const pawn_position & RHS);

    uint64_t white_pawns = 0;
    uint64_t black_pawns = 0;

    int total_white_pawns = 0;
    int total_black_pawns = 0;

public:

    int white_pawn_captures_black_piece_allowed = 0;
    int black_pawn_captures_white_piece_allowed = 0;

    int white_queens_required = 0;
    int black_queens_required = 0;

    int en_passant_square = 0;

    bool valid(void)
    {
	// return (! white_queens_required && ! black_queens_required && (total_white_pawns + total_black_pawns == pawns_required));
	return (! white_queens_required && ! black_queens_required);
    }

    bool white_pawn_at(int square)
    {
	return (white_pawns & (1ULL << square));
    }

    void remove_white_pawn(int square)
    {
	white_pawns &= ~(1ULL << square);
	total_white_pawns --;
    }

    void add_white_pawn(int square)
    {
	white_pawns |= (1ULL << square);
	total_white_pawns ++;
    }

    bool black_pawn_at(int square)
    {
	return (black_pawns & (1ULL << square));
    }

    void remove_black_pawn(int square)
    {
	black_pawns &= ~(1ULL << square);
	total_black_pawns --;
    }

    void add_black_pawn(int square)
    {
	black_pawns |= (1ULL << square);
	total_black_pawns ++;
    }

    bool pawn_at(int square)
    {
	return white_pawn_at(square) || black_pawn_at(square);
    }

};

/* Comparison operator for pawn position.
 *
 * We want to order pawn positions so that later pawn positions depend only on early ones, and also
 * so that we can easily tell how many board squares are occupied by pawns.
 *
 * We order first by total number of pawns, then by number of white pawns on the 2nd rank, then 3rd,
 * up to 7th, then by number of black pawns on the 7th rank, then 6th, down to 2nd, then by any
 * remaining differences.
 *
 * Any capture will reduce the total number of pawns and thus lead to a smaller index.
 *
 * Any pawn move will reduce the number of pawns on a rank earlier in the sort order, and thus lead
 * to a smaller index.
 */

short pawns_on_rank[256];

bool operator< (const pawn_position & LHS, const pawn_position & RHS)
{
    if ((LHS.total_white_pawns + LHS.total_black_pawns) != (RHS.total_white_pawns + RHS.total_black_pawns)) {
	return ((LHS.total_white_pawns + LHS.total_black_pawns) < (RHS.total_white_pawns + RHS.total_black_pawns));
    } else {
	for (int rank = 2; rank <= 7; rank ++) {
	    auto LHS_white_rank_pawns = pawns_on_rank[(LHS.white_pawns >> 8*(rank-1)) & 0xff];
	    auto RHS_white_rank_pawns = pawns_on_rank[(RHS.white_pawns >> 8*(rank-1)) & 0xff];
	    if (LHS_white_rank_pawns != RHS_white_rank_pawns) {
		return (LHS_white_rank_pawns < RHS_white_rank_pawns);
	    }
	}
	for (int rank = 7; rank >= 2; rank --) {
	    auto LHS_black_rank_pawns = pawns_on_rank[(LHS.black_pawns >> 8*(rank-1)) & 0xff];
	    auto RHS_black_rank_pawns = pawns_on_rank[(RHS.black_pawns >> 8*(rank-1)) & 0xff];
	    if (LHS_black_rank_pawns != RHS_black_rank_pawns) {
		return (LHS_black_rank_pawns < RHS_black_rank_pawns);
	    }
	}
    }

    if (LHS.white_pawns != RHS.white_pawns) {
	return (LHS.white_pawns < RHS.white_pawns);
    }

    if (LHS.black_pawns != RHS.black_pawns) {
	return (LHS.black_pawns < RHS.black_pawns);
    }

    return LHS.en_passant_square < RHS.en_passant_square;
}

class std::set<pawn_position> valid_pawn_positions;
class std::set<pawn_position> invalid_pawn_positions;

/* Back rank is 56-63
 * 7th rank is 48-55
 *
 * 2nd rank is 8-15
 * 1st rank is 0-7
 *
 * Pawns can always be captured by a piece.  They can always move forward and can always capture
 * each other.  They can only queen if required to, and can only capture a piece if allowed to.
 */

void process(class pawn_position position)
{
    std::pair<std::set<pawn_position>::iterator,bool> ret;

    if (position.valid()) {
	ret = valid_pawn_positions.insert(position);
    } else {
	ret = invalid_pawn_positions.insert(position);
    }

    if (! ret.second) return;

    for (int square=0; square < 64; square ++) {

	/* White pawns */

	if (position.white_pawn_at(square)) {
	    pawn_position position2 = position;

	    /* Remove pawn (captured by piece), and reuse position2 with pawn removed */
	    position2.remove_white_pawn(square);
	    position2.en_passant_square = 0;
	    process(position2);

	    /* Queen it if queens are required */
	    if (square >= 48 && position2.white_queens_required) {
		pawn_position position3 = position2;
		position3.white_queens_required --;
		process(position3);
	    }

	    if (square < 48) {
		/* Move forward if unblocked */
		if (! position2.pawn_at(square + 8)) {
		    pawn_position position3 = position2;

		    position3.add_white_pawn(square + 8);
		    process(position3);
		}

		/* Left pawn capture */
		if ((square % 8) != 0) {
		    if (position2.black_pawn_at(square + 8 - 1)) {
			pawn_position position3 = position2;
			position3.remove_black_pawn(square + 8 - 1);
			position3.add_white_pawn(square + 8 - 1);
			process(position3);
		    } else if (position2.white_pawn_captures_black_piece_allowed
			       && !position2.white_pawn_at(square + 8 - 1)) {
			pawn_position position3 = position2;
			position3.add_white_pawn(square + 8 - 1);
			position3.white_pawn_captures_black_piece_allowed --;
			process(position3);
		    }
		}

		/* Right pawn capture */
		if ((square % 8) != 7) {
		    if (position2.black_pawn_at(square + 8 + 1)) {
			pawn_position position3 = position2;
			position3.remove_black_pawn(square + 8 + 1);
			position3.add_white_pawn(square + 8 + 1);
			process(position3);
		    } else if (position2.white_pawn_captures_black_piece_allowed
			       && !position2.white_pawn_at(square + 8 + 1)) {
			pawn_position position3 = position2;
			position3.add_white_pawn(square + 8 + 1);
			position3.white_pawn_captures_black_piece_allowed --;
			process(position3);
		    }
		}
	    }

	    /* En passant movement */
	    if (square < 16) {
		if (! position2.pawn_at(square + 8) && ! position2.pawn_at(square + 16)) {
		    if ((square != 8) && (position2.black_pawn_at(square + 16 - 1))
			|| (square != 15) && (position2.black_pawn_at(square + 16 + 1))) {
			position2.add_white_pawn(square + 16);
			position2.en_passant_square = square + 8;
			process(position2);
		    }
		}
	    }
	}

	/* Ditto for black pawns */

	if (position.black_pawn_at(square)) {
	    pawn_position position2 = position;
	    position2.remove_black_pawn(square);
	    position2.en_passant_square = 0;
	    process(position2);

	    if (square < 16 && position2.black_queens_required) {
		pawn_position position3 = position2;
		position3.black_queens_required --;
		process(position3);
	    }
	    if (square >= 16) {
		if (! position2.pawn_at(square - 8)) {
		    pawn_position position3 = position2;
		    position3.add_black_pawn(square - 8);
		    process(position3);
		}
		if ((square % 8) != 0) {
		    if (position2.white_pawn_at(square - 8 - 1)) {
			pawn_position position3 = position2;
			position3.remove_white_pawn(square - 8 - 1);
			position3.add_black_pawn(square - 8 - 1);
			process(position3);
		    } else if (position2.black_pawn_captures_white_piece_allowed
			       && !position2.black_pawn_at(square - 8 - 1)) {
			pawn_position position3 = position2;
			position3.add_black_pawn(square - 8 - 1);
			position3.black_pawn_captures_white_piece_allowed --;
			process(position3);
		    }
		}
		if ((square % 8) != 7) {
		    if (position.white_pawn_at(square - 8 + 1)) {
			pawn_position position3 = position2;
			position3.remove_white_pawn(square - 8 + 1);
			position3.add_black_pawn(square - 8 + 1);
			process(position3);
		    } else if (position2.black_pawn_captures_white_piece_allowed
			       && !position2.black_pawn_at(square - 8 + 1)) {
			pawn_position position3 = position2;
			position3.add_black_pawn(square - 8 + 1);
			position3.black_pawn_captures_white_piece_allowed --;
			process(position3);
		    }
		}
	    }
	    if (square >= 48) {
		if (! position2.pawn_at(square - 8) && ! position2.pawn_at(square - 16)) {
		    if ((square != 48) && (position2.white_pawn_at(square - 16 - 1))
			|| (square != 55) && (position2.white_pawn_at(square - 16 + 1))) {
			position2.add_black_pawn(square - 16);
			position2.en_passant_square = square - 8;
			process(position2);
		    }
		}
	    }
	}
    }
}

int main(int argc, char * argv [])
{
    for (int i=0; i < 256; i++) {
	int bits = 0;
	for (int bit=0; bit < 7; bit ++) {
	    if (i & (1 << bit)) bits++;
	}
	pawns_on_rank[i] = bits;
    }

    pawn_position initial_position;

    /* White pawn on a2: 7 possible positions
     * White pawn on a7: 2 possible positions: 6 + 1 = 7
     * White pawns on a2 and b2: 6*6 + 6 + 6 + 1 = 36 + 12 + 1 = 49
     * White pawn on a2; black pawn on a7: C(6,2) + 6 + 6 + 1 = 15 + 12 + 1 = 28
     *
     * White pawns on a2, b2; black pawn on a7:
     *    C(6,2) * 7 = 15 * 7 = 105 (both a pawns present)
     *    2*6*7 = 84 (one or the other a pawn present)
     *    7 (no a-pawn)
     *    C(6,2) = 15 (bxa)
     *    5*7 = 35 (axb)
     *    105 + 84 + 7 + 15 + 35 = 246
     * include en-passant:
     *    white pawn b2++, black pawn a4, white pawn a2 or a3 or gone = 3
     *    black pawn a7++, white pawn b5, white pawn a2, a3, a4 or gone = 4
     *    white pawn a2++, black pawn b4 (after capture) = 1
     *    246 + 8 = 254
     */

    switch (5) {
    case 1:
	initial_position.add_white_pawn(8);
	initial_position.add_white_pawn(9);
	initial_position.add_black_pawn(48);
	break;

    case 2:
	/* Carlsen-Anand 2014 Game 7 */
	initial_position.add_white_pawn(9);
	initial_position.add_white_pawn(10);

	initial_position.add_black_pawn(48);
	initial_position.add_black_pawn(50);
	initial_position.add_black_pawn(41);
	initial_position.add_black_pawn(34);

	//initial_position.white_pawn_captures_black_piece_allowed ++;
	//initial_position.black_pawn_captures_white_piece_allowed ++;
	//initial_position.black_pawn_captures_white_piece_allowed ++;

	break;

    case 3:
	/* Fine Problem 68
	 *
	 * White pawns on a2, b2, c2; black pawns on f7, g7, h7: 7^6 = 117649
	 * Same thing with one white queen required: (7^2 + 7*6 + 6^2)(7^3) = 43561
	 *   7^2 = a-file empty, any possibility on b- and c- files (including empty)
	 *   7*6 = a-file occupied, b-file empty, any possibility on c- file (including empty)
	 *   6^2 = c-file empty, a- and b- files occupied
	 */

	initial_position.add_white_pawn(9);
	initial_position.add_white_pawn(10);
	initial_position.add_white_pawn(11);

	initial_position.add_black_pawn(48 + 7);
	initial_position.add_black_pawn(48 + 6);
	initial_position.add_black_pawn(48 + 5);

	initial_position.white_queens_required ++;

	break;

    case 4:
	/* Hollis-Florian */
	initial_position.add_white_pawn(13);
	initial_position.add_white_pawn(22);
	initial_position.add_white_pawn(31);
	initial_position.add_white_pawn(49);

	initial_position.add_black_pawn(39);
	initial_position.add_black_pawn(46);
	initial_position.add_black_pawn(53);

	break;

    case 5:
	/* Barcza-Sanchez */
	initial_position.add_white_pawn(13);
	initial_position.add_white_pawn(22);
	initial_position.add_white_pawn(15);
	initial_position.add_white_pawn(17);

	initial_position.add_black_pawn(48+5);
	initial_position.add_black_pawn(40+6);
	initial_position.add_black_pawn(48+7);
	initial_position.add_black_pawn(35);

	initial_position.white_queens_required ++;

	break;
    }

    process(initial_position);

    std::cout << "Total positions: " << valid_pawn_positions.size() << std::endl;
}
