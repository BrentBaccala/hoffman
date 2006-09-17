/* -*- mode: C; fill-column: 100; eval: (c-set-style "stroustrup"); -*-
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
 * This program will calculate a tablebase for chess pieces (called the 'mobile' pieces) in a static
 * configuration of other 'frozen' pieces.  The mobile pieces could possibly be pawns.  The frozen
 * pieces could possibly be kings.
 *
 * Three piece tablebases with no frozen pieces can also be built.  These are the only tablebases
 * that are completely self contained and don't depend on other tablebases (the 'futurebases').
 *
 * Feed this program a list of futurebases on the command line.
 *
 * Feed this program an XML control file on the command line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>	/* for putting timestamps on the output tablebases */
#include <fcntl.h>	/* for O_RDONLY */
#include <sys/stat.h>	/* for stat'ing the length of the tablebase file */
#include <sys/mman.h>	/* for mmap */

/* The GNU readline library, used for prompting the user during the probe code.  By defining
 * READLINE_LIBRARY, the library is set up to read include files from a directory specified on the
 * compiler's command line, rather than a system-wide /usr/include/readline.  I use it this way
 * simply because I don't have the readline include files installed system-wide on my machine.
 */

#define READLINE_LIBRARY
#include "readline.h"

/* The GNOME XML library.  To use it, I need "-I /usr/include/libxml2" (compiler) and "-lxml2"
 * (linker).
 */

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xmlsave.h>

/* According the GCC documentation, "long long" ints are supported by the C99 standard as well as
 * the GCC compiler.  In any event, since chess boards have 64 squares, being able to use 64 bit
 * integers makes a bunch of stuff a lot easier.  Might have to be careful with this if porting.
 */

typedef unsigned long long int int64;
typedef unsigned int int32;
typedef short boolean;


/***** GLOBAL CONSTANTS *****/

/* Maximum number of mobile pieces; used to simplify various arrays
 *
 * "8" may seem absurd, but it's probably about right.  "4" is easily doable in memory.  "5"
 * requires sweeping passes across a file on disk.  "6" and "7" are worse than "5", but doable with
 * severe restrictions on the movements of the pieces.  So "8" is enough.
 */

#define MAX_MOBILES 8

/* Why 100?  Well, I just think it's less likely to introduce bugs into this code if I count
 * half-moves instead of moves.  So it takes 100 half-moves to stalemate.
 */

#define STALEMATE_COUNT 100

/* seven possible pieces: KQRBNP, plus pawn that can be captured en passant 64 possible squares, up
 * to 8 directions per piece, up to 7 movements in one direction
 */

#define NUM_PIECES 7
#define NUM_SQUARES 64
#define NUM_DIR 8
#define NUM_MOVEMENTS 7


/***** DATA STRUCTURES *****/

/* position - the data structure that represents a board position
 *
 * How about if "position" is a structure containing an 8x8 char array with ASCII characters
 * representing each piece?  No.  Too slow.  Position manipulation is at the core of this program.
 * It has to be fast.
 *
 * So we use a 64-bit vector with one bit for each board position, in addition to a flag to indicate
 * which side is to move and numbers (0-63) indicating the positions of the mobile pieces.  That
 * way, we can easily check if possible moves are legal by looking for pieces that block our moving
 * piece.
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
 * Everytime we call index_to_local_position, copy from the static vector into the position structure.
 * Then we compute the positions of the mobile pieces and plug their bits into the structure's
 * vector at the right places.
 *
 */

/* Where are the kings located in the mobile piece list? */

#define WHITE_KING 0
#define BLACK_KING 1

typedef struct {
    struct tablebase *tb;
    int64 board_vector;
    int64 white_vector;
    int64 black_vector;
    short side_to_move;
    short mobile_piece_position[MAX_MOBILES];
} local_position_t;

/* This is a global position, that doesn't depend on a particular tablebase.  It's slower to
 * manipulate, but is suitable for translating positions between tablebases.  Each char in the array
 * is either 0 or ' ' for an empty square, and one of the FEN characters for a chess piece.  We can
 * use 'e' or 'E' for a pawn than can be captured en passant.
 */

typedef struct {
    unsigned char board[64];
    int64 board_vector;
    short side_to_move;
} global_position_t;


/* bitvector gets initialized in init_movements() */

int64 bitvector[64];
int64 allones_bitvector = 0xffffffffffffffffLL;

/* I'm not sure which one of these will be faster... */

/* #define BITVECTOR(square) bitvector[square] */
#define BITVECTOR(square) (1ULL << (square))

/* tablebase - the data structure used to hold tablebases
 *
 * WHITE and BLACK are also used for the side_to_move variable in the position type above
 */

#define KING 0
#define QUEEN 1
#define ROOK 2
#define BISHOP 3
#define KNIGHT 4
#define PAWN 5
#define PAWNep 6

char * piece_name[NUM_PIECES+1] = {"KING", "QUEEN", "ROOK", "BISHOP", "KNIGHT", "PAWN", "PAWNep", NULL};

char * colors[3] = {"WHITE", "BLACK", NULL};

unsigned char global_pieces[2][NUM_PIECES] = {{'K', 'Q', 'R', 'B', 'N', 'P', 'E'},
					      {'k', 'q', 'r', 'b', 'n', 'p', 'e'}};

#define WHITE 0
#define BLACK 1


/**** TABLEBASE STRUCTURE AND OPERATIONS ****/

/* movecnt - 0 if this entry is ready to propagate; 255 if it has been propagated
 *
 * While movecnt is > 0, it is the number of moves FORWARD from this position that haven't been
 * analyzed yet.  The other three numbers are the number of moves out of this position for which
 * white wins, for which black wins, for which there is some kind of draw.
 *
 * If this position is WHITE TO MOVE, then we don't have to count outcomes which are WHITE WINS,
 * since that makes this position WHITE WINS.  We only have to count outcomes which are BLACK WINS,
 * in order to conclude that, if all possible white moves result in BLACK WINS, then this position
 * is BLACK WINS.  If at least one move leads to a draw (other moves lead to BLACK WINS), then the
 * position is WHITE DRAWS.  If all moves lead to draws, then the position is also BLACK DRAWS.
 * Since we assume that white will make the best move, then we can just say that this position DRAWS
 * unless either there is at least one move which leads to WHITE WINS, or if all moves lead to BLACK
 * WINS.
 *
 * So, all we really need is movecnt.  If we backtrace from a single WHITE WINS, then this position
 * becomes WHITE WINS.  If we backtrace from BLACK WINS, we decrement movecnt.  If movecnt reaches
 * zero, then the position becomes BLACK WINS.  When we're all done backtracing possible wins,
 * anything left with a non-zero movecnt is a DRAW.
 *
 * We also need a mate-in count and a stalemate (conversion) count.
 *
 * To make this work for either white or black positions, let's adopt the notation PTM (Player to
 * move) and PNTM (Player not to move)
 *
 * movecnt
 * 255 - ILLEGAL POSITION
 * 254 - PTM WINS; propagation done
 * 253 - PNTM WINS; propagation done
 * 252 - PTM WINS; propagation needed
 * 0   - PNTM WINS; propagation needed
 *
 * 1 through 251 - movecnt (during run), or DRAW (after run is finished)
 *
 */

#define ILLEGAL_POSITION 255
#define PTM_WINS_PROPAGATION_DONE 254
#define PNTM_WINS_PROPAGATION_DONE 253
#define PTM_WINS_PROPAGATION_NEEDED 252
#define PNTM_WINS_PROPAGATION_NEEDED 0

#define MAX_MOVECNT 251

struct fourbyte_entry {
    unsigned char movecnt;
    unsigned char mate_in_cnt;
    unsigned char stalemate_cnt;
    unsigned char futuremove_cnt;
};

typedef struct tablebase {
    int num_mobiles;
    short mobile_piece_type[MAX_MOBILES];
    short mobile_piece_color[MAX_MOBILES];
    struct fourbyte_entry *entries;
} tablebase;

boolean place_piece_in_local_position(tablebase *tb, local_position_t *pos, int square, int color, int type);

int find_name_in_array(char * name, char * array[])
{
    int i=0;

    while (*array != NULL) {
	if (!strcasecmp(name, *array)) return i;
	array ++;
	i ++;
    }

    return -1;
}

/* Parses an XML control file, creates a tablebase structure corresponding to it, and returns it.
 *
 * Eventually, I want to provide a DTD and validate the XML input, so there's very little error
 * checking here.  The idea is that the validation will ultimately provide the error check.
 */

tablebase * parse_XML_control_file(char *filename)
{
    xmlDocPtr doc;
    xmlNodePtr root_element;
    tablebase *tb;

    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    doc = xmlReadFile(filename, NULL, 0);
    if (doc == NULL) {
	fprintf(stderr, "'%s' failed XML read\n", filename);
	return NULL;
    }

    root_element = xmlDocGetRootElement(doc);

    if (xmlStrcmp(root_element->name, (const xmlChar *) "tablebase")) {
	fprintf(stderr, "'%s' failed XML parse\n", filename);
	return NULL;
    }


    tb = malloc(sizeof(tablebase));
    if (tb == NULL) {
	fprintf(stderr, "Can't malloc tablebase\n");
	return NULL;
    }
    bzero(tb, sizeof(tablebase));

    /* Fetch the mobile pieces from the XML */

    context = xmlXPathNewContext(doc);
    result = xmlXPathEvalExpression((const xmlChar *) "//mobile", context);
    if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
	fprintf(stderr, "'%s': No mobile pieces!\n", filename);
    } else if (result->nodesetval->nodeNr < 2) {
	fprintf(stderr, "'%s': Too few mobile pieces!\n", filename);
    } else if (result->nodesetval->nodeNr > MAX_MOBILES) {
	fprintf(stderr, "'%s': Too many mobile pieces!\n", filename);
    } else {
	int i;

	tb->num_mobiles = result->nodesetval->nodeNr;

	for (i=0; i < result->nodesetval->nodeNr; i++) {
	    xmlChar * color;
	    xmlChar * type;
	    color = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "color");
	    type = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "type");
	    tb->mobile_piece_color[i] = find_name_in_array((char *) color, colors);
	    tb->mobile_piece_type[i] = find_name_in_array((char *) type, piece_name);

	    if ((tb->mobile_piece_color[i] == -1) || (tb->mobile_piece_type[i] == -1)) {
		fprintf(stderr, "Illegal color (%s) or type (%s) in mobile\n", color, type);
		xmlFree(color);
	    }
	}
    }

    if ((tb->mobile_piece_color[WHITE_KING] != WHITE) || (tb->mobile_piece_type[WHITE_KING] != KING)
	|| (tb->mobile_piece_color[BLACK_KING] != BLACK) || (tb->mobile_piece_type[BLACK_KING] != KING)) {
	fprintf(stderr, "Kings aren't where they need to be in mobiles!\n");
    }

    /* The "1" is because side-to-play is part of the position; "6" for the 2^6 squares on the board */

    tb->entries = (struct fourbyte_entry *) calloc(1<<(1+6*tb->num_mobiles), sizeof(struct fourbyte_entry));
    if (tb->entries == NULL) {
	fprintf(stderr, "Can't malloc tablebase entries\n");
    }

    xmlXPathFreeContext(context);

    /* Fetch the futurebases from the XML */

    context = xmlXPathNewContext(doc);
    result = xmlXPathEvalExpression((const xmlChar *) "//futurebase", context);
    if ((tb->num_mobiles > 2) && xmlXPathNodeSetIsEmpty(result->nodesetval)) {
	fprintf(stderr, "'%s': No futurebases!\n", filename);
    } else {
	int i;

	for (i=0; i < result->nodesetval->nodeNr; i++) {
	    xmlChar * filename;
	    xmlChar * md5;
	    filename = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "filename");
	    md5 = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "md5");
	}
    }

    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);

    return tb;
}

tablebase * load_futurebase_from_file(char *filename)
{
    int fd;
    struct stat filestat;
    size_t length;
    char *fileptr;
    int xml_size;
    xmlDocPtr doc;
    tablebase *tb;

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
	fprintf(stderr, "Can not open futurebase '%s'\n", filename);
	return NULL;
    }

    fstat(fd, &filestat);

    fileptr = mmap(0, filestat.st_size, PROT_READ, MAP_SHARED, fd, 0);

    for (xml_size = 0; fileptr[xml_size] != '\0'; xml_size ++) ;

    doc = xmlReadMemory(fileptr, xml_size, NULL, NULL, 0);
}

/* Given a tablebase, create the XML header describing its contents and print it out.
 */

xmlDocPtr create_XML_header(tablebase *tb)
{
    xmlDocPtr doc;
    xmlNodePtr tablebase, pieces, node;
    int piece;
    time_t creation_time;

    doc = xmlNewDoc((const xmlChar *) "1.0");
    tablebase = xmlNewDocNode(doc, NULL, (const xmlChar *) "tablebase", NULL);
    xmlNewProp(tablebase, (const xmlChar *) "offset", (const xmlChar *) "0x1000");
    xmlDocSetRootElement(doc, tablebase);

    pieces = xmlNewChild(tablebase, NULL, (const xmlChar *) "pieces", NULL);

    for (piece = 0; piece < tb->num_mobiles; piece ++) {
	xmlNodePtr mobile;

	mobile = xmlNewChild(pieces, NULL, (const xmlChar *) "mobile", NULL);
	xmlNewProp(mobile, (const xmlChar *) "color",
		   (const xmlChar *) colors[tb->mobile_piece_color[piece]]);
	xmlNewProp(mobile, (const xmlChar *) "type",
		   (const xmlChar *) piece_name[tb->mobile_piece_type[piece]]);
    }

    node = xmlNewChild(tablebase, NULL, (const xmlChar *) "generating-program", NULL);
    xmlNewProp(node, (const xmlChar *) "name", (const xmlChar *) "Hoffman");
    xmlNewProp(node, (const xmlChar *) "version", (const xmlChar *) "$Revision: 1.33 $");

    node = xmlNewChild(tablebase, NULL, (const xmlChar *) "generating-time", NULL);
    time(&creation_time);
    xmlNewProp(node, (const xmlChar *) "time", (const xmlChar *) ctime(&creation_time));

    /* xmlSaveFile("-", doc); */
    return doc;
}

int do_write(int fd, void *ptr, int length)
{
    while (length > 0) {
	int writ = write(fd, ptr, length);
	if (writ == -1) return -1;
	ptr += writ;
	length -= writ;
    }
    return 0;
}

void write_tablebase_to_file(tablebase *tb, char *filename)
{
    xmlDocPtr doc;
    xmlSaveCtxtPtr savectx;
    int fd;
    void *writeptr;

#if 0
    doc = create_XML_header(tb);
    xmlSaveFile(filename, doc);
    xmlFreeDoc(doc);
#endif

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
	fprintf(stderr, "Can't open '%s' for writing tablebase\n", filename);
	return;
    }

    doc = create_XML_header(tb);
    savectx = xmlSaveToFd(fd, NULL, 0);
    xmlSaveDoc(savectx, doc);
    xmlSaveClose(savectx);
    xmlFreeDoc(doc);

    if (lseek(fd, 0x1000, SEEK_SET) != 0x1000) {
	fprintf(stderr, "seek failed\n");
    }

    do_write(fd, tb->entries, sizeof(struct fourbyte_entry) << (1 + 6*tb->num_mobiles));

    close(fd);
}

/* Simple initialization for a K vs K endgame. */

tablebase * create_2piece_tablebase(void)
{
    tablebase *tb;

    tb = (tablebase *) malloc(sizeof(tablebase));
    if (tb == NULL) {
	fprintf(stderr, "Can't malloc tablebase header\n");
    }

    /* The "2" is because side-to-play is part of the position */

    tb->entries = (struct fourbyte_entry *) calloc(2*64*64, sizeof(struct fourbyte_entry));
    if (tb->entries == NULL) {
	fprintf(stderr, "Can't malloc tablebase entries\n");
    }

    /* Remember above, I defined WHITE_KING as 0 and BLACK_KING as 1, so we have to make sure
     * that these two pieces are where we want them in this list.  Other than that, it's
     * pretty flexible.
     */

    tb->num_mobiles = 2;
    tb->mobile_piece_type[0] = KING;
    tb->mobile_piece_type[1] = KING;
    tb->mobile_piece_color[0] = WHITE;
    tb->mobile_piece_color[1] = BLACK;

    return tb;
}

/* Simple initialization for a K+Q vs K endgame. */

tablebase * create_KRK_tablebase(void)
{
    tablebase *tb;

    tb = (tablebase *) malloc(sizeof(tablebase));
    if (tb == NULL) {
	fprintf(stderr, "Can't malloc tablebase header\n");
    }

    /* The "2" is because side-to-play is part of the position */

    tb->entries = (struct fourbyte_entry *) calloc(2*64*64*64, sizeof(struct fourbyte_entry));
    if (tb->entries == NULL) {
	fprintf(stderr, "Can't malloc tablebase entries\n");
    }

    /* Remember above, I defined WHITE_KING as 0 and BLACK_KING as 1, so we have to make sure
     * that these two pieces are where we want them in this list.  Other than that, it's
     * pretty flexible.
     */

    tb->num_mobiles = 3;
    tb->mobile_piece_type[0] = KING;
    tb->mobile_piece_type[1] = KING;
    tb->mobile_piece_type[2] = ROOK;
    tb->mobile_piece_color[0] = WHITE;
    tb->mobile_piece_color[1] = BLACK;
    tb->mobile_piece_color[2] = WHITE;

    return tb;
}

tablebase * create_KQK_tablebase(void)
{
    tablebase *tb;

    tb = (tablebase *) malloc(sizeof(tablebase));
    if (tb == NULL) {
	fprintf(stderr, "Can't malloc tablebase header\n");
    }

    /* The "2" is because side-to-play is part of the position */

    tb->entries = (struct fourbyte_entry *) calloc(2*64*64*64, sizeof(struct fourbyte_entry));
    if (tb->entries == NULL) {
	fprintf(stderr, "Can't malloc tablebase entries\n");
    }

    /* Remember above, I defined WHITE_KING as 0 and BLACK_KING as 1, so we have to make sure
     * that these two pieces are where we want them in this list.  Other than that, it's
     * pretty flexible.
     */

    tb->num_mobiles = 3;
    tb->mobile_piece_type[0] = KING;
    tb->mobile_piece_type[1] = KING;
    tb->mobile_piece_type[2] = QUEEN;
    tb->mobile_piece_color[0] = WHITE;
    tb->mobile_piece_color[1] = BLACK;
    tb->mobile_piece_color[2] = WHITE;

    return tb;
}

tablebase * create_KQKR_tablebase(void)
{
    tablebase *tb;

    tb = (tablebase *) malloc(sizeof(tablebase));
    if (tb == NULL) {
	fprintf(stderr, "Can't malloc tablebase header\n");
    }

    /* The "2" is because side-to-play is part of the position */

    tb->entries = (struct fourbyte_entry *) calloc(2*64*64*64*64, sizeof(struct fourbyte_entry));
    if (tb->entries == NULL) {
	fprintf(stderr, "Can't malloc tablebase entries\n");
    }

    /* Remember above, I defined WHITE_KING as 0 and BLACK_KING as 1, so we have to make sure
     * that these two pieces are where we want them in this list.  Other than that, it's
     * pretty flexible.
     */

    tb->num_mobiles = 4;
    tb->mobile_piece_type[0] = KING;
    tb->mobile_piece_type[1] = KING;
    tb->mobile_piece_type[2] = QUEEN;
    tb->mobile_piece_type[3] = ROOK;
    tb->mobile_piece_color[0] = WHITE;
    tb->mobile_piece_color[1] = BLACK;
    tb->mobile_piece_color[2] = WHITE;
    tb->mobile_piece_color[3] = BLACK;

    return tb;
}

inline int square(int row, int col)
{
    return (col + row*8);
}

int32 max_index(tablebase *tb)
{
    return (2<<(6*tb->num_mobiles)) - 1;
}

int32 local_position_to_index(tablebase *tb, local_position_t *pos)
{
    /* This function, given a board position, returns an index into the tablebase.
     *
     * The reason we pass the tablebase in explicitly is that we will need to use this function to
     * calculate not only indices into our own table, but also into future tables with different
     * static configs.  Actually, I'm not sure about this.  Maybe it's only the matching function
     * index_to_local_position() that we need for future tables.  In any event, we'll need this function
     * to probe tables when we want to actually use them.
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

    /* Keep it simple, for now */

    int shift_count = 1;
    int32 index = pos->side_to_move;  /* WHITE is 0; BLACK is 1 */
    int piece;

    for (piece = 0; piece < tb->num_mobiles; piece ++) {
	if (pos->mobile_piece_position[piece] < 0)
	    fprintf(stderr, "Bad mobile piece position in local_position_to_index()\n");
	index |= pos->mobile_piece_position[piece] << shift_count;
	shift_count += 6;  /* because 2^6=64 */
    }

    return index;
}

int32 global_position_to_index(tablebase *tb, global_position_t *position)
{
    int32 index = position->side_to_move;  /* WHITE is 0; BLACK is 1 */
    int piece;
    int square;
    short pieces_processed_bitvector = 0;

    for (square = 0; square < NUM_SQUARES; square ++) {
	if ((position->board[square] != 0) && (position->board[square] != ' ')) {
	    for (piece = 0; piece < tb->num_mobiles; piece ++) {
		if ((position->board[square]
		     == global_pieces[tb->mobile_piece_color[piece]][tb->mobile_piece_type[piece]])
		    && !(pieces_processed_bitvector & (1 << piece))) {
		    index |= square << (1 + 6*piece);
		    pieces_processed_bitvector |= (1 << piece);
		    break;
		}
	    }
	    /* If we didn't find a suitable matching piece... */
	    if (piece == tb->num_mobiles) return -1;
	}
    }


    /* Make sure all the pieces have been accounted for */

    for (piece = 0; piece < tb->num_mobiles; piece ++) {
	if (!(pieces_processed_bitvector & (1 << piece)))
	    return -1;
    }

    return index;
}

/* OK, maybe not.  Maybe need to check index numbers, too. (Unless all positions in the table are
 * legal!)
 *
 * It's starting to look like we won't need this function, because the only time we want to check
 * the legality of an index is when we want to convert it to a position right after that,
 * so both functions get wrapped together into index_to_local_position().
 */

boolean check_legality_of_index(tablebase *config, int32 index)
{
}

/* any reason to do this?  just for one mobile? */
int index_to_mobile_position(tablebase *config, int32 index, int piece)
{}

boolean index_to_local_position(tablebase *tb, int32 index, local_position_t *p)
{
    /* Given an index, fill in a board position.  Obviously has to correspond to local_position_to_index()
     * and it's a big bug if it doesn't.  The boolean that gets returned is TRUE if the operation
     * succeeded (the index is at least minimally valid) and FALSE if the index is so blatantly
     * illegal (two piece on the same square) that we can't even fill in the position.
     */

    int piece;

    bzero(p, sizeof(local_position_t));

    p->side_to_move = index & 1;
    index >>= 1;

    for (piece = 0; piece < tb->num_mobiles; piece++) {
	p->mobile_piece_position[piece] = index & 63;
	if (p->board_vector & BITVECTOR(index & 63)) {
	    return 0;
	}
	p->board_vector |= BITVECTOR(index & 63);
	if (tb->mobile_piece_color[piece] == WHITE) {
	    p->white_vector |= BITVECTOR(index & 63);
	} else {
	    p->black_vector |= BITVECTOR(index & 63);
	}
	index >>= 6;
    }
    return 1;
}

boolean index_to_global_position(tablebase *tb, int32 index, global_position_t *position)
{
    int piece;

    bzero(position, sizeof(global_position_t));

    position->side_to_move = index & 1;
    index >>= 1;

    for (piece = 0; piece < tb->num_mobiles; piece++) {

	if (position->board[index & 63] != 0) {
	    return 0;
	}

	position->board[index & 63]
	    = global_pieces[tb->mobile_piece_color[piece]][tb->mobile_piece_type[piece]];

	position->board_vector |= BITVECTOR(index & 63);

	index >>= 6;
    }

    return 1;
}

/* invert_colors_of_global_position - just what its name implies
 *
 * We use this when propagating from a futurebase built for the opposite colors, say a K+R vs K
 * endgame that we now want to propagate into a game where the rook is black, not white.
 *
 * Actually, this only works (in its present form) if no pawns are present.  If there are pawns in
 * the game, this function will also have to reflect the board around a horizontal centerline,
 * right?
 */

void invert_colors_of_global_position(global_position_t *global)
{
    int square;

    for (square=0; square < NUM_SQUARES; square++) {
	if ((global->board[square] >= 'A') && (global->board[square] <= 'Z')) {
	    global->board[square] += 'a' - 'A';
	} else if ((global->board[square] >= 'a') && (global->board[square] <= 'z')) {
	    global->board[square] += 'A' - 'a';
	}
    }
    if (global->side_to_move == WHITE)
	global->side_to_move = BLACK;
    else
	global->side_to_move = WHITE;
}

boolean global_position_to_local_position(tablebase *tb, global_position_t *global, local_position_t *local)
{
    int piece;
    int square;
    short pieces_processed_bitvector = 0;

    bzero(local, sizeof(local_position_t));

    for (piece = 0; piece < tb->num_mobiles; piece ++)
	local->mobile_piece_position[piece] = -1;

    local->side_to_move = global->side_to_move;

    for (square = 0; square < NUM_SQUARES; square ++) {
	if ((global->board[square] != 0) && (global->board[square] != ' ')) {
	    for (piece = 0; piece < tb->num_mobiles; piece ++) {
		if ((global->board[square]
		     == global_pieces[tb->mobile_piece_color[piece]][tb->mobile_piece_type[piece]])
		    && !(pieces_processed_bitvector & (1 << piece))) {

		    local->mobile_piece_position[piece] = square;
		    local->board_vector |= BITVECTOR(square);
		    if (tb->mobile_piece_color[piece] == WHITE)
			local->white_vector |= BITVECTOR(square);
		    else
			local->black_vector |= BITVECTOR(square);
		    pieces_processed_bitvector |= (1 << piece);
		    break;
		}
	    }
	    /* If we didn't find a suitable matching piece... */
	    if (piece == tb->num_mobiles) return 0;
	}
    }


    /* Right now, it seems that we actually don't want to do this.  When we back-propagate
     * from a futurebase, one piece in the current tablebase will be missing from the
     * futurebase (since it was captured), so we leave its value at -1.  Obviously,
     * this will have to be corrected by the calling routine before this local position
     * can be converted to an index.
     */

#if 0
    /* Make sure all the pieces have been accounted for */

    for (piece = 0; piece < tb->num_mobiles; piece ++) {
	if (!(pieces_processed_bitvector & (1 << piece)))
	    return 0;
    }
#endif

    return 1;
}


/* "Designed to multi-thread"
 *
 * Keep atomic operations confined to single functions.  Design functions so that functions calling
 * them don't need to know the details of table format, either.
 *
 * These "add one" functions (atomically) add one to the count in question, subtract one from the
 * total move count, and flag the position as 'ready for propagation' (maybe this is just a move
 * count of zero) if the total move count goes to zero.
 *
 * PTM = Player to Move
 * PNTM = Player not to Move
 *
 */

#define WHITE_TO_MOVE(index) (((index)&1)==WHITE)
#define BLACK_TO_MOVE(index) (((index)&1)==BLACK)

inline short does_PTM_win(tablebase *tb, int32 index)
{
    return (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_NEEDED)
	|| (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_DONE);
}

inline short does_PNTM_win(tablebase *tb, int32 index)
{
    return (tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_NEEDED)
	|| (tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_DONE);
}

inline short does_white_win(tablebase *tb, int32 index)
{
    if (WHITE_TO_MOVE(index))
	return does_PTM_win(tb, index);
    else
	return does_PNTM_win(tb,index);
}

inline short does_black_win(tablebase *tb, int32 index)
{
    if (BLACK_TO_MOVE(index))
	return does_PTM_win(tb, index);
    else
	return does_PNTM_win(tb,index);
}

inline boolean needs_propagation(tablebase *tb, int32 index)
{
    return (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_NEEDED)
	|| (tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_NEEDED);
}

inline boolean is_position_valid(tablebase *tb, int32 index)
{
    return (! (does_PTM_win(tb, index) && (tb->entries[index].mate_in_cnt == 0)));
}

inline void mark_propagated(tablebase *tb, int32 index)
{
    if (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_NEEDED) {
	tb->entries[index].movecnt = PTM_WINS_PROPAGATION_DONE;
    } else if (tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_NEEDED) {
	tb->entries[index].movecnt = PNTM_WINS_PROPAGATION_DONE;
    } else {
	fprintf(stderr, "Propagation attempt on a completed or unresolved position\n");
    }
}

/* get_mate_in_count() is also used as basically (does_white_win() || does_black_win()), so it has
 * to return -1 if there is no mate from this position
 */

inline int get_mate_in_count(tablebase *tb, int32 index)
{
    if (tb->entries[index].movecnt >= 1 && tb->entries[index].movecnt <= MAX_MOVECNT) {
	return -1;
    } else {
	return tb->entries[index].mate_in_cnt;
    }
}

inline int get_stalemate_count(tablebase *tb, int32 index)
{
    return tb->entries[index].stalemate_cnt;
}

/* DEBUG_MOVE can be used to print more verbose debugging information about what the program is
 * doing to process a single move.
 */

/* #define DEBUG_MOVE 42923 */

/* Five possible ways we can initialize an index for a position:
 *  - it's illegal
 *  - white's mated (black is to move)
 *  - black's mated (white is to move)
 *  - stalemate
 *  - any other position, with 'movecnt' possible moves out the position
 */

void initialize_index_as_illegal(tablebase *tb, int32 index)
{
    tb->entries[index].movecnt = ILLEGAL_POSITION;
    tb->entries[index].mate_in_cnt = 255;
    tb->entries[index].stalemate_cnt = 255;
    tb->entries[index].futuremove_cnt = 0;
}

void initialize_index_with_white_mated(tablebase *tb, int32 index)
{

#ifdef DEBUG_MOVE
    if (index == DEBUG_MOVE) printf("initialize_index_with_white_mated; index=%d\n", index);
#endif

    if (WHITE_TO_MOVE(index)) {
	fprintf(stderr, "initialize_index_with_white_mated in a white to move position!\n");
    }
    tb->entries[index].movecnt = PTM_WINS_PROPAGATION_NEEDED;
    tb->entries[index].mate_in_cnt = 0;
    tb->entries[index].stalemate_cnt = 0;
    tb->entries[index].futuremove_cnt = 0;
}

void initialize_index_with_black_mated(tablebase *tb, int32 index)
{

#ifdef DEBUG_MOVE
    if (index == DEBUG_MOVE) printf("initialize_index_with_black_mated; index=%d\n", index);
#endif

    if (BLACK_TO_MOVE(index)) {
	fprintf(stderr, "initialize_index_with_black_mated in a black to move position!\n");
    }
    tb->entries[index].movecnt = PTM_WINS_PROPAGATION_NEEDED;
    tb->entries[index].mate_in_cnt = 0;
    tb->entries[index].stalemate_cnt = 0;
    tb->entries[index].futuremove_cnt = 0;
}

void initialize_index_with_stalemate(tablebase *tb, int32 index)
{

#ifdef DEBUG_MOVE
    if (index == DEBUG_MOVE) printf("initialize_index_with_stalemate; index=%d\n", index);
#endif

    tb->entries[index].movecnt = 251; /* use this as stalemate for now */
    tb->entries[index].mate_in_cnt = 255;
    tb->entries[index].stalemate_cnt = 0;
    tb->entries[index].futuremove_cnt = 0;
}

void initialize_index_with_movecnt(tablebase *tb, int32 index, int movecnt, int futuremove_cnt)
{

#ifdef DEBUG_MOVE
    if (index == DEBUG_MOVE) printf("initialize_index_with_movecnt; index=%d movecnt=%d\n", index, movecnt);
#endif

    tb->entries[index].movecnt = movecnt;
    tb->entries[index].mate_in_cnt = 255;
    tb->entries[index].stalemate_cnt = 255;
    tb->entries[index].futuremove_cnt = futuremove_cnt;
}

inline void PTM_wins(tablebase *tb, int32 index, int mate_in_count, int stalemate_count)
{

#ifdef DEBUG_MOVE
    if (index == DEBUG_MOVE)
	printf("PTM_wins; index=%d; movecnt=%d; old mate_in=%d, mate_in=%d\n",
	       index, tb->entries[index].movecnt, tb->entries[index].mate_in_cnt, mate_in_count);
#endif

    if (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_DONE) {
	if (mate_in_count < tb->entries[index].mate_in_cnt) {
	    fprintf(stderr, "Mate in count dropped in PTM_wins after propagation done!?\n");
	}
    } else if (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_NEEDED) {
	if (mate_in_count < tb->entries[index].mate_in_cnt) {
	    /* This can happen if we're propagating in from a futurebase, since the propagation runs
	     * through the futurebase in index order, not mate-in order.
	     */
	    /* fprintf(stderr, "Mate in count dropped in PTM_wins!?\n"); */
	    tb->entries[index].mate_in_cnt = mate_in_count;
	    tb->entries[index].stalemate_cnt = stalemate_count;
	}
    } else if ((tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_NEEDED)
	       || (tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_DONE)) {
	fprintf(stderr, "PTM_wins in a position where PNTM already won?!\n");
    } else {
	tb->entries[index].movecnt = PTM_WINS_PROPAGATION_NEEDED;
	tb->entries[index].mate_in_cnt = mate_in_count;
	tb->entries[index].stalemate_cnt = stalemate_count;
    }
}

inline void add_one_to_PNTM_wins(tablebase *tb, int32 index, int mate_in_count, int stalemate_count)
{

#ifdef DEBUG_MOVE
    if (index == DEBUG_MOVE)
	printf("add_one_to_PNTM_wins; index=%d; movecnt=%d; old mate_in=%d, mate_in=%d\n",
	       index, tb->entries[index].movecnt, tb->entries[index].mate_in_cnt, mate_in_count);
#endif

    if ((tb->entries[index].movecnt == PTM_WINS_PROPAGATION_NEEDED) ||
	(tb->entries[index].movecnt == PTM_WINS_PROPAGATION_DONE)) {
	/* This is OK.  PTM already found a way to win.  Do nothing. */
    } else if ((tb->entries[index].movecnt == 0) || (tb->entries[index].movecnt > MAX_MOVECNT)) {
	fprintf(stderr, "add_one_to_PNTM_wins in an already won position!?\n");
    } else {
	/* since PNTM_WIN_PROPAGATION_NEEDED is 0, this decrements right into the special flag,
	 * no extra check needed here
	 */
	tb->entries[index].movecnt --;
	if (mate_in_count < tb->entries[index].mate_in_cnt) {
	    if (tb->entries[index].mate_in_cnt != 255) {
		/* As above, this can happen during a futurebase back propagation */
		/* fprintf(stderr, "mate-in count dropped in add_one_to_PNTM_wins?\n"); */
		tb->entries[index].mate_in_cnt = mate_in_count;
		tb->entries[index].stalemate_cnt = stalemate_count;
	    }
	}
	tb->entries[index].mate_in_cnt = mate_in_count;

	if ((tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_NEEDED)
	    && (tb->entries[index].mate_in_cnt == 1)) {
	    /* In this case, the only moves at PTM's disposal move him into check (mate_in_cnt is
	     * now one, so it would drop to zero on next move).  So we need to distinguish here
	     * between being in check (it's checkmate) and not being in check (stalemate).  The
	     * simplest way to do this is to flip the side-to-move flag and look at the position
	     * with the other side to move.  If the king can be taken, then that other position
	     * will be PTM_WINS (of either flavor) with a zero mate_in_cnt.
	     */
	    /* XXX assumes that flipping lowest bit in index flips side-to-move flag */
	    if (does_PTM_win(tb, index^1) && (tb->entries[index^1].mate_in_cnt == 0)) {
	    } else {
		initialize_index_with_stalemate(tb, index);
	    }
	}

	/* XXX not sure about this stalemate code */
	if (stalemate_count < tb->entries[index].stalemate_cnt) {
	    tb->entries[index].stalemate_cnt = stalemate_count;
	}
    }
}


/***** MOVEMENT VECTORS *****/

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

/* we add one to NUM_MOVEMENTS to leave space at the end for the all-ones bitmask that signals the
 * end of the list
 */

struct movement movements[NUM_PIECES][NUM_SQUARES][NUM_DIR][NUM_MOVEMENTS+1];

/* How many different directions can each piece move in?  Knights have 8 directions because they
 * can't be blocked in any of them.
 */

int number_of_movement_directions[7] = {8,8,4,4,8,1,1};
int maximum_movements_in_one_direction[7] = {1,7,7,7,1,2,1};

enum {RIGHT, LEFT, UP, DOWN, DIAG_UL, DIAG_UR, DIAG_DL, DIAG_DR, KNIGHTmove, PAWNmove, PAWN2move}
movementdir[7][8] = {
    {RIGHT, LEFT, UP, DOWN, DIAG_UL, DIAG_UR, DIAG_DL, DIAG_DR},	/* King */
    {RIGHT, LEFT, UP, DOWN, DIAG_UL, DIAG_UR, DIAG_DL, DIAG_DR},	/* Queen */
    {RIGHT, LEFT, UP, DOWN},						/* Rook */
    {DIAG_UL, DIAG_UR, DIAG_DL, DIAG_DR},				/* Bishop */
    {KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove},	/* Knights are special... */
    {PAWNmove, PAWN2move},						/* Pawns need more work */
    {PAWNmove},								/* en passant pawns */
};



char algebraic_notation[64][3];

void init_movements()
{
    int square, piece, dir, mvmt;

    for (square=0; square < NUM_SQUARES; square++) {
	bitvector[square] = 1ULL << square;
	algebraic_notation[square][0] = 'a' + square%8;
	algebraic_notation[square][1] = '1' + square/8;
	algebraic_notation[square][2] = '\0';
    }

    for (piece=0; piece < NUM_PIECES; piece++) {
	for (square=0; square < NUM_SQUARES; square++) {

	    for (dir=0; dir < number_of_movement_directions[piece]; dir++) {

		int current_square = square;

		for (mvmt=0; mvmt < maximum_movements_in_one_direction[piece]; mvmt ++) {

#define RIGHT_MOVEMENT_POSSIBLE ((current_square%8)<7)
#define RIGHT2_MOVEMENT_POSSIBLE ((current_square%8)<6)
#define LEFT_MOVEMENT_POSSIBLE ((current_square%8)>0)
#define LEFT2_MOVEMENT_POSSIBLE ((current_square%8)>1)
#define UP_MOVEMENT_POSSIBLE (current_square<56)
#define UP2_MOVEMENT_POSSIBLE (current_square<48)
#define DOWN_MOVEMENT_POSSIBLE (current_square>7)
#define DOWN2_MOVEMENT_POSSIBLE (current_square>15)

		    switch (movementdir[piece][dir]) {
		    case RIGHT:
			if (RIGHT_MOVEMENT_POSSIBLE) {
			    current_square++;
			    movements[piece][square][dir][mvmt].square = current_square;
			    movements[piece][square][dir][mvmt].vector = BITVECTOR(current_square);
			} else {
			    movements[piece][square][dir][mvmt].square = -1;
			    movements[piece][square][dir][mvmt].vector = allones_bitvector;
			}
			break;
		    case LEFT:
			if (LEFT_MOVEMENT_POSSIBLE) {
			    current_square--;
			    movements[piece][square][dir][mvmt].square = current_square;
			    movements[piece][square][dir][mvmt].vector = BITVECTOR(current_square);
			} else {
			    movements[piece][square][dir][mvmt].square = -1;
			    movements[piece][square][dir][mvmt].vector = allones_bitvector;
			}
			break;
		    case UP:
			if (UP_MOVEMENT_POSSIBLE) {
			    current_square+=8;
			    movements[piece][square][dir][mvmt].square = current_square;
			    movements[piece][square][dir][mvmt].vector = BITVECTOR(current_square);
			} else {
			    movements[piece][square][dir][mvmt].square = -1;
			    movements[piece][square][dir][mvmt].vector = allones_bitvector;
			}
			break;
		    case DOWN:
			if (DOWN_MOVEMENT_POSSIBLE) {
			    current_square-=8;
			    movements[piece][square][dir][mvmt].square = current_square;
			    movements[piece][square][dir][mvmt].vector = BITVECTOR(current_square);
			} else {
			    movements[piece][square][dir][mvmt].square = -1;
			    movements[piece][square][dir][mvmt].vector = allones_bitvector;
			}
			break;
		    case DIAG_UL:
			if (LEFT_MOVEMENT_POSSIBLE && UP_MOVEMENT_POSSIBLE) {
			    current_square+=8;
			    current_square--;
			    movements[piece][square][dir][mvmt].square = current_square;
			    movements[piece][square][dir][mvmt].vector = BITVECTOR(current_square);
			} else {
			    movements[piece][square][dir][mvmt].square = -1;
			    movements[piece][square][dir][mvmt].vector = allones_bitvector;
			}
			break;
		    case DIAG_UR:
			if (RIGHT_MOVEMENT_POSSIBLE && UP_MOVEMENT_POSSIBLE) {
			    current_square+=8;
			    current_square++;
			    movements[piece][square][dir][mvmt].square = current_square;
			    movements[piece][square][dir][mvmt].vector = BITVECTOR(current_square);
			} else {
			    movements[piece][square][dir][mvmt].square = -1;
			    movements[piece][square][dir][mvmt].vector = allones_bitvector;
			}
			break;
		    case DIAG_DL:
			if (LEFT_MOVEMENT_POSSIBLE && DOWN_MOVEMENT_POSSIBLE) {
			    current_square-=8;
			    current_square--;
			    movements[piece][square][dir][mvmt].square = current_square;
			    movements[piece][square][dir][mvmt].vector = BITVECTOR(current_square);
			} else {
			    movements[piece][square][dir][mvmt].square = -1;
			    movements[piece][square][dir][mvmt].vector = allones_bitvector;
			}
			break;
		    case DIAG_DR:
			if (RIGHT_MOVEMENT_POSSIBLE && DOWN_MOVEMENT_POSSIBLE) {
			    current_square-=8;
			    current_square++;
			    movements[piece][square][dir][mvmt].square = current_square;
			    movements[piece][square][dir][mvmt].vector = BITVECTOR(current_square);
			} else {
			    movements[piece][square][dir][mvmt].square = -1;
			    movements[piece][square][dir][mvmt].vector = allones_bitvector;
			}
			break;
		    case KNIGHTmove:
			current_square=square;
			switch (dir) {
			case 0:
			    if (RIGHT2_MOVEMENT_POSSIBLE && UP_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square = square + 2 + 8;
				movements[piece][square][dir][0].vector = BITVECTOR(square + 2 + 8);
				movements[piece][square][dir][1].square = -1;
				movements[piece][square][dir][1].vector = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].square = -1;
				movements[piece][square][dir][0].vector = allones_bitvector;
			    }
			    break;
			case 1:
			    if (RIGHT2_MOVEMENT_POSSIBLE && DOWN_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square = square + 2 - 8;
				movements[piece][square][dir][0].vector = BITVECTOR(square + 2 - 8);
				movements[piece][square][dir][1].square = -1;
				movements[piece][square][dir][1].vector = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].square = -1;
				movements[piece][square][dir][0].vector = allones_bitvector;
			    }
			    break;
			case 2:
			    if (LEFT2_MOVEMENT_POSSIBLE && UP_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square = square - 2 + 8;
				movements[piece][square][dir][0].vector = BITVECTOR(square - 2 + 8);
				movements[piece][square][dir][1].square = -1;
				movements[piece][square][dir][1].vector = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].square = -1;
				movements[piece][square][dir][0].vector = allones_bitvector;
			    }
			    break;
			case 3:
			    if (LEFT2_MOVEMENT_POSSIBLE && DOWN_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square = square - 2 - 8;
				movements[piece][square][dir][0].vector = BITVECTOR(square - 2 - 8);
				movements[piece][square][dir][1].square = -1;
				movements[piece][square][dir][1].vector = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].square = -1;
				movements[piece][square][dir][0].vector = allones_bitvector;
			    }
			    break;
			case 4:
			    if (RIGHT_MOVEMENT_POSSIBLE && UP2_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square = square + 1 + 16;
				movements[piece][square][dir][0].vector = BITVECTOR(square + 1 + 16);
				movements[piece][square][dir][1].square = -1;
				movements[piece][square][dir][1].vector = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].square = -1;
				movements[piece][square][dir][0].vector = allones_bitvector;
			    }
			    break;
			case 5:
			    if (RIGHT_MOVEMENT_POSSIBLE && DOWN2_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square = square + 1 - 16;
				movements[piece][square][dir][0].vector = BITVECTOR(square + 1 - 16);
				movements[piece][square][dir][1].square = -1;
				movements[piece][square][dir][1].vector = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].square = -1;
				movements[piece][square][dir][0].vector = allones_bitvector;
			    }
			    break;
			case 6:
			    if (LEFT_MOVEMENT_POSSIBLE && UP2_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square = square - 1 + 16;
				movements[piece][square][dir][0].vector = BITVECTOR(square - 1 + 16);
				movements[piece][square][dir][1].square = -1;
				movements[piece][square][dir][1].vector = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].square = -1;
				movements[piece][square][dir][0].vector = allones_bitvector;
			    }
			    break;
			case 7:
			    if (LEFT_MOVEMENT_POSSIBLE && DOWN2_MOVEMENT_POSSIBLE) {
				movements[piece][square][dir][0].square = square - 1 - 16;
				movements[piece][square][dir][0].vector = BITVECTOR(square - 1 - 16);
				movements[piece][square][dir][1].square = -1;
				movements[piece][square][dir][1].vector = allones_bitvector;
			    } else {
				movements[piece][square][dir][0].square = -1;
				movements[piece][square][dir][0].vector = allones_bitvector;
			    }
			    break;
			}
			break;

		    case PAWNmove:
		    case PAWN2move:
			/* Oh, we need to distinguish between forward/backward here as well as white
			 * and black pawns...
			 */
			break;
		    }
		}

		/* Always put an allones_bitvector at the end of the movement vector
		 * to make sure we stop!
		 */

		movements[piece][square][dir][mvmt].square = -1;
		movements[piece][square][dir][mvmt].vector = allones_bitvector;

	    }
	}
    }
}

/* I don't plan to call this routine every time the program runs, but it has to be used after any
 * changes to the code above to verify that those complex movement vectors are correct, or at least
 * consistent.  We're using this in a game situation.  We can't afford bugs in this code.
 */

void verify_movements()
{
    int piece;
    int squareA, squareB;
    int dir;
    struct movement * movementptr;

    /* For everything except pawns, if it can move from A to B, then it better be able to move from
     * B to A...
     */

    for (piece=KING; piece <= KNIGHT; piece ++) {

	for (squareA=0; squareA < NUM_SQUARES; squareA ++) {

	    for (squareB=0; squareB < NUM_SQUARES; squareB ++) {

		int movement_possible = 0;
		int reverse_movement_possible = 0;

		if (squareA == squareB) {
		    for (dir = 0; dir < number_of_movement_directions[piece]; dir++) {
			for (movementptr = movements[piece][squareA][dir];
			     (movementptr->vector & BITVECTOR(squareB)) == 0;
			     movementptr++) ;
			if ((movementptr->square != -1) || (movementptr->vector != allones_bitvector)) {
			    fprintf(stderr, "Self movement possible!? %s %d %d\n",
				    piece_name[piece], squareA, movementptr->square);
			}
		    }
		    continue;
		}

		for (dir = 0; dir < number_of_movement_directions[piece]; dir++) {

		    for (movementptr = movements[piece][squareA][dir];
			 (movementptr->vector & BITVECTOR(squareB)) == 0;
			 movementptr++) {
			if ((movementptr->square < 0) || (movementptr->square >= NUM_SQUARES)) {
			    fprintf(stderr, "Bad movement square: %s %d %d %d\n",
				    piece_name[piece], squareA, squareB, movementptr->square);
			}
		    }

		    if (movementptr->square == -1) {
			if (movementptr->vector != allones_bitvector) {
			    fprintf(stderr, "-1 movement lacks allones_bitvector: %s %d %d\n",
				    piece_name[piece], squareA, squareB);
			}
		    } else if ((movementptr->square < 0) || (movementptr->square >= NUM_SQUARES)) {
			fprintf(stderr, "Bad movement square: %s %d %d\n",
				piece_name[piece], squareA, squareB);
		    } else {
			if (movementptr->square != squareB) {
			    fprintf(stderr, "bitvector does not match destination square: %s %d %d\n",
				    piece_name[piece], squareA, squareB);
			}
			if (movement_possible) {
			    fprintf(stderr, "multiple idential destinations from same origin: %s %d %d\n",
				    piece_name[piece], squareA, squareB);
			}
			movement_possible = 1;
			if (movementptr->vector == allones_bitvector) {
			    fprintf(stderr, "allones_bitvector on a legal movement: %s %d %d\n",
				    piece_name[piece], squareA, squareB);
			}
		    }
		}


		for (dir = 0; dir < number_of_movement_directions[piece]; dir++) {

		    for (movementptr = movements[piece][squareB][dir];
			 (movementptr->vector & BITVECTOR(squareA)) == 0;
			 movementptr++) ;

		    if (movementptr->square != -1) reverse_movement_possible=1;
		}


		if (movement_possible && !reverse_movement_possible) {
		    fprintf(stderr, "reverse movement impossible: %s %d %d\n",
			    piece_name[piece], squareA, squareB);
		}

	    }
	}
    }
}


/***** FUTUREBASES *****/

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

/* Propagate moves from a futurebase that resulted from capturing one of the mobile
 * pieces in the current tablebase.
 *
 * We use global positions here, even though they're slower than local positions, because we're
 * translating between two different tablebases.  The cleanest (but not necessarily fastest) way to
 * do this is with global positions.
 *
 * I'm thinking of changing that "invert_colors_of_futurebase" flag to be a subroutine that gets
 * passed in.  It could be a pointer to invert_colors_of_global_position to do what it does now.  Or
 * it could be a "reflect board around vertical axis" to move a d4 pawn to e4.  Also see my comments
 * on invert_colors_of_global position.
 */

void propagate_moves_from_mobile_capture_futurebase(tablebase *tb, tablebase *futurebase,
						    int invert_colors_of_futurebase, int captured_piece)
{
    int32 future_index;
    int32 max_future_index_static = max_index(futurebase);
    int32 current_index;
    global_position_t future_position;
    local_position_t current_position;
    int piece;
    int dir;
    struct movement *movementptr;

    for (future_index = 0; future_index < max_future_index_static; future_index ++) {

	/* It's tempting to break out the loop here if the position isn't a win, but if we want to
	 * track futuremoves in order to make sure we don't miss one (probably a good idea), then
	 * the simplest way to do that is to run this loop even for draws.
	 */

	if (index_to_global_position(futurebase, future_index, &future_position)) {

	    if (invert_colors_of_futurebase)
		invert_colors_of_global_position(&future_position);

	    /* Since the position resulted from a capture, we only want to consider future positions
	     * where the side to move is not the side that captured.
	     */

	    if (future_position.side_to_move != tb->mobile_piece_color[captured_piece])
		continue;

	    for (piece = 0; piece < tb->num_mobiles; piece++) {

		/* Now this is the piece that actually did the capturing.  We only want to consider
		 * pieces of the side which captured...
		 */

		if (tb->mobile_piece_color[piece] == tb->mobile_piece_color[captured_piece])
		    continue;

		/* Take the global position from the futurebase and translate it into a local
		 * position for the current tablebase.  There should be one piece missing
		 * from the local position - the piece that was captured.
		 *
		 * Probably could move this code outside of the for loop, and just copy the local
		 * position.  But we don't have that many mobile pieces, so it's probably not a huge
		 * performance hit anyway.
		 */

		global_position_to_local_position(tb, &future_position, &current_position);

		if (future_position.side_to_move == WHITE)
		    current_position.side_to_move = BLACK;
		else
		    current_position.side_to_move = WHITE;


		if (current_position.mobile_piece_position[captured_piece] != -1)
		    fprintf(stderr, "Captured piece position specified too soon during back-prop\n");

		/* Place the captured piece back into the position on the square from which
		 * the moving piece started (i.e, ended) its move.
		 *
		 * Probably should use place_piece_in_local_position here, but we don't.  The board
		 * vectors don't get updated, but that doesn't matter since we're never going to
		 * look at the square the captured piece is on as a possible origin square for the
		 * capturing piece.
		 */

		current_position.mobile_piece_position[captured_piece]
		    = current_position.mobile_piece_position[piece];

		/* We consider all possible backwards movements of the piece which captured. */

		for (dir = 0; dir < number_of_movement_directions[tb->mobile_piece_type[piece]]; dir++) {

		    /* Make sure we start each movement of the capturing piece from the capture square */

		    current_position.mobile_piece_position[piece]
			= current_position.mobile_piece_position[captured_piece];

		    for (movementptr = movements[tb->mobile_piece_type[piece]][current_position.mobile_piece_position[piece]][dir];
			 (movementptr->vector & future_position.board_vector) == 0;
			 movementptr++) {

			/* Move the capturing piece... */

			current_position.mobile_piece_position[piece] = movementptr->square;

			/* Look up the position in the current tablebase... */

			current_index = local_position_to_index(tb, &current_position);

			if (current_index == -1) {
			    fprintf(stderr, "Can't lookup position in futurebase propagation!\n");
			}

			/* Skip everything else if the position isn't valid.  In particular,
			 * we don't track futuremove propagation for illegal positions.
			 */

			if (!is_position_valid(tb, current_index)) continue;

			/* ...note that we've handled one of the futuremoves out of this position... */

			tb->entries[current_index].futuremove_cnt --;

			/* ...and propagate the win */

			/* XXX might want to look more closely at the stalemate options here */

			if (does_PTM_win(futurebase, future_index)) {

			    add_one_to_PNTM_wins(tb, current_index,
						 get_mate_in_count(futurebase, future_index)+1, 0);

			} else if (does_PNTM_win(futurebase, future_index)) {

			    PTM_wins(tb, current_index,
				     get_mate_in_count(futurebase, future_index)+1, 0);

			}

		    }
		}
	    }

	}
    }
}

boolean have_all_futuremoves_been_handled(tablebase *tb) {

    int32 max_index_static = max_index(tb);
    int32 index;

    for (index = 0; index < max_index_static; index ++) {
	if (tb->entries[index].futuremove_cnt != 0)
	    return 0;
    }

    return 1;
}


/***** INTRA-TABLE MOVE PROPAGATION *****/

/* This is the guts of the program here.  We've got a move that needs to be propagated,
 * so we back out one half-move to all of the positions that could have gotten us
 * here and update their counters in various obscure ways.
 */

void propagate_move_within_table(tablebase *tb, int32 parent_index, int mate_in_count)
{
    local_position_t parent_position;
    local_position_t current_position; /* i.e, last position that moved to parent_position */
    int piece;
    int dir;
    struct movement *movementptr;
    int32 current_index;

    /* We want to check to make sure the mate-in number of the position in the database matches a
     * mate-in variable in this routine.  If we're propagating moves from a future table, we might
     * get tables with a whole range of mate-in counts, so we want to make sure we go through them
     * in order.
     */

    if (get_mate_in_count(tb, parent_index) != mate_in_count) {
	fprintf(stderr, "Mate-in counts don't match: %d %d\n",
		mate_in_count, get_mate_in_count(tb, parent_index));
    }

    if (!does_white_win(tb, parent_index) && !does_black_win(tb, parent_index)) {
	fprintf(stderr, "Propagating position %d where neither side wins?!\n", parent_index);
    }

    mark_propagated(tb, parent_index);

    index_to_local_position(tb, parent_index, &parent_position);

    /* foreach (mobile piece of player NOT TO PLAY) { */

    for (piece = 0; piece < tb->num_mobiles; piece++) {

	/* We've moving BACKWARDS in the game, so we want the pieces of the player who is NOT TO
	 * PLAY here - this is the LAST move we're considering, not the next move.
	 */

	if (tb->mobile_piece_color[piece] == parent_position.side_to_move)
	    continue;

	/* forall possible_moves(current_position, piece) { */

	for (dir = 0; dir < number_of_movement_directions[tb->mobile_piece_type[piece]]; dir++) {

	    /* What about captures?  Well, first of all, there are no captures here!  We're moving
	     * BACKWARDS in the game... and pieces don't appear out of thin air.  Captures are
	     * handled by back-propagation from futurebases, not here in the movement code.  The
	     * piece moving had to come from somewhere, and that somewhere will now be an empty
	     * square, so once we've hit another piece along a movement vector, there's absolutely
	     * no need to consider anything further.
	     */

	    for (movementptr
		     = movements[tb->mobile_piece_type[piece]][parent_position.mobile_piece_position[piece]][dir];
		 (movementptr->vector & parent_position.board_vector) == 0;
		 movementptr++) {

		current_position = parent_position;

		if (parent_position.side_to_move == WHITE)
		    current_position.side_to_move = BLACK;
		else
		    current_position.side_to_move = WHITE;

		/* This code makes perfect sense... but I doubt it will be needed!  The
		 * local_position_to_index function will probably only require the square numbers, not the
		 * board vectors.
		 */
#if NEEDED
		current_position.board_vector &= ~BITVECTOR(parent_position.mobile_piece_position[piece]);
		current_position.board_vector |= BITVECTOR(movementptr->square);
		if (tb->mobile_piece_color[piece] == WHITE) {
		    current_position.white_vector &= ~BITVECTOR(parent_position.mobile_piece_position[piece]);
		    current_position.white_vector |= BITVECTOR(movementptr->square);
		} else {
		    current_position.black_vector &= ~BITVECTOR(parent_position.mobile_piece_position[piece]);
		    current_position.black_vector |= BITVECTOR(movementptr->square);
		}
#endif

		current_position.mobile_piece_position[piece] = movementptr->square;

		current_index = local_position_to_index(tb, &current_position);

		/* Parent position is the FUTURE position.  We now back-propagate to
		 * the current position, which is the PAST position.
		 *
		 * If the player to move in the FUTURE position wins, then we add one to that
		 * player's win count in the PAST position.  On other other hand, if the player not
		 * to move in the FUTURE position wins, then the player to move in the PAST position
		 * has a winning move (the one we're considering).
		 *
		 * These stalemate and mate counts increment by one every HALF MOVE.
		 */

		if (does_PTM_win(tb, parent_index)) {

		    if (get_stalemate_count(tb, parent_index) < STALEMATE_COUNT) {
			add_one_to_PNTM_wins(tb, current_index,
					     get_mate_in_count(tb, parent_index)+1,
					     get_stalemate_count(tb, parent_index)+1);
		    }

		} else if (does_PNTM_win(tb, parent_index)) {

		    if (get_stalemate_count(tb, parent_index) < STALEMATE_COUNT) {
			PTM_wins(tb, current_index,
				 get_mate_in_count(tb, parent_index)+1,
				 get_stalemate_count(tb, parent_index)+1);
		    }

		}

	    }
	}

    }
}

/* initialize_tablebase()
 *
 * This is another critical function; don't be deceived by the tame word 'initialize'.
 *
 * We determine that a position is won for the player not to move (PNTM) if all possible moves (of
 * the player to move) lead to a won game for PNTM.  We count down this total during back
 * propagation, so it stands to reason that we need an accurate count to start with.  Thus the
 * importance of this function.
 *
 * Basically, there are two types of moves we need to consider in each position:
 *
 * 1. non-capture moves of the mobile pieces
 *
 * Normally, we just add these up and then count them down during intra-table propagation.  The only
 * extra issue that arises is the possibility of move restrictions.  If the "good guys" are PTM,
 * then we can just ignore any possible moves outside the restriction - we don't count them up here,
 * and we don't count them down later.  If the "bad guys" are PTM, then any possible move outside
 * the restriction immediately results in this routine flagging the position won for PTM...  unless
 * we want to step forward another half move.  (How?)
 *
 * 2. non-capture moves of the frozen pieces and capture moves
 *
 * These always lead to a different tablebase (a futurebase).  The only way we handle them is
 * through inter-table back propagation.  We keep a seperate count of these moves (the
 * "futuremoves"), because, unlike non-capture moves of mobile pieces, we might miss some of these
 * moves if we don't have a complete set of futurebases.  So we count futuremoves by themselves (as
 * well as part of the standard count), and count them down normally during a single sweep through
 * our futurebases.  If that takes care of everything fine.  Otherwise, during our first pass
 * through the current tablebase, we'll find that some of the futuremove remain unaccounted for.  If
 * they occur with the "good guys" as PTM, we just double-check that the restriction is OK, subtract
 * the remaining futuremoves out from the standard count, and keep going.  But if the "bad guys" are
 * PTM, then some more work is needed.  The position is marked won for PTM, unless we want to step
 * forward another half move.  In this case, we compute all possible next moves (or maybe just
 * captures), and search for them in our tablebases.  If any of them are marked drawn or won, we can
 * safely back-propagate this.  Otherwise, the position has to be marked won for PTM, as before.
 *
 * There's a real serious speed penalty here, because this half-move-forward algorithm requires
 * random access lookups in the futurebases.  A possible way to address this would be to create an
 * intermediate tablebase for the half move following the capture.
 *
 */

initialize_tablebase(tablebase *tb)
{
    local_position_t parent_position;
    local_position_t current_position;
    int32 index;
    int piece;
    int dir;
    struct movement *movementptr;

    /* This is here because we don't want to be calling max_index() everytime through the loop below */

    int32 max_index_static = max_index(tb);

    for (index=0; index < max_index_static; index++) {

	if (! index_to_local_position(tb, index, &parent_position)) {

	    initialize_index_as_illegal(tb, index);

	} else {

	    /* Now we need to count moves.  FORWARD moves. */
	    int movecnt = 0;
	    int futuremove_cnt = 0;

	    for (piece = 0; piece < tb->num_mobiles; piece++) {

		/* We only want to consider pieces of the side which is to move... */

		if (tb->mobile_piece_color[piece] != parent_position.side_to_move)
		    continue;

		for (dir = 0; dir < number_of_movement_directions[tb->mobile_piece_type[piece]]; dir++) {

		    current_position = parent_position;

		    for (movementptr = movements[tb->mobile_piece_type[piece]][parent_position.mobile_piece_position[piece]][dir];
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
			    futuremove_cnt ++;
			    if (movementptr->square ==
				current_position.mobile_piece_position[BLACK_KING]) {
				initialize_index_with_black_mated(tb, index);
				goto mated;
			    }
			}
		    } else {
			if ((movementptr->vector & current_position.black_vector) == 0) {
			    movecnt ++;
			    futuremove_cnt ++;
			    if (movementptr->square ==
				current_position.mobile_piece_position[WHITE_KING]) {
				initialize_index_with_white_mated(tb, index);
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
	    }

	    if (movecnt == 0) initialize_index_with_stalemate(tb, index);
	    else initialize_index_with_movecnt(tb, index, movecnt, futuremove_cnt);

	mated: ;
				
	}
    }
}

void propagate_all_moves_within_tablebase(tablebase *tb)
{
    int max_moves_to_win;
    int moves_to_win;
    int progress_made;
    int32 max_index_static;
    int32 index;

    /* create_data_structure_from_control_file(); */

#ifdef FUTUREBASE
    max_moves_to_win = propagate_moves_from_futurebases();
#else
    max_moves_to_win = 1;
#endif

    /* First we look for forced mates... */

    moves_to_win = 0;
    progress_made = 1;
    max_index_static = max_index(tb);

    while (progress_made || moves_to_win < max_moves_to_win) {
	progress_made = 0;
	for (index=0; index < max_index_static; index++) {
	    if (needs_propagation(tb, index) && get_mate_in_count(tb, index) == moves_to_win) {
#if 0
		if (!progress_made)
		    fprintf(stderr, "Pass %d starts with %d\n", moves_to_win, index);
#endif
		propagate_move_within_table(tb, index, moves_to_win);
		progress_made ++;
	    }
	}
	fprintf(stderr, "Pass %d complete; %d positions processed\n", moves_to_win, progress_made);
	moves_to_win ++;
    }

    /* Everything else allows both sides to draw with best play. */

    /* flag_everything_else_drawn_by_repetition(); */

    /* write_output_tablebase(); */

}


/***** PROBING NALIMOV TABLEBASES *****/

int EGTBProbe(int wtm, int WhiteKingSQ, int BlackKingSQ, int WhiteQueenSQ, int WhiteRookSQ, int BlackRookSQ,
	      int *score);

int IInitializeTb(char *pszPath);

int FTbSetCacheSize(void    *pv, unsigned long   cbSize );

#define EGTB_CACHE_DEFAULT (1024*1024)

void *EGTB_cache;

void init_nalimov_code(void)
{
    int nalimov_num;

    nalimov_num = IInitializeTb(".");
    printf("%d piece Nalimov tablebases found\n", nalimov_num);
    EGTB_cache = malloc(EGTB_CACHE_DEFAULT);
    if (EGTB_cache == NULL) {
	fprintf(stderr, "Can't malloc EGTB cache\n");
    } else {
	FTbSetCacheSize(EGTB_cache, EGTB_CACHE_DEFAULT);
    }
}

/* Note that the buffer in this function is static... */

char * global_position_to_FEN(global_position_t *position)
{
    static char buffer[256];
    char *ptr = buffer;
    int empty_squares;
    int row, col;

    for (row=7; row>=0; row--) {
	empty_squares=0;
	for (col=0; col<=7; col++) {
	    if ((position->board[square(row, col)] == ' ') || (position->board[square(row,col)] == 0)) {
		empty_squares++;
	    } else {
		if (empty_squares > 0) {
		    *(ptr++) = '0' + empty_squares;
		    empty_squares = 0;
		}
		*(ptr++) = position->board[square(row,col)];
	    }
	}
	if (empty_squares > 0) {
	    *(ptr++) = '0' + empty_squares;
	}
	if (row > 0) *(ptr++) = '/';
    }

    *(ptr++) = ' ';

    *(ptr++) = (position->side_to_move == WHITE) ? 'w' : 'b';

    *(ptr++) = '\0';

    return buffer;
}

void verify_KRK_tablebase_against_nalimov(tablebase *tb)
{
    int32 index;
    int32 max_index_static = max_index(tb);
    local_position_t pos;
    global_position_t global;
    int score;

    for (index = 0; index < max_index_static; index++) {
	if (index_to_local_position(tb, index, &pos)) {
	    if (! is_position_valid(tb, index)) {

		/* I've learned the hard way not to probe a Nalimov tablebase for an illegal position... */

	    } else if (EGTBProbe(pos.side_to_move == WHITE,
			  pos.mobile_piece_position[WHITE_KING],
			  pos.mobile_piece_position[BLACK_KING],
			  -1, pos.mobile_piece_position[2], -1, &score) == 1) {
		if (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_DONE) {
		    /* Make sure mate_in_cnt is greater than zero here, since the Nalimov tablebase
		     * doesn't appear to handle illegal positions.  PTM wins in 0 would mean that
		     * PNTM is in check, so the king can just be captured.
		     */
		    if (tb->entries[index].mate_in_cnt > 0) {
			if ((tb->entries[index].mate_in_cnt/2) != ((65536-4)/2)-score+1) {
			    index_to_global_position(tb, index, &global);
			    printf("%s (%d): Nalimov says %d (mate in %d), but we say mate in %d\n",
				   global_position_to_FEN(&global), index,
				   score, ((65536-4)/2)-score+1, tb->entries[index].mate_in_cnt/2);
			}
		    }
		} else if (tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_DONE) {
		    if ((tb->entries[index].mate_in_cnt/2) != ((65536-4)/2)+score) {
			index_to_global_position(tb, index, &global);
			printf("%s (%d): Nalimov says %d (%d), but we say mated in %d\n",
			       global_position_to_FEN(&global), index,
			       score, ((65536-4)/2)+score, tb->entries[index].mate_in_cnt/2);
		    }
		} else {
		    if (score != 0) {
			index_to_global_position(tb, index, &global);
			printf("%s (%d): Nalimov says %d (%d), but we say draw\n",
			       global_position_to_FEN(&global), index,
			       score, ((65536-4)/2)+score);
		    }
		}
	    } else {
		if (((tb->entries[index].movecnt != PTM_WINS_PROPAGATION_DONE)
		     && (tb->entries[index].movecnt != PTM_WINS_PROPAGATION_DONE))
		    || tb->entries[index].mate_in_cnt != 0) {
		    index_to_global_position(tb, index, &global);
		    fprintf(stderr, "%s (%d): Nalimov says illegal, but we say %d %d\n",
			    global_position_to_FEN(&global), index,
			    tb->entries[index].movecnt, tb->entries[index].mate_in_cnt);
		} else {
		    /* printf("Illegal OK\n"); */
		}
	    }
	}
    }
}

void verify_KQKR_tablebase_against_nalimov(tablebase *tb)
{
    int32 index;
    int32 max_index_static = max_index(tb);
    local_position_t pos;
    global_position_t global;
    int score;

    for (index = 0; index < max_index_static; index++) {
	if (index_to_local_position(tb, index, &pos)) {
	    if (! is_position_valid(tb, index)) {

		/* I've learned the hard way not to probe a Nalimov tablebase for an illegal position... */

	    } else if (EGTBProbe(pos.side_to_move == WHITE,
			  pos.mobile_piece_position[WHITE_KING],
			  pos.mobile_piece_position[BLACK_KING],
			  pos.mobile_piece_position[2], -1, pos.mobile_piece_position[3], &score) == 1) {
		if (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_DONE) {
		    /* Make sure mate_in_cnt is greater than zero here, since the Nalimov tablebase
		     * doesn't appear to handle illegal positions.  PTM wins in 0 would mean that
		     * PNTM is in check, so the king can just be captured.
		     */
		    if (tb->entries[index].mate_in_cnt > 0) {
			if ((tb->entries[index].mate_in_cnt/2) != ((65536-4)/2)-score+1) {
			    index_to_global_position(tb, index, &global);
			    printf("%s (%d): Nalimov says %d (mate in %d), but we say mate in %d\n",
				   global_position_to_FEN(&global), index,
				   score, ((65536-4)/2)-score+1, tb->entries[index].mate_in_cnt/2);
			}
		    }
		} else if (tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_DONE) {
		    if ((tb->entries[index].mate_in_cnt/2) != ((65536-4)/2)+score) {
			index_to_global_position(tb, index, &global);
			printf("%s (%d): Nalimov says %d (%d), but we say mated in %d\n",
			       global_position_to_FEN(&global), index,
			       score, ((65536-4)/2)+score, tb->entries[index].mate_in_cnt/2);
		    }
		} else {
		    if (score != 0) {
			index_to_global_position(tb, index, &global);
			printf("%s (%d): Nalimov says %d (%d), but we say draw\n",
			       global_position_to_FEN(&global), index,
			       score, ((65536-4)/2)+score);
		    }
		}
	    } else {
		if (((tb->entries[index].movecnt != PTM_WINS_PROPAGATION_DONE)
		     && (tb->entries[index].movecnt != PTM_WINS_PROPAGATION_DONE))
		    || tb->entries[index].mate_in_cnt != 0) {
		    index_to_global_position(tb, index, &global);
		    fprintf(stderr, "%s (%d): Nalimov says illegal, but we say %d %d\n",
			    global_position_to_FEN(&global), index,
			    tb->entries[index].movecnt, tb->entries[index].mate_in_cnt);
		} else {
		    /* printf("Illegal OK\n"); */
		}
	    }
	}
    }
}

/***** PARSING FEN INTO POSITION STRUCTURES *****/

boolean place_piece_in_local_position(tablebase *tb, local_position_t *pos, int square, int color, int type)
{
    int piece;

    if (pos->board_vector & BITVECTOR(square)) return 0;

    for (piece = 0; piece < tb->num_mobiles; piece ++) {
	if ((tb->mobile_piece_type[piece] == type) && (tb->mobile_piece_color[piece] == color)) {
	    pos->mobile_piece_position[piece] = square;
	    pos->board_vector |= BITVECTOR(square);
	    if (color == WHITE) pos->white_vector |= BITVECTOR(square);
	    else pos->black_vector |= BITVECTOR(square);
	    return 1;
	}
    }

    return 0;
}

boolean place_piece_in_global_position(global_position_t *position, int square, int color, int type)
{
    position->board[square] = global_pieces[color][type];
    return 1;
}

boolean parse_FEN_to_local_position(char *FEN_string, tablebase *tb, local_position_t *pos)
{
    int row, col;

    bzero(pos, sizeof(local_position_t));

    for (row=7; row>=0; row--) {
	for (col=0; col<=7; col++) {
	    switch (*FEN_string) {
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
		/* subtract one here since the 'for' loop will bump col by one */
		col += *FEN_string - '0' - 1;
		if (col > 7) return 0;
		break;

	    case 'k':
		if (!place_piece_in_local_position(tb, pos, square(row, col), BLACK, KING)) return 0;
		break;
	    case 'K':
		if (!place_piece_in_local_position(tb, pos, square(row, col), WHITE, KING)) return 0;
		break;

	    case 'q':
		if (!place_piece_in_local_position(tb, pos, square(row, col), BLACK, QUEEN)) return 0;
		break;
	    case 'Q':
		if (!place_piece_in_local_position(tb, pos, square(row, col), WHITE, QUEEN)) return 0;
		break;

	    case 'r':
		if (!place_piece_in_local_position(tb, pos, square(row, col), BLACK, ROOK)) return 0;
		break;
	    case 'R':
		if (!place_piece_in_local_position(tb, pos, square(row, col), WHITE, ROOK)) return 0;
		break;

	    case 'b':
		if (!place_piece_in_local_position(tb, pos, square(row, col), BLACK, BISHOP)) return 0;
		break;
	    case 'B':
		if (!place_piece_in_local_position(tb, pos, square(row, col), WHITE, BISHOP)) return 0;
		break;

	    case 'n':
		if (!place_piece_in_local_position(tb, pos, square(row, col), BLACK, KNIGHT)) return 0;
		break;
	    case 'N':
		if (!place_piece_in_local_position(tb, pos, square(row, col), WHITE, KNIGHT)) return 0;
		break;

	    case 'p':
		if (!place_piece_in_local_position(tb, pos, square(row, col), BLACK, PAWN)) return 0;
		break;
	    case 'P':
		if (!place_piece_in_local_position(tb, pos, square(row, col), WHITE, PAWN)) return 0;
		break;
	    }
	    FEN_string++;
	}
	if (row > 0) {
	  if (*FEN_string != '/') return 0;
	  else FEN_string++;
	}
    }

    if (*FEN_string != ' ') return 0;
    while (*FEN_string == ' ') FEN_string ++;

    if (*FEN_string == 'w') {
      pos->side_to_move = WHITE;
    } else if (*FEN_string == 'b') {
      pos->side_to_move = BLACK;
    } else {
      return 0;
    }

    return 1;
}

boolean parse_FEN_to_global_position(char *FEN_string, global_position_t *pos)
{
    int row, col;

    bzero(pos, sizeof(global_position_t));

    for (row=7; row>=0; row--) {
	for (col=0; col<=7; col++) {
	    switch (*FEN_string) {
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
		/* subtract one here since the 'for' loop will bump col by one */
		col += *FEN_string - '0' - 1;
		if (col > 7) return 0;
		break;

	    case 'k':
		if (!place_piece_in_global_position(pos, square(row, col), BLACK, KING)) return 0;
		break;
	    case 'K':
		if (!place_piece_in_global_position(pos, square(row, col), WHITE, KING)) return 0;
		break;

	    case 'q':
		if (!place_piece_in_global_position(pos, square(row, col), BLACK, QUEEN)) return 0;
		break;
	    case 'Q':
		if (!place_piece_in_global_position(pos, square(row, col), WHITE, QUEEN)) return 0;
		break;

	    case 'r':
		if (!place_piece_in_global_position(pos, square(row, col), BLACK, ROOK)) return 0;
		break;
	    case 'R':
		if (!place_piece_in_global_position(pos, square(row, col), WHITE, ROOK)) return 0;
		break;

	    case 'b':
		if (!place_piece_in_global_position(pos, square(row, col), BLACK, BISHOP)) return 0;
		break;
	    case 'B':
		if (!place_piece_in_global_position(pos, square(row, col), WHITE, BISHOP)) return 0;
		break;

	    case 'n':
		if (!place_piece_in_global_position(pos, square(row, col), BLACK, KNIGHT)) return 0;
		break;
	    case 'N':
		if (!place_piece_in_global_position(pos, square(row, col), WHITE, KNIGHT)) return 0;
		break;

	    case 'p':
		if (!place_piece_in_global_position(pos, square(row, col), BLACK, PAWN)) return 0;
		break;
	    case 'P':
		if (!place_piece_in_global_position(pos, square(row, col), WHITE, PAWN)) return 0;
		break;
	    }
	    FEN_string++;
	}
	if (row > 0) {
	  if (*FEN_string != '/') return 0;
	  else FEN_string++;
	}
    }

    if (*FEN_string != ' ') return 0;
    while (*FEN_string == ' ') FEN_string ++;

    if (*FEN_string == 'w') {
      pos->side_to_move = WHITE;
    } else if (*FEN_string == 'b') {
      pos->side_to_move = BLACK;
    } else {
      return 0;
    }

    return 1;
}

/* This routine looks at "movestr" to try and figure out if it is a valid move from this global
 * position.  If so, it changes the global position to reflect the move and returns true.
 * Otherwise, it leaves the global position alone and returns false.
 */

boolean parse_move_in_global_position(char *movestr, global_position_t *global)
{
    int origin_square, destination_square;
    int is_capture = 0;

    if (movestr[0] >= 'a' && movestr[0] <= 'h' && movestr[1] >= '1' && movestr[1] <= '8') {
	origin_square = movestr[0]-'a' + (movestr[1]-'1')*8;
	movestr += 2;
    } else {
	return 0;
    }

    if (movestr[0] == 'x') {
	is_capture = 1;
	movestr ++;
    }

    if (movestr[0] >= 'a' && movestr[0] <= 'h' && movestr[1] >= '1' && movestr[1] <= '8') {
	destination_square = movestr[0]-'a' + (movestr[1]-'1')*8;
	movestr += 2;
    } else {
	return 0;
    }

    if (!(global->board[origin_square] >= 'A' && global->board[origin_square] <= 'Z')
	&& global->side_to_move == WHITE)
	return 0;

    if (!(global->board[origin_square] >= 'a' && global->board[origin_square] <= 'z')
	&& global->side_to_move == BLACK)
	return 0;

    if (global->board[destination_square] >= 'A' && !is_capture) return 0;

    if (!(global->board[destination_square] >= 'A' && global->board[destination_square] <= 'Z')
	&& is_capture && global->side_to_move == BLACK)
	return 0;

    if (!(global->board[destination_square] >= 'a' && global->board[destination_square] <= 'z')
	&& is_capture && global->side_to_move == WHITE)
	return 0;

    global->board[destination_square] = global->board[origin_square];
    global->board[origin_square] = 0;
    if (global->side_to_move == WHITE)
	global->side_to_move = BLACK;
    else
	global->side_to_move = WHITE;

    /* XXX doesn't modify board vector */

    return 1;
}

/* Search an array of tablebases for a global position.  Array should be terminated with a NULL ptr.
 */

boolean search_tablebases_for_global_position(tablebase **tbs, global_position_t *global_position,
					      tablebase **tbptr, int32 *indexptr)
{
    int32 index;

    for (; *tbs != NULL; tbs++) {
	index = global_position_to_index(*tbs, global_position);
	if (index != -1) {
	    *tbptr = *tbs;
	    *indexptr = index;
	    return 1;
	}
    }

    return 0;
}

void print_score(tablebase *tb, int32 index, char *ptm, char *pntm)
{
    switch (tb->entries[index].movecnt) {
    case ILLEGAL_POSITION:
	printf("Illegal position\n");
	break;
    case PTM_WINS_PROPAGATION_DONE:
	printf("%s moves and wins in %d\n", ptm, tb->entries[index].mate_in_cnt/2);
	break;
    case PNTM_WINS_PROPAGATION_DONE:
	printf("%s wins in %d\n", pntm, tb->entries[index].mate_in_cnt/2);
	break;
    case PTM_WINS_PROPAGATION_NEEDED:
    case PNTM_WINS_PROPAGATION_NEEDED:
	printf("Propagation needed!?\n");
	break;
    default:
	printf("Draw\n");
	break;
    }
}

int main(int argc, char *argv[])
{
    /* Make sure this tablebase array is one bigger than we need, so it can be NULL terminated */
    tablebase *tb, *tbs[5];
    global_position_t global_position;
    boolean global_position_valid = 0;

    bzero(tbs, sizeof(tbs));

    init_movements();
    verify_movements();

    tb = parse_XML_control_file(argv[1]);
    initialize_tablebase(tb);
    propagate_all_moves_within_tablebase(tb);
    write_tablebase_to_file(tb, "a");

    init_nalimov_code();

    tbs[0] = create_2piece_tablebase();
    initialize_tablebase(tbs[0]);

    tbs[1] = create_KQK_tablebase();
    initialize_tablebase(tbs[1]);

    tbs[2] = create_KRK_tablebase();
    initialize_tablebase(tbs[2]);

    propagate_all_moves_within_tablebase(tbs[0]);

    /* The White Queen/Rook is hard-wired in here as piece #2 */
    propagate_moves_from_mobile_capture_futurebase(tbs[1], tbs[0], 0, 2);
    propagate_all_moves_within_tablebase(tbs[1]);

    propagate_moves_from_mobile_capture_futurebase(tbs[2], tbs[0], 0, 2);
    propagate_all_moves_within_tablebase(tbs[2]);

#if 1
    fprintf(stderr, "Initializing KQKR endgame\n");
    tbs[3] = create_KQKR_tablebase();
    initialize_tablebase(tbs[3]);

    fprintf(stderr, "Back propagating from KQK\n");
    propagate_moves_from_mobile_capture_futurebase(tbs[3], tbs[1], 0, 3);
    fprintf(stderr, "Back propagating from KRK\n");
    propagate_moves_from_mobile_capture_futurebase(tbs[3], tbs[2], 1, 2);
    fprintf(stderr, "Checking futuremoves...\n");
    if (have_all_futuremoves_been_handled(tbs[3])) {
	fprintf(stderr, "All futuremoves handled\n");
    } else {
	fprintf(stderr, "Some futuremoves not handled\n");
    }
    fprintf(stderr, "Intra-table propagating\n");
    propagate_all_moves_within_tablebase(tbs[3]);
#endif

    /* verify_KRK_tablebase_against_nalimov(tbs[2]); */
    /* verify_KQKR_tablebase_against_nalimov(tbs[3]); */

    read_history(".hoffman_history");

    while (1) {
	char *buffer;
	local_position_t pos;
	local_position_t nextpos;
	int piece, dir;
	struct movement * movementptr;
	global_position_t global_capture_position;
	int score;
	int32 index;

	buffer = readline(global_position_valid ? "FEN or move? " : "FEN? ");
	if (buffer == NULL) break;
	if (*buffer == '\0') continue;
	add_history(buffer);

	if (!(global_position_valid && parse_move_in_global_position(buffer, &global_position))
	    && !parse_FEN_to_global_position(buffer, &global_position)) {
	    printf(global_position_valid ? "Bad FEN or move\n\n" : "Bad FEN\n\n");
	    continue;
	}

	global_position_valid = 1;

	if (search_tablebases_for_global_position(tbs, &global_position, &tb, &index)) {

	    int32 index2;
	    char *ptm, *pntm;

	    /* 'index' is the index of the current position; 'index2' will be the index
	     * of the various next positions that we'll consider
	     */

	    printf("FEN %s\n", global_position_to_FEN(&global_position));
	    printf("Index %d\n", index);

	    if (global_position.side_to_move == WHITE) {
		ptm = "White";
		pntm = "Black";
	    } else {
		ptm = "Black";
		pntm = "White";
	    }

	    if (is_position_valid(tb, index)) {
		print_score(tb, index, ptm, pntm);
	    }

#if 1
	    if (tb == tbs[3]) {
		global_position_to_local_position(tb, &global_position, &pos);
		printf("\nNalimov score: ");
		if (EGTBProbe(pos.side_to_move == WHITE,
			      pos.mobile_piece_position[WHITE_KING],
			      pos.mobile_piece_position[BLACK_KING],
			      pos.mobile_piece_position[2], -1, pos.mobile_piece_position[3], &score) == 1) {

		    if (score > 0) {
			printf("%s moves and wins in %d\n", ptm, ((65536-4)/2)-score+1);
		    } else if (score < 0) {
			printf("%s wins in %d\n", pntm, ((65536-4)/2)+score);
		    } else {
			printf("DRAW\n");
		    }
		} else {
		    printf("ILLEGAL POSITION\n");
		}
	    }
#endif

	    /* Now we want to print a move list */

	    for (piece = 0; piece < tb->num_mobiles; piece++) {

		/* We only want to consider pieces of the side which is to move... */

		if (tb->mobile_piece_color[piece] != global_position.side_to_move)
		    continue;

		for (dir = 0; dir < number_of_movement_directions[tb->mobile_piece_type[piece]]; dir++) {

		    index_to_local_position(tb, index, &pos);

		    nextpos = pos;

		    if (pos.side_to_move == WHITE)
			nextpos.side_to_move = BLACK;
		    else
			nextpos.side_to_move = WHITE;

		    for (movementptr = movements[tb->mobile_piece_type[piece]][pos.mobile_piece_position[piece]][dir];
			 (movementptr->vector & pos.board_vector) == 0;
			 movementptr++) {

			nextpos.mobile_piece_position[piece] = movementptr->square;

			index2 = local_position_to_index(tb, &nextpos);

			/* This is the next move, so we reverse the sense of PTM and PNTM */

			if (is_position_valid(tb, index2)) {
			    printf("   %s%s ",
				   algebraic_notation[pos.mobile_piece_position[piece]],
				   algebraic_notation[movementptr->square]);
			    print_score(tb, index2, pntm, ptm);
			}

		    }

		    /* Now we consider possible captures */

		    index_to_global_position(tb, index, &global_capture_position);

		    if (((pos.side_to_move == WHITE) &&
			 ((movementptr->vector & pos.white_vector) == 0))
			|| ((pos.side_to_move == BLACK) &&
			    ((movementptr->vector & pos.black_vector) == 0))) {

			if ((movementptr->square == pos.mobile_piece_position[BLACK_KING])
			    || (movementptr->square == pos.mobile_piece_position[WHITE_KING])) {

			    /* printf("MATE\n"); */

			} else {
			    tablebase *tb2;
			    global_position_t reversed_position;

			    global_capture_position.board[pos.mobile_piece_position[piece]] = 0;
			    place_piece_in_global_position(&global_capture_position, movementptr->square,
							   tb->mobile_piece_color[piece],
							   tb->mobile_piece_type[piece]);

			    if (global_capture_position.side_to_move == WHITE)
				global_capture_position.side_to_move = BLACK;
			    else
				global_capture_position.side_to_move = WHITE;

			    reversed_position = global_capture_position;
			    invert_colors_of_global_position(&reversed_position);

			    if (search_tablebases_for_global_position(tbs, &global_capture_position,
								      &tb2, &index2)
				|| search_tablebases_for_global_position(tbs, &reversed_position,
									 &tb2, &index2)) {

				if (is_position_valid(tb2, index2)) {
				    printf ("   %sx%s ",
					    algebraic_notation[pos.mobile_piece_position[piece]],
					    algebraic_notation[movementptr->square]);
				    print_score(tb2, index2, pntm, ptm);
				}
			    } else {
				printf("Can't find %sx%s in tablebases!!?!\n",
				       algebraic_notation[pos.mobile_piece_position[piece]],
				       algebraic_notation[movementptr->square]);
			    }
			}
		    }
		    /* end of capture search */

		}

	    }
	}
    }
    write_history(".hoffman_history");
    printf("\n");
}
