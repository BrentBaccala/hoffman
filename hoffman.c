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
#include <netdb.h>	/* for gethostbyname() */

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

/* Number of possibilities for pawn promotions.  "2" means queen and knight, but that can cause some
 * problems, as I've learned the hard (and embarrassing) way.
 */

#define PROMOTION_POSSIBILITIES 3

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
char piece_char[NUM_PIECES+1] = {'K', 'Q', 'R', 'B', 'N', 'P', 'E', 0};

char * colors[3] = {"WHITE", "BLACK", NULL};

unsigned char global_pieces[2][NUM_PIECES] = {{'K', 'Q', 'R', 'B', 'N', 'P', 'E'},
					      {'k', 'q', 'r', 'b', 'n', 'p', 'e'}};

#define WHITE 0
#define BLACK 1


/**** TABLEBASE STRUCTURE AND OPERATIONS ****/

/* The 'xml' in the tablebase is authoritative; much of the other info is extracted from it
 * for efficiency.
 *
 * movecnt - 0 if this entry is ready to propagate; 255 if it has been propagated
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

#define RESTRICTION_NONE 0
#define RESTRICTION_DISCARD 1
#define RESTRICTION_CONCEDE 2

char * restriction_types[4] = {"NONE", "DISCARD", "CONCEDE", NULL};

typedef struct tablebase {
    xmlDocPtr xml;
    int num_mobiles;
    int move_restrictions[2];		/* one for each color */
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

/* Parses XML, creates a tablebase structure corresponding to it, and returns it.
 *
 * Eventually, I want to provide a DTD and validate the XML input, so there's very little error
 * checking here.  The idea is that the validation will ultimately provide the error check.
 */

tablebase * parse_XML_into_tablebase(xmlDocPtr doc)
{
    tablebase *tb;
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    tb = malloc(sizeof(tablebase));
    if (tb == NULL) {
	fprintf(stderr, "Can't malloc tablebase\n");
	return NULL;
    }
    bzero(tb, sizeof(tablebase));

    tb->xml = doc;

    /* Fetch the mobile pieces from the XML */

    context = xmlXPathNewContext(doc);
    result = xmlXPathEvalExpression((const xmlChar *) "//mobile", context);
    if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
	fprintf(stderr, "No mobile pieces!\n");
    } else if (result->nodesetval->nodeNr < 2) {
	fprintf(stderr, "Too few mobile pieces!\n");
    } else if (result->nodesetval->nodeNr > MAX_MOBILES) {
	fprintf(stderr, "Too many mobile pieces!\n");
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

    xmlXPathFreeContext(context);

    context = xmlXPathNewContext(doc);
    result = xmlXPathEvalExpression((const xmlChar *) "//move-restriction", context);
    if (! xmlXPathNodeSetIsEmpty(result->nodesetval)) {
	int i;
	for (i=0; i < result->nodesetval->nodeNr; i++) {
	    xmlChar * color_str;
	    xmlChar * type_str;
	    int color;
	    int type;

	    color_str = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "color");
	    type_str = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "type");

	    color = find_name_in_array((char *) color_str, colors);
	    type = find_name_in_array((char *) type_str, restriction_types);
	    if ((color == -1) || (type == -1)) {
		fprintf(stderr, "Illegal move restriction\n");
	    } else {
		if ((tb->move_restrictions[color] > 0) && (tb->move_restrictions[color] != type)) {
		    fprintf(stderr, "Incompatible move restrictions\n");
		} else {
		    tb->move_restrictions[color] = type;
		}
	    }
	}
    }

    xmlXPathFreeContext(context);

    return tb;
}

/* Parses an XML control file.  This function allocates an entries array, as well.
 */

tablebase * parse_XML_control_file(char *filename)
{
    xmlParserCtxtPtr ctxt; /* the parser context */
    xmlDocPtr doc;
    tablebase *tb;

    /* create a parser context */
    ctxt = xmlNewParserCtxt();
    if (ctxt == NULL) {
        fprintf(stderr, "Failed to allocate parser context\n");
	return;
    }
    /* parse the file, activating the DTD validation option */
    doc = xmlCtxtReadFile(ctxt, filename, NULL, XML_PARSE_DTDVALID);

    /* check if parsing suceeded */
    if (doc == NULL) {
	fprintf(stderr, "'%s' failed XML read\n", filename);
	return NULL;
    } else {
	/* check if validation suceeded */
        if (ctxt->valid == 0) {
	    fprintf(stderr, "WARNING: '%s' failed XML validatation\n", filename);
	}
    }
    /* free up the parser context */
    xmlFreeParserCtxt(ctxt);

#if 0
    doc = xmlReadFile(filename, NULL, 0);
    if (doc == NULL) {
	fprintf(stderr, "'%s' failed XML read\n", filename);
	return NULL;
    }
#endif

    tb = parse_XML_into_tablebase(doc);

    /* The "1" is because side-to-play is part of the position; "6" for the 2^6 squares on the board */

    tb->entries = (struct fourbyte_entry *) calloc(1<<(1+6*tb->num_mobiles), sizeof(struct fourbyte_entry));
    if (tb->entries == NULL) {
	fprintf(stderr, "Can't malloc tablebase entries\n");
    }

    /* We don't free the XML doc because the tablebase struct contains a pointer to it */

    return tb;
}

/* Loads a futurebase, by mmap'ing it into memory, parsing the XML header, and returning a pointer
 * to the resulting tablebase structure after setting 'entries' to point into the mmap'ed data.
 */

tablebase * load_futurebase_from_file(char *filename)
{
    int fd;
    struct stat filestat;
    size_t length;
    char *fileptr;
    int xml_size;
    xmlDocPtr doc;
    xmlNodePtr root_element;
    xmlChar * offsetstr;
    long offset;
    tablebase *tb;

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
	fprintf(stderr, "Can not open futurebase '%s'\n", filename);
	return NULL;
    }

    fstat(fd, &filestat);

    fileptr = mmap(0, filestat.st_size, PROT_READ, MAP_SHARED, fd, 0);

    close(fd);

    for (xml_size = 0; fileptr[xml_size] != '\0'; xml_size ++) ;

    doc = xmlReadMemory(fileptr, xml_size, NULL, NULL, 0);

    tb = parse_XML_into_tablebase(doc);

    root_element = xmlDocGetRootElement(doc);

    if (xmlStrcmp(root_element->name, (const xmlChar *) "tablebase")) {
	fprintf(stderr, "'%s' failed XML parse\n", filename);
	return NULL;
    }

    offsetstr = xmlGetProp(root_element, (const xmlChar *) "offset");

    offset = strtol((const char *) offsetstr, NULL, 0);

    tb->entries = (struct fourbyte_entry *) (fileptr + offset);

    return tb;
}

/* Given a tablebase, create an XML header describing its contents and return it.
 */

xmlDocPtr create_XML_header(tablebase *tb)
{
    xmlDocPtr doc;
    xmlNodePtr tablebase, pieces, node;
    int color;
    int piece;
    time_t creation_time;
    char hostname[256];
    struct hostent *he;

    doc = xmlNewDoc((const xmlChar *) "1.0");
    xmlCreateIntSubset(doc, BAD_CAST "tablebase", NULL, BAD_CAST "tablebase.dtd");

    tablebase = xmlNewDocNode(doc, NULL, (const xmlChar *) "tablebase", NULL);
    xmlNewProp(tablebase, (const xmlChar *) "offset", (const xmlChar *) "0x1000");
    xmlNewProp(tablebase, (const xmlChar *) "format", (const xmlChar *) "fourbyte");
    xmlNewProp(tablebase, (const xmlChar *) "index", (const xmlChar *) "naive");
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

    for (color = 0; color < 2; color ++) {
	if (tb->move_restrictions[color] != RESTRICTION_NONE) {
	    node = xmlNewChild(tablebase, NULL, (const xmlChar *) "move-restriction", NULL);
	    xmlNewProp(node, (const xmlChar *) "color", (const xmlChar *) colors[color]);
	    xmlNewProp(node, (const xmlChar *) "type",
		       (const xmlChar *) restriction_types[tb->move_restrictions[color]]);
	}
    }

    node = xmlNewChild(tablebase, NULL, (const xmlChar *) "generating-program", NULL);
    xmlNewProp(node, (const xmlChar *) "name", (const xmlChar *) "Hoffman");
    xmlNewProp(node, (const xmlChar *) "version", (const xmlChar *) "$Revision: 1.59 $");

    node = xmlNewChild(tablebase, NULL, (const xmlChar *) "generating-time", NULL);
    time(&creation_time);
    xmlNewProp(node, (const xmlChar *) "time", (const xmlChar *) ctime(&creation_time));

    gethostname(hostname, sizeof(hostname));
    he = gethostbyname(hostname);
    node = xmlNewChild(tablebase, NULL, (const xmlChar *) "generating-host", NULL);
    xmlNewProp(node, (const xmlChar *) "fqdn", (const xmlChar *) he->h_name);

    /* xmlSaveFile("-", doc); */
    return doc;
}

/* do_write() is like the system call write(), but keeps repeating until the write is complete */

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

#define ROW(square) ((square) / 8)
#define COL(square) ((square) % 8)

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
	/* I've added this pawn check because I've had some problems.  This makes the
	 * return of this function match up with the return of index_to_global_position
	 */
	if ((tb->mobile_piece_type[piece] == PAWN)
	    && ((pos->mobile_piece_position[piece] < 8) || (pos->mobile_piece_position[piece] >= 56))) {
	    return -1;
	}
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

boolean index_to_local_position(tablebase *tb, int32 index, local_position_t *p)
{
    /* Given an index, fill in a board position.  Obviously has to correspond to local_position_to_index()
     * and it's a big bug if it doesn't.  The boolean that gets returned is TRUE if the operation
     * succeeded (the index is at least minimally valid) and FALSE if the index is so blatantly
     * illegal (two pieces on the same square) that we can't even fill in the position.
     */

    int piece;

    bzero(p, sizeof(local_position_t));

    p->side_to_move = index & 1;
    index >>= 1;

    for (piece = 0; piece < tb->num_mobiles; piece++) {
	/* I've added this pawn check because I've had some problems.  This makes the
	 * return of this function match up with the return of index_to_global_position
	 */
	if ((tb->mobile_piece_type[piece] == PAWN) && (((index & 63) < 8) || ((index & 63) >= 56))) {
	    return 0;
	}
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

/* This function could be made a bit faster, but this simpler version is hopefully safer. */

int index_to_side_to_move(tablebase *tb, int32 index)
{
    local_position_t position;

    if (! index_to_local_position(tb, index, &position)) return -1;
    else return position.side_to_move;
}

inline void flip_side_to_move_local(local_position_t *position)
{
    if (position->side_to_move == WHITE)
	position->side_to_move = BLACK;
    else
	position->side_to_move = WHITE;
}

inline void flip_side_to_move_global(global_position_t *position)
{
    if (position->side_to_move == WHITE)
	position->side_to_move = BLACK;
    else
	position->side_to_move = WHITE;
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

	/* There are other possibilities for illegal combinations, namely a king next to the other
	 * king, but that possibility is taken care of with an is_position_valid() check.  I need
	 * this check here to keep my Nalimov verification routine from screaming about pawns on the
	 * eighth rank.
	 */

	if ((tb->mobile_piece_type[piece] == PAWN)
	    && (((index & 63) < 8) || ((index & 63) >= 56))) {
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
 * endgame that we now want to propagate into a game where the rook is black, not white.  If there
 * are pawns in the game, this function has to reflect the board around a horizontal centerline.
 */

void invert_colors_of_global_position(global_position_t *global)
{
    int squareA;

    global->board_vector = 0;

    for (squareA=0; squareA < NUM_SQUARES/2; squareA++) {
	unsigned char pieceA;
	unsigned char pieceB;
	int squareB = square(7-ROW(squareA),COL(squareA));

	pieceA = global->board[squareA];
	pieceB = global->board[squareB];

	if ((pieceA >= 'A') && (pieceA <= 'Z')) {
	    pieceA += 'a' - 'A';
	} else if ((pieceA >= 'a') && (pieceA <= 'z')) {
	    pieceA += 'A' - 'a';
	}

	if ((pieceB >= 'A') && (pieceB <= 'Z')) {
	    pieceB += 'a' - 'A';
	} else if ((pieceB >= 'a') && (pieceB <= 'z')) {
	    pieceB += 'A' - 'a';
	}
	
	global->board[squareA] = pieceB;
	global->board[squareB] = pieceA;

	if (pieceB >= 'A') global->board_vector |= BITVECTOR(squareA);
	if (pieceA >= 'A') global->board_vector |= BITVECTOR(squareB);
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

/* #define DEBUG_MOVE 393220 */

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
	if ((mate_in_count < tb->entries[index].mate_in_cnt) && (tb->entries[index].mate_in_cnt != 255)) {
	    /* (255 means we haven't found any mates yet in this position) As above, this can
	     * happen during a futurebase back propagation, and if it does... we do nothing!
	     * Since this is PNTM wins, PTM will make the move leading to the slowest mate.
	     */
	    /* XXX need to think more about the stalemates */
	    /* fprintf(stderr, "mate-in count dropped in add_one_to_PNTM_wins?\n"); */
	    /* tb->entries[index].mate_in_cnt = mate_in_count; */
	    /* tb->entries[index].stalemate_cnt = stalemate_count; */
	} else {
	    tb->entries[index].mate_in_cnt = mate_in_count;
	    tb->entries[index].stalemate_cnt = stalemate_count;
	}

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

/* Pawns are, of course, special.  We have seperate vectors for different types of pawn movements.
 * Each array is indexed first by square number, then by side (WHITE or BLACK - this doesn't exist
 * for other pieces), then by the number of possibilities (at most two normal movements, at most two
 * captures, and one more for the all-ones bitvector to terminate)
 *
 * All of these are FORWARD motions.
 */

struct movement normal_pawn_movements[NUM_SQUARES][2][3];
struct movement capture_pawn_movements[NUM_SQUARES][2][3];

struct movement normal_pawn_movements_bkwd[NUM_SQUARES][2][3];
struct movement capture_pawn_movements_bkwd[NUM_SQUARES][2][3];

/* How many different directions can each piece move in?  Knights have 8 directions because they
 * can't be blocked in any of them.
 */

int number_of_movement_directions[7] = {8,8,4,4,8,0,0};
int maximum_movements_in_one_direction[7] = {1,7,7,7,1,0,0};

enum {RIGHT, LEFT, UP, DOWN, DIAG_UL, DIAG_UR, DIAG_DL, DIAG_DR, KNIGHTmove}
movementdir[5][8] = {
    {RIGHT, LEFT, UP, DOWN, DIAG_UL, DIAG_UR, DIAG_DL, DIAG_DR},	/* King */
    {RIGHT, LEFT, UP, DOWN, DIAG_UL, DIAG_UR, DIAG_DL, DIAG_DR},	/* Queen */
    {RIGHT, LEFT, UP, DOWN},						/* Rook */
    {DIAG_UL, DIAG_UR, DIAG_DL, DIAG_DR},				/* Bishop */
    {KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove, KNIGHTmove},	/* Knights are special... */
};



char algebraic_notation[64][3];

void init_movements()
{
    int square, piece, dir, mvmt, color;

    for (square=0; square < NUM_SQUARES; square++) {
	bitvector[square] = 1ULL << square;
	algebraic_notation[square][0] = 'a' + square%8;
	algebraic_notation[square][1] = '1' + square/8;
	algebraic_notation[square][2] = '\0';
    }

    for (piece=KING; piece <= KNIGHT; piece++) {

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

    /* Now for the pawns... */

    for (square=0; square < NUM_SQUARES; square ++) {

	for (color = WHITE; color <= BLACK; color ++) {

	    int forwards_pawn_move = ((color == WHITE) ? 8 : -8);
	    int backwards_pawn_move = ((color == WHITE) ? -8 : 8);

	    /* Forward pawn movements
	     *
	     * An ordinary pawn move... unless its a white pawn on the second rank, or a black
	     * pawn on the seventh.  In these two cases, there is a possible double move as
	     * well.
	     *
	     * XXX I'm ignoring pawns on the first rank completely for now.  Might want to come
	     * back later and regard them as en passant pawns.
	     */

	    mvmt = 0;

	    if ((ROW(square) >= 1) && (ROW(square) <= 6)) {

		normal_pawn_movements[square][color][mvmt].square = square + forwards_pawn_move;
		normal_pawn_movements[square][color][mvmt].vector = BITVECTOR(square + forwards_pawn_move);

		mvmt ++;
	    }

	    if (((color == WHITE) && (ROW(square) == 1)) || ((color == BLACK) && (ROW(square) == 6))) {

		normal_pawn_movements[square][color][mvmt].square = square + 2*forwards_pawn_move;
		normal_pawn_movements[square][color][mvmt].vector = BITVECTOR(square + 2*forwards_pawn_move);

		mvmt ++;

	    }

	    normal_pawn_movements[square][color][mvmt].square = -1;
	    normal_pawn_movements[square][color][mvmt].vector = allones_bitvector;

	    /* Backwards pawn movements */

	    mvmt = 0;

	    if (((color == WHITE) && (ROW(square) > 1)) || ((color == BLACK) && (ROW(square) < 6))) {

		normal_pawn_movements_bkwd[square][color][mvmt].square = square + backwards_pawn_move;
		normal_pawn_movements_bkwd[square][color][mvmt].vector = BITVECTOR(square + backwards_pawn_move);
		mvmt ++;
	    }

	    if (((color == WHITE) && (ROW(square) == 3)) || ((color == BLACK) && (ROW(square) == 4))) {

		normal_pawn_movements_bkwd[square][color][mvmt].square = square + 2*backwards_pawn_move;
		normal_pawn_movements_bkwd[square][color][mvmt].vector = BITVECTOR(square + 2*backwards_pawn_move);
		mvmt ++;
	    }

	    normal_pawn_movements_bkwd[square][color][mvmt].square = -1;
	    normal_pawn_movements_bkwd[square][color][mvmt].vector = allones_bitvector;

	    /* Forward pawn captures. */

	    mvmt = 0;

	    if ((ROW(square) >= 1) && (ROW(square) <= 6)) {

		if (COL(square) > 0) {

		    capture_pawn_movements[square][color][mvmt].square
			= square + forwards_pawn_move - 1;
		    capture_pawn_movements[square][color][mvmt].vector
			= BITVECTOR(square + forwards_pawn_move - 1);

		    mvmt ++;

		}

		if (COL(square) < 7) {

		    capture_pawn_movements[square][color][mvmt].square
			= square + forwards_pawn_move + 1;
		    capture_pawn_movements[square][color][mvmt].vector
			= BITVECTOR(square + forwards_pawn_move + 1);

		    mvmt ++;

		}
	    }

	    capture_pawn_movements[square][color][mvmt].square = -1;
	    capture_pawn_movements[square][color][mvmt].vector = allones_bitvector;

	    /* Backwards pawn captures */

	    mvmt = 0;

	    if (((color == WHITE) && (ROW(square) > 1)) || ((color == BLACK) && (ROW(square) < 6))) {

		if (COL(square) > 0) {

		    capture_pawn_movements_bkwd[square][color][mvmt].square
			= square + backwards_pawn_move - 1;
		    capture_pawn_movements_bkwd[square][color][mvmt].vector
			= BITVECTOR(square + backwards_pawn_move - 1);

		    mvmt ++;

		}

		if (COL(square) < 7) {

		    capture_pawn_movements_bkwd[square][color][mvmt].square
			= square + backwards_pawn_move + 1;
		    capture_pawn_movements_bkwd[square][color][mvmt].vector
			= BITVECTOR(square + backwards_pawn_move + 1);

		    mvmt ++;

		}
	    }

	    capture_pawn_movements_bkwd[square][color][mvmt].square = -1;
	    capture_pawn_movements_bkwd[square][color][mvmt].vector = allones_bitvector;

	}

    }

}

/* This routine is pretty fast, so I just call it once every time the program runs.  It has to be
 * used after any changes to the code above to verify that those complex movement vectors are
 * correct, or at least consistent.  We're using this in a game situation.  We can't afford bugs in
 * this code.
 */

void verify_movements()
{
    int piece;
    int squareA, squareB;
    int dir;
    int color;
    struct movement * movementptr;
    int pawn_option;

    /* For everything except pawns, if it can move from A to B, then it better be able to move from
     * B to A...
     */

    for (piece=KING; piece <= KNIGHT; piece ++) {

	for (squareA=0; squareA < NUM_SQUARES; squareA ++) {

	    for (squareB=0; squareB < NUM_SQUARES; squareB ++) {

		int movement_possible = 0;
		int reverse_movement_possible = 0;

		/* check for possible self-movement, if A and B are the same square */

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

		/* check for possible A to B move */

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

    /* Pawns are special */

    piece = PAWN;

    for (pawn_option = 0; pawn_option < 4; pawn_option ++) {

	struct movement * fwd_movement;
	struct movement * rev_movement;

	for (color = WHITE; color <= BLACK; color ++) {

	    /* fprintf(stderr, "Pawn option %d; color %s\n", pawn_option, colors[color]); */

	    for (squareA=0; squareA < NUM_SQUARES; squareA ++) {

		for (squareB=0; squareB < NUM_SQUARES; squareB ++) {

		    int movement_possible = 0;
		    int reverse_movement_possible = 0;

		    switch (pawn_option) {
		    case 0:
			fwd_movement = normal_pawn_movements[squareA][color];
			rev_movement = normal_pawn_movements_bkwd[squareB][color];
			break;
		    case 1:
			fwd_movement = normal_pawn_movements_bkwd[squareA][color];
			rev_movement = normal_pawn_movements[squareB][color];
			break;
		    case 2:
			fwd_movement = capture_pawn_movements[squareA][color];
			rev_movement = capture_pawn_movements_bkwd[squareB][color];
			break;
		    case 3:
			fwd_movement = capture_pawn_movements_bkwd[squareA][color];
			rev_movement = capture_pawn_movements[squareB][color];
			break;
		    }

		    /* check for self-movement */

		    if (squareA == squareB) {
			for (movementptr = fwd_movement;
			     (movementptr->vector & BITVECTOR(squareB)) == 0;
			     movementptr++) ;
			if ((movementptr->square != -1) || (movementptr->vector != allones_bitvector)) {
			    fprintf(stderr, "Self movement possible!? PAWN %d %d\n",
				    squareA, movementptr->square);
			}
		    }

		    /* check for possible A to B move */

		    for (movementptr = fwd_movement;
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


		    /* check for possible B to A reverse move */

		    for (movementptr = rev_movement;
			 (movementptr->vector & BITVECTOR(squareA)) == 0;
			 movementptr++) ;

		    if (movementptr->square != -1) reverse_movement_possible=1;

		    if (movement_possible && !reverse_movement_possible) {
			fprintf(stderr, "reverse movement impossible: %s %d %d\n",
				piece_name[piece], squareA, squareB);
		    }
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

void propagate_index_from_futurebase(tablebase *tb, tablebase *futurebase,
				      int32 future_index, int32 current_index, int *mate_in_limit)
{
    /* Skip everything else if the position isn't valid.  In particular,
     * we don't track futuremove propagation for illegal positions.
     */

    if (is_position_valid(tb, current_index)) {

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

	/* This is pretty primitive, but we need some way to figure how deep to look during
	 * intra-table propagation.
	 */

	if (get_mate_in_count(futurebase, future_index) > *mate_in_limit)
	    *mate_in_limit = get_mate_in_count(futurebase, future_index);
    }
}

void propagate_one_move_from_mobile_capture_futurebase(tablebase *tb, tablebase *futurebase,
						       int32 future_index, local_position_t *current_position,
						       int *mate_in_limit)
{
    int32 current_index;

    /* Look up the position in the current tablebase... */

    current_index = local_position_to_index(tb, current_position);

    if (current_index == -1) {
	fprintf(stderr, "Can't lookup position in futurebase propagation!\n");
    }

    propagate_index_from_futurebase(tb, futurebase, future_index, current_index, mate_in_limit);
}

void propagate_global_position_from_futurebase(tablebase *tb, tablebase *futurebase,
					       int32 future_index, global_position_t *position, int *mate_in_limit)
{
    int32 current_index;

    /* Look up the position in the current tablebase... */

    current_index = global_position_to_index(tb, position);

    if (current_index == -1) {
	fprintf(stderr, "Can't lookup position in futurebase propagation!\n");
    }

    propagate_index_from_futurebase(tb, futurebase, future_index, current_index, mate_in_limit);
}


/* Back propagate promotion moves
 *
 * Passed a piece (a global position character) that the pawn is promoting into.  Searches
 * futurebase for positions with that piece on the last rank and back-props.
 */

void propagate_moves_from_promotion_futurebase(tablebase *tb, tablebase *futurebase,
					       int invert_colors_of_futurebase,
					       unsigned char promoted_piece,
					       int *mate_in_limit)
{
    int32 future_index;
    int32 max_future_index_static = max_index(futurebase);
    global_position_t future_position;
    int square;

    int promotion_color = ((promoted_piece < 'a') ? WHITE : BLACK);
    int first_back_rank_square = ((promotion_color == WHITE) ? 56 : 0);
    int last_back_rank_square = ((promotion_color == WHITE) ? 63 : 7);
    int promotion_move = ((promotion_color == WHITE) ? 8 : -8);

    /* We could limit the range of future_index here */

    for (future_index = 0; future_index < max_future_index_static; future_index ++) {

	if (index_to_global_position(futurebase, future_index, &future_position)) {

	    if (invert_colors_of_futurebase)
		invert_colors_of_global_position(&future_position);

	    /* Whatever color the promoted piece is, after the promotion it must be the other side
	     * to move.
	     */

	    if (future_position.side_to_move == promotion_color) continue;

	    /* Since the last move had to have been a promotion move, there is absolutely no way we
	     * could have en passant capturable pawns in the futurebase position.
	     */

	    /* We're back-proping one half move to the promotion move. */

	    flip_side_to_move_global(&future_position);

	    /* Consider only positions with the promoted piece on the last rank and with an empty
	     * square right behind where a pawn could have come from.
	     */

	    for (square = first_back_rank_square; square <= last_back_rank_square; square ++) {

		if ((future_position.board[square] == promoted_piece)
		    && (future_position.board[square - promotion_move] < 'A')) {

		    /* Replace the promoted piece with a pawn on the seventh */

		    future_position.board[square] = 0;
		    future_position.board[square - promotion_move]
			= ((promotion_color == WHITE) ? 'P' : 'p');

		    /* Back propagate the resulting position */

		    /* Also want to back prop any similar positions with one of the pawns from the
		     * side that didn't promote in an en passant state.
		     */

		    propagate_global_position_from_futurebase(tb, futurebase, future_index, &future_position, mate_in_limit);

		    /* ...and put everything back so we can keep running this loop! */

		    future_position.board[square - promotion_move] = 0;
		    future_position.board[square] = promoted_piece;
		}
	    }
	}
    }
}

void propagate_moves_from_promotion_capture_futurebase(tablebase *tb, tablebase *futurebase,
						       int invert_colors_of_futurebase,
						       unsigned char promoted_piece,
						       unsigned char captured_piece,
						       int *mate_in_limit)
{
    int32 future_index;
    int32 max_future_index_static = max_index(futurebase);
    global_position_t future_position;
    int square;

    int promotion_color = ((promoted_piece < 'a') ? WHITE : BLACK);
    int first_back_rank_square = ((promotion_color == WHITE) ? 56 : 0);
    int last_back_rank_square = ((promotion_color == WHITE) ? 63 : 7);
    int promotion_move = ((promotion_color == WHITE) ? 8 : -8);

    /* We could limit the range of future_index here */

    for (future_index = 0; future_index < max_future_index_static; future_index ++) {

	if (index_to_global_position(futurebase, future_index, &future_position)) {

	    if (invert_colors_of_futurebase)
		invert_colors_of_global_position(&future_position);

	    /* Whatever color the promoted piece is, after the promotion it must be the other side
	     * to move.
	     */

	    if (future_position.side_to_move == promotion_color) continue;

	    /* Since the last move had to have been a promotion move, there is absolutely no way we
	     * could have en passant capturable pawns in the futurebase position.
	     */

	    /* We're back-proping one half move to the promotion move. */

	    flip_side_to_move_global(&future_position);

	    /* Consider only positions with the promoted piece on the last rank and with an empty
	     * square right behind where a pawn could have captured from.
	     */

	    for (square = first_back_rank_square; square <= last_back_rank_square; square ++) {

		if (future_position.board[square] == promoted_piece) {

		    /* Put the captured piece where it's going to go */

		    future_position.board[square] = captured_piece;

		    if ((COL(square) != 0) && (future_position.board[square - promotion_move - 1] < 'A')) {

			/* Replace the promoted piece with a pawn on the seventh */

			future_position.board[square - promotion_move - 1]
			    = ((promotion_color == WHITE) ? 'P' : 'p');

			/* Back propagate the resulting position */

			/* Also want to back prop any similar positions with one of the pawns from
			 * the side that didn't promote in an en passant state.
			 */

			propagate_global_position_from_futurebase(tb, futurebase, future_index, &future_position, mate_in_limit);

			/* We're about to use this position just below, probably... */

			future_position.board[square - promotion_move - 1] = '\0';
		    }

		    if ((COL(square) != 7) && (future_position.board[square - promotion_move + 1] < 'A')) {

			/* Replace the promoted piece with a pawn on the seventh */

			future_position.board[square] = captured_piece;
			future_position.board[square - promotion_move + 1]
			    = ((promotion_color == WHITE) ? 'P' : 'p');

			/* Back propagate the resulting position */

			/* Also want to back prop any similar positions with one of the pawns from
			 * the side that didn't promote in an en passant state.
			 */

			propagate_global_position_from_futurebase(tb, futurebase, future_index, &future_position, mate_in_limit);

			future_position.board[square - promotion_move + 1] = '\0';
		    }

		    /* We've taken the pawns back off the board, now put the promoted piece back
		     * where the captured piece was, because we want to keep running this loop.
		     */

		    future_position.board[square] = promoted_piece;

		}
	    }
	}
    }
}

void propagate_moves_from_mobile_capture_futurebase(tablebase *tb, tablebase *futurebase,
						    int invert_colors_of_futurebase, int captured_piece, int *mate_in_limit)
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

	    /* Since the last move had to have been a capture move, there is absolutely no way we
	     * could have en passant capturable pawns in the futurebase position.
	     */

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

		if (! global_position_to_local_position(tb, &future_position, &current_position)) {
		    fprintf(stderr, "Can't convert global position to local during back-prop\n");
		    continue;
		}

		flip_side_to_move_local(&current_position);

		if (current_position.mobile_piece_position[captured_piece] != -1) {
		    fprintf(stderr, "Captured piece position specified too soon during back-prop\n");
		    continue;
		}

		/* Place the captured piece back into the position on the square from which
		 * the moving piece started (i.e, ended) its move.
		 *
		 * Probably should use place_piece_in_local_position here, but we don't.  The board
		 * vectors don't get updated, but that doesn't matter since we're never going to
		 * look at the square the captured piece is on as a possible origin square for the
		 * capturing piece.
		 */

		if ((tb->mobile_piece_type[captured_piece] == PAWN)
		    && ((current_position.mobile_piece_position[piece] < 8)
			|| (current_position.mobile_piece_position[piece] >= 56))) {

		    /* If we "captured" a pawn on the first or eighth ranks, well, obviously that
		     * can't happen...
		     */

		    continue;
		}

		current_position.mobile_piece_position[captured_piece]
		    = current_position.mobile_piece_position[piece];

		/* We consider all possible backwards movements of the piece which captured. */

		if (tb->mobile_piece_type[piece] != PAWN) {

		    for (dir = 0; dir < number_of_movement_directions[tb->mobile_piece_type[piece]]; dir++) {

			/* Make sure we start each movement of the capturing piece from the capture square */

			current_position.mobile_piece_position[piece]
			    = current_position.mobile_piece_position[captured_piece];

			for (movementptr = movements[tb->mobile_piece_type[piece]][current_position.mobile_piece_position[piece]][dir];
			     (movementptr->vector & future_position.board_vector) == 0;
			     movementptr++) {

			    /* Move the capturing piece... */

			    current_position.mobile_piece_position[piece] = movementptr->square;

			    propagate_one_move_from_mobile_capture_futurebase(tb, futurebase, future_index, &current_position, mate_in_limit);

			}
		    }

		} else {

		    /* Yes, pawn captures are special */

		    /* The en passant special case: if both the piece that captured and the piece
		     * that was captured are both pawns, and either a white pawn captured from the
		     * fifth to the sixth rank, or a black pawn captured from the fourth to the
		     * third, then there are two possible back prop positions - the obvious one, and
		     * the one where the captured pawn was in an en passant state.  This is in
		     * addition to back prop positions with a pawn in the 'obvious' capturable
		     * position, and some other pawn in an en passant state.
		     */


		    for (movementptr = capture_pawn_movements_bkwd[current_position.mobile_piece_position[piece]][tb->mobile_piece_color[piece]];
			 movementptr->square != -1;
			 movementptr++) {

			if ((movementptr->vector & future_position.board_vector) != 0) continue;

			/* non-promotion capture */

			current_position.mobile_piece_position[piece] = movementptr->square;

			propagate_one_move_from_mobile_capture_futurebase(tb, futurebase, future_index,
									  &current_position, mate_in_limit);
		    }

		}
	    }

	}
    }
}

/* Back propagates from all the futurebases.
 *
 * Should be called after the tablebase has been initialized, but before intra-table propagation.
 *
 * Runs through the parsed XML control file, pulls out all the futurebases, and back-propagates each
 * one.  Right now, only handles futurebases that resulted from captures, and that therefore have
 * exactly one less mobile piece than the current tablebase.  Doesn't handle futurebases due to pawn
 * promotions, nor to frozen pieces moving, nor to any configuration of mobile pieces other than
 * that described (like one of the frozen pieces becoming mobile in the futurebase).
 *
 * Returns maximum mate_in value, or -1 if something went wrong
 */

int back_propagate_all_futurebases(tablebase *tb) {

    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;
    int mate_in_limit = 0;

    /* Fetch the futurebases from the XML */

    context = xmlXPathNewContext(tb->xml);
    result = xmlXPathEvalExpression((const xmlChar *) "//futurebase", context);
    if ((tb->num_mobiles > 2) && xmlXPathNodeSetIsEmpty(result->nodesetval)) {
	fprintf(stderr, "No futurebases!\n");
    } else {
	int i;

	for (i=0; i < result->nodesetval->nodeNr; i++) {
	    xmlChar * filename;
	    xmlChar * colors_property;
	    xmlChar * type;
	    tablebase * futurebase;
	    int invert_colors = 0;
	    int future_piece;
	    int piece;
	    int color;
	    int piece_vector;
	    int promoted_piece = -1;
	    unsigned char captured_piece_char;
	    unsigned char promoted_piece_char;

	    filename = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "filename");

	    colors_property = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "colors");
	    if (colors_property != NULL) {
		if (!strcasecmp((char *) colors_property, "invert")) invert_colors = 1;
		xmlFree(colors_property);
	    }

	    futurebase = load_futurebase_from_file((char *) filename);

	    /* load_futurebase_from_file() already printed some kind of error message */
	    if (futurebase == NULL) return -1;

	    /* Check futurebase to make sure its move restriction(s) match our own */

	    for (color = 0; color < 2; color ++) {
		if ((futurebase->move_restrictions[color] != RESTRICTION_NONE)
		    && (futurebase->move_restrictions[color]
			!= tb->move_restrictions[invert_colors ? 1 - color : color])) {
		    fprintf(stderr, "'%s': Futurebase doesn't match move restrictions!\n", filename);
		    return -1;
		}
	    }

	    type = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "type");

	    if ((type != NULL) && !strcasecmp((char *) type, "capture")) {

		/* It's a capture futurebase.  Futurebase should have exactly one less mobile than
		 * the current tablebase.  Find it.
		 */

		/* piece_vector - set a bit for every mobile piece in current tablebase */
		piece_vector = (1 << tb->num_mobiles) - 1;

		for (future_piece = 0; future_piece < futurebase->num_mobiles; future_piece ++) {
		    for (piece = 0; piece < tb->num_mobiles; piece ++) {
			if (! (piece_vector & (1 << piece))) continue;
			if ((tb->mobile_piece_type[piece] == futurebase->mobile_piece_type[future_piece])
			    && ((!invert_colors &&
				 (tb->mobile_piece_color[piece] == futurebase->mobile_piece_color[future_piece]))
				|| (invert_colors &&
				    (tb->mobile_piece_color[piece] != futurebase->mobile_piece_color[future_piece])))) {
			    piece_vector ^= (1 << piece);
			    break;
			}
		    }
		    if (piece == tb->num_mobiles) {
			fprintf(stderr, "'%s': Couldn't find future piece in tablebase\n", filename);
			return -1;
		    }
		}

		for (piece = 0; piece < tb->num_mobiles; piece ++) {
		    if ((piece_vector & (1 << piece))) break;
		}

		if (piece == tb->num_mobiles) {
		    fprintf(stderr, "'%s': No extra mobile piece in futurebase\n", filename);
		    return -1;
		}

		piece_vector ^= (1 << piece);

		if (piece_vector != 0) {
		    fprintf(stderr, "'%s': Too many extra mobile pieces in futurebase\n", filename);
		    return -1;
		}

		fprintf(stderr, "Back propagating from '%s'\n", (char *) filename);

		propagate_moves_from_mobile_capture_futurebase(tb, futurebase, invert_colors, piece, &mate_in_limit);

	    } else if ((type != NULL) && !strcasecmp((char *) type, "promotion")) {

		/* It's a promotion futurebase.  Futurebase should have exactly the same number of
		 * pieces as the current tablebase, and one of our pawns should have promoted into
		 * something else.  Determine what the pawn promoted into.
		 */

		/* piece_vector - set a bit for every mobile piece in current tablebase */
		piece_vector = (1 << tb->num_mobiles) - 1;

		for (future_piece = 0; future_piece < futurebase->num_mobiles; future_piece ++) {
		    for (piece = 0; piece < tb->num_mobiles; piece ++) {
			if (! (piece_vector & (1 << piece))) continue;
			if ((tb->mobile_piece_type[piece] == futurebase->mobile_piece_type[future_piece])
			    && ((!invert_colors &&
				 (tb->mobile_piece_color[piece] == futurebase->mobile_piece_color[future_piece]))
				|| (invert_colors &&
				    (tb->mobile_piece_color[piece] != futurebase->mobile_piece_color[future_piece])))) {
			    piece_vector ^= (1 << piece);
			    break;
			}
		    }
		    if (piece == tb->num_mobiles) {
			if ((promoted_piece == -1) && (futurebase->mobile_piece_type[future_piece] != PAWN)) {
			    promoted_piece = future_piece;
			} else {
			    fprintf(stderr, "'%s': Couldn't find future piece in tablebase\n", filename);
			    return -1;
			}
		    }
		}

		/* The only thing left unaccounted for in the current tablebase should be a pawn. */

		for (piece = 0; piece < tb->num_mobiles; piece ++) {
		    if ((tb->mobile_piece_type[piece] == PAWN) && (piece_vector & (1 << piece))) break;
		}

		if (piece == tb->num_mobiles) {
		    fprintf(stderr, "'%s': No extra pawn in tablebase\n", filename);
		    return -1;
		}

		piece_vector ^= (1 << piece);

		if (piece_vector != 0) {
		    fprintf(stderr, "'%s': Too many extra mobile pieces in futurebase\n", filename);
		    return -1;
		}

		/* Ready to go.
		 *
		 * I use the color of 'piece' (the pawn) here because the futurebase might have
		 * colors inverted.
		 */

		promoted_piece_char = global_pieces[tb->mobile_piece_color[piece]][futurebase->mobile_piece_type[promoted_piece]];

		fprintf(stderr, "Back propagating from '%s'\n", (char *) filename);

		propagate_moves_from_promotion_futurebase(tb, futurebase, invert_colors,
							  promoted_piece_char, &mate_in_limit);

	    } else if ((type != NULL) && !strcasecmp((char *) type, "promotion-capture")) {

		/* It's a promotion capture futurebase.  Futurebase should have exactly one less
		 * mobile than the current tablebase, and one of our pawns should have promoted
		 * into something else.  Find the missing piece in the current tablebase, and
		 * determine what the pawn promoted into.
		 */

		/* piece_vector - set a bit for every mobile piece in current tablebase */
		piece_vector = (1 << tb->num_mobiles) - 1;

		for (future_piece = 0; future_piece < futurebase->num_mobiles; future_piece ++) {
		    for (piece = 0; piece < tb->num_mobiles; piece ++) {
			if (! (piece_vector & (1 << piece))) continue;
			if ((tb->mobile_piece_type[piece] == futurebase->mobile_piece_type[future_piece])
			    && ((!invert_colors &&
				 (tb->mobile_piece_color[piece] == futurebase->mobile_piece_color[future_piece]))
				|| (invert_colors &&
				    (tb->mobile_piece_color[piece] != futurebase->mobile_piece_color[future_piece])))) {
			    piece_vector ^= (1 << piece);
			    break;
			}
		    }
		    if (piece == tb->num_mobiles) {
			if ((promoted_piece == -1) && (futurebase->mobile_piece_type[future_piece] != PAWN)) {
			    promoted_piece = future_piece;
			} else {
			    fprintf(stderr, "'%s': Couldn't find future piece in tablebase\n", filename);
			    return -1;
			}
		    }
		}

		/* The only things left unaccounted for in the current tablebase should be
		 * one of our pawns and one of our non-pawns.
		 */

		for (piece = 0; piece < tb->num_mobiles; piece ++) {
		    if ((tb->mobile_piece_type[piece] == PAWN) && (piece_vector & (1 << piece))) break;
		}

		if (piece == tb->num_mobiles) {
		    fprintf(stderr, "'%s': No extra pawn in tablebase\n", filename);
		    return -1;
		}

		piece_vector ^= (1 << piece);

		for (piece = 0; piece < tb->num_mobiles; piece ++) {
		    if ((tb->mobile_piece_type[piece] != PAWN) && (piece_vector & (1 << piece))) break;
		}

		if (piece == tb->num_mobiles) {
		    fprintf(stderr, "'%s': No captured non-pawn in tablebase\n", filename);
		    return -1;
		}

		piece_vector ^= (1 << piece);

		if (piece_vector != 0) {
		    fprintf(stderr, "'%s': Too many extra mobile pieces in futurebase\n", filename);
		    return -1;
		}

		/* Ready to go.
		 *
		 * I use the color of 'piece' (the captured piece) here, and invert it, because the
		 * futurebase might have colors inverted.
		 */
		/* XXX this "BLACK -" business has to go */


		promoted_piece_char = global_pieces[BLACK - tb->mobile_piece_color[piece]][futurebase->mobile_piece_type[promoted_piece]];
		captured_piece_char = global_pieces[tb->mobile_piece_color[piece]][tb->mobile_piece_type[piece]];

		fprintf(stderr, "Back propagating from '%s'\n", (char *) filename);

		propagate_moves_from_promotion_capture_futurebase(tb, futurebase, invert_colors,
								  promoted_piece_char,
								  captured_piece_char,
								  &mate_in_limit);

	    } else {

		fprintf(stderr, "'%s': Unknown back propagation type (%s)\n",
			(char *) filename, (char *) type);
		return -1;

	    }
	}
    }

    xmlXPathFreeContext(context);

    return mate_in_limit;

}

/* I really want to improve this function's handling of move restrictions.  I want to explicitly
 * state in the XML config which moves are being restricted, rather than the current catch-all.
 */

boolean have_all_futuremoves_been_handled(tablebase *tb) {

    int32 max_index_static = max_index(tb);
    int32 index;
    int all_futuremoves_handled = 1;

    for (index = 0; index < max_index_static; index ++) {
	if (tb->entries[index].futuremove_cnt != 0) {
	    switch (tb->move_restrictions[index_to_side_to_move(tb, index)]) {

	    case RESTRICTION_NONE:
		{
		    global_position_t global;
		    index_to_global_position(tb, index, &global);
		    all_futuremoves_handled = 0;
		}
		break;

	    case RESTRICTION_DISCARD:
		/* discard - we discard any unhandled futuremoves this side might have */
		tb->entries[index].movecnt -= tb->entries[index].futuremove_cnt;
		break;

	    case RESTRICTION_CONCEDE:
		/* concede - we treat any unhandled futuremoves as forced wins for this side */
		PTM_wins(tb, index, 1, 1);
		break;
	    }
	}
    }

    return all_futuremoves_handled;
}


/***** INTRA-TABLE MOVE PROPAGATION *****/

/* This is the guts of the program here.  We've got a move that needs to be propagated,
 * so we back out one half-move to all of the positions that could have gotten us
 * here and update their counters in various obscure ways.
 */

void propagate_one_move_within_table(tablebase *tb, int32 parent_index, local_position_t *current_position)
{
    int32 current_index;

    /* local_position_to_index() only requires the square numbers of the pieces, not the board
     * vectors.
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

    current_index = local_position_to_index(tb, current_position);

    if (current_index == -1) {
	fprintf(stderr, "Can't lookup position in intratable propagation!\n");
    }

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

void propagate_move_within_table(tablebase *tb, int32 parent_index, int mate_in_count)
{
    local_position_t parent_position;
    local_position_t current_position; /* i.e, last position that moved to parent_position */
    int piece;
    int dir;
    struct movement *movementptr;

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

	/* If there are any en passant capturable pawns in the position, then the last move had to
	 * have been a pawn move.  In fact, in this case, we already know exactly what the last move
	 * had to have been.
	 */

	/* forall possible_moves(current_position, piece) { */

	if (tb->mobile_piece_type[piece] != PAWN) {

	    for (dir = 0; dir < number_of_movement_directions[tb->mobile_piece_type[piece]]; dir++) {

		/* What about captures?  Well, first of all, there are no captures here!  We're
		 * moving BACKWARDS in the game... and pieces don't appear out of thin air.
		 * Captures are handled by back-propagation from futurebases, not here in the
		 * movement code.  The piece moving had to come from somewhere, and that somewhere
		 * will now be an empty square, so once we've hit another piece along a movement
		 * vector, there's absolutely no need to consider anything further.
		 */

		for (movementptr
			 = movements[tb->mobile_piece_type[piece]][parent_position.mobile_piece_position[piece]][dir];
		     (movementptr->vector & parent_position.board_vector) == 0;
		     movementptr++) {

		    /* XXX can we move the next two statements out of this loop (ditto below)? */

		    current_position = parent_position;

		    /* Back stepping a half move here involves several things: flipping the
		     * side-to-move flag, clearing any en passant pawns into regular pawns, moving
		     * the piece (backwards), and considering a bunch of additional positions
		     * identical to the base position except that a single one of the pawns on the
		     * fourth or fifth ranks was capturable en passant.
		     *
		     * Of course, the only way we could have gotten an en passant pawn is if THIS
		     * MOVE created it.  Since this isn't a pawn move, that can't happen.
		     */

		    flip_side_to_move_local(&current_position);

		    current_position.mobile_piece_position[piece] = movementptr->square;

		    propagate_one_move_within_table(tb, parent_index, &current_position);
		}
	    }

	} else {

	    /* Usual special case for pawns */

	    for (movementptr = normal_pawn_movements_bkwd[parent_position.mobile_piece_position[piece]][tb->mobile_piece_color[piece]];
		 (movementptr->vector & parent_position.board_vector) == 0;
		 movementptr++) {

		/* Do we have a backwards pawn move here?
		 *
		 * Back stepping a half move here involves several things: flipping the
		 * side-to-move flag, clearing any en passant pawns into regular pawns, moving
		 * the piece (backwards), and considering a bunch of additional positions
		 * identical to the base position except that a single one of the pawns on the
		 * fourth or fifth ranks was capturable en passant.
		 *
		 * Of course, the only way we could have gotten an en passant pawn is if THIS MOVE
		 * created it.  We handle that as a special case above, so we shouldn't have to
		 * worry about clearing en passant pawns here - there should be none.
		 */

		current_position = parent_position;

		flip_side_to_move_local(&current_position);

		current_position.mobile_piece_position[piece] = movementptr->square;

		propagate_one_move_within_table(tb, parent_index, &current_position);

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
 * 2. non-capture moves of the frozen pieces and capture/promotion moves
 *
 * These always lead to a different tablebase (a futurebase).  The only way we handle them is
 * through inter-table back propagation.  We keep a seperate count of these moves (the
 * "futuremoves"), because, unlike non-capture moves of mobile pieces, we might miss some of these
 * moves if we don't have a complete set of futurebases.  So we count futuremoves by themselves (as
 * well as part of the standard count), and count them down normally during a single sweep through
 * our futurebases.  If that takes care of everything fine.  Otherwise, during our first pass
 * through the current tablebase, we'll find that some of the futuremoves remain unaccounted for.
 * If they occur with the "good guys" as PTM, we just double-check that the restriction is OK,
 * subtract the remaining futuremoves out from the standard count, and keep going.  But if the "bad
 * guys" are PTM, then some more work is needed.  The position is marked won for PTM, unless we want
 * to step forward another half move.  In this case, we compute all possible next moves (or maybe
 * just captures), and search for them in our tablebases.  If any of them are marked drawn or won,
 * we can safely back-propagate this.  Otherwise, the position has to be marked won for PTM, as
 * before.
 *
 * There's a real serious speed penalty here, because this half-move-forward algorithm requires
 * random access lookups in the futurebases.  A possible way to address this would be to create an
 * intermediate tablebase for the half move following the capture/promotion.  This could be done by
 * building a tablebase with a queen (and another one with a knight) frozen on the queening square.
 * Any possible move of the queen or knight would result in a win for the moving side.  A similar
 * shortcut could be done for a capture, though the only real justification (from a performance
 * perspective) would be on promotions.
 *
 */

initialize_tablebase(tablebase *tb)
{
    local_position_t position;
    int32 index;
    int piece;
    int dir;
    struct movement *movementptr;

    /* This is here because we don't want to be calling max_index() everytime through the loop below */

    int32 max_index_static = max_index(tb);

    for (index=0; index < max_index_static; index++) {

	if (! index_to_local_position(tb, index, &position)) {

	    initialize_index_as_illegal(tb, index);

	} else {

	    /* Now we need to count moves.  FORWARD moves. */
	    int movecnt = 0;
	    int futuremove_cnt = 0;

	    /* En passant:
	     *
	     * We're just counting moves here.  In particular, we don't compute the indices of the
	     * resulting positions.  If we did, we'd have to worry about clearing en passant status
	     * from any of fourth or fifth rank pawns, but we don't have to worry about it.
	     *
	     * We do have to count one or two possible extra en passant pawn captures, though...
	     */


	    for (piece = 0; piece < tb->num_mobiles; piece++) {

		/* We only want to consider pieces of the side which is to move... */

		if (tb->mobile_piece_color[piece] != position.side_to_move)
		    continue;

		if (tb->mobile_piece_type[piece] != PAWN) {

		    for (dir = 0; dir < number_of_movement_directions[tb->mobile_piece_type[piece]]; dir++) {

			for (movementptr = movements[tb->mobile_piece_type[piece]][position.mobile_piece_position[piece]][dir];
			     (movementptr->vector & position.board_vector) == 0;
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

			if (position.side_to_move == WHITE) {
			    if ((movementptr->vector & position.white_vector) == 0) {
				movecnt ++;
				futuremove_cnt ++;
				if (movementptr->square == position.mobile_piece_position[BLACK_KING]) {
				    initialize_index_with_black_mated(tb, index);
				    goto mated;
				}
			    }
			} else {
			    if ((movementptr->vector & position.black_vector) == 0) {
				movecnt ++;
				futuremove_cnt ++;
				if (movementptr->square == position.mobile_piece_position[WHITE_KING]) {
				    initialize_index_with_white_mated(tb, index);
				    goto mated;
				}
			    }
			}
		    }

		} else {

		    /* Pawns, as always, are special */

		    for (movementptr = normal_pawn_movements[position.mobile_piece_position[piece]][tb->mobile_piece_color[piece]];
			 (movementptr->vector & position.board_vector) == 0;
			 movementptr++) {

			/* If the piece is a pawn and we're moving to the last rank, then this has
			 * to be a promotion move, in fact, PROMOTION_POSSIBILITIES moves.  (queen,
			 * knight, maybe rook and bishop).  As such, they will require back
			 * propagation from futurebases and must therefore be flagged as
			 * futuremoves.
			 */

			if ((ROW(movementptr->square) == 7) || (ROW(movementptr->square) == 0)) {

			    futuremove_cnt += PROMOTION_POSSIBILITIES;
			    movecnt += PROMOTION_POSSIBILITIES;

			} else {

			    movecnt ++;

			}

		    }


		    /* Pawn captures.
		     *
		     * In this part of the code, we're just counting forward moves, and all captures
		     * are futurebase moves, so the only difference to us whether this is a
		     * promotion move or not is how many futuremoves get recorded.
		     */

#define ENEMY_BOARD_VECTOR(pos) ((tb->mobile_piece_color[piece] == WHITE) ? pos.black_vector : pos.white_vector)

		    for (movementptr = capture_pawn_movements[position.mobile_piece_position[piece]][tb->mobile_piece_color[piece]];
			 movementptr->square != -1;
			 movementptr++) {

			/* This is where we'll need to check for en passant captures.  Something
			 * simple like "if (movementptr->square == enPassantSquare)"
			 */

			if ((movementptr->vector & ENEMY_BOARD_VECTOR(position)) == 0) continue;

			/* Same check as above for a mated situation */

			if (position.side_to_move == WHITE) {
			    if (movementptr->square == position.mobile_piece_position[BLACK_KING]) {
				initialize_index_with_black_mated(tb, index);
				goto mated;
			    }
			} else {
			    if (movementptr->square == position.mobile_piece_position[WHITE_KING]) {
				initialize_index_with_white_mated(tb, index);
				goto mated;
			    }
			}

			/* If the piece is a pawn and we're moving to the last rank, then this has
			 * to be a promotion move, in fact, PROMOTION_POSSIBILITIES moves.  (queen,
			 * knight, maybe rook and bishop).  As such, they will require back
			 * propagation from futurebases and must therefore be flagged as
			 * futuremoves.
			 */

			if ((ROW(movementptr->square) == 7) || (ROW(movementptr->square) == 0)) {

			    futuremove_cnt += PROMOTION_POSSIBILITIES;
			    movecnt += PROMOTION_POSSIBILITIES;

			} else {

			    movecnt ++;
			    futuremove_cnt ++;

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

void propagate_all_moves_within_tablebase(tablebase *tb, int mate_in_limit)
{
    int moves_to_win;
    int progress_made;
    int32 max_index_static;
    int32 index;

    /* First we look for forced mates... */

    moves_to_win = 0;
    progress_made = 1;
    max_index_static = max_index(tb);

    while (progress_made || moves_to_win <= mate_in_limit) {
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

int EGTBProbe(int wtm, unsigned char board[64], int *score);

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

char * nalimov_to_english(int score)
{
    static char buffer[256];

    if (score > 0) {
	sprintf(buffer, "mate in %d", ((65536-4)/2)-score+1);
    } else if (score < 0) {
	sprintf(buffer, "mated in %d", ((65536-4)/2)+score);
    } else {
	sprintf(buffer, "draw");
    }

    return buffer;
}

void verify_tablebase_against_nalimov(tablebase *tb)
{
    int32 index;
    int32 max_index_static = max_index(tb);
    global_position_t global;
    int score;

    fprintf(stderr, "Verifying tablebase against Nalimov\n");

    for (index = 0; index < max_index_static; index++) {
	if (index_to_global_position(tb, index, &global)) {
	    if (! is_position_valid(tb, index)) {

		/* I've learned the hard way not to probe a Nalimov tablebase for an illegal position... */

	    } else if (EGTBProbe(global.side_to_move == WHITE, global.board, &score) == 1) {

		if (tb->entries[index].movecnt == PTM_WINS_PROPAGATION_DONE) {
		    /* Make sure mate_in_cnt is greater than zero here, since the Nalimov tablebase
		     * doesn't appear to handle illegal positions.  PTM wins in 0 would mean that
		     * PNTM is in check, so the king can just be captured.
		     */
		    if (tb->entries[index].mate_in_cnt > 0) {
			if ((tb->entries[index].mate_in_cnt/2) != ((65536-4)/2)-score+1) {

			    printf("%s (%d): Nalimov says %s (%d), but we say mate in %d\n",
				   global_position_to_FEN(&global), index,
				   nalimov_to_english(score), score, tb->entries[index].mate_in_cnt/2);
			}
		    }
		} else if (tb->entries[index].movecnt == PNTM_WINS_PROPAGATION_DONE) {
		    if ((tb->entries[index].mate_in_cnt/2) != ((65536-4)/2)+score) {

			printf("%s (%d): Nalimov says %s (%d), but we say mated in %d\n",
			       global_position_to_FEN(&global), index,
			       nalimov_to_english(score), score, tb->entries[index].mate_in_cnt/2);
		    }
		} else {
		    if (score != 0) {

			printf("%s (%d): Nalimov says %s (%d), but we say draw\n",
			       global_position_to_FEN(&global), index,
			       nalimov_to_english(score), ((65536-4)/2)+score);
		    }
		}
	    } else {
		if (((tb->entries[index].movecnt != PTM_WINS_PROPAGATION_DONE)
		     && (tb->entries[index].movecnt != PTM_WINS_PROPAGATION_DONE))
		    || tb->entries[index].mate_in_cnt != 0) {

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
    unsigned char promotion_piece = '\0';

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

    if (movestr[0] == '=') {
	movestr ++;
	promotion_piece = movestr[0];
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

    global->board[destination_square] = promotion_piece ? promotion_piece : global->board[origin_square];
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
    tablebase *tb, **tbs;
    global_position_t global_position;
    boolean global_position_valid = 0;
    int argi;
    int i;
    int c;
    int generating=0;
    int probing=0;
    int verify=0;
    char *output_filename = NULL;
    extern char *optarg;
    extern int optind;

    bzero(tbs, sizeof(tbs));

    init_movements();
    verify_movements();

    while (1) {
	c = getopt(argc, argv, "gpvo:");

	if (c == -1) break;

	switch (c) {

	case 'g':
	    generating = 1;
	    break;
	case 'p':
	    probing = 1;
	    break;
	case 'v':
	    verify = 1;
	    break;
	case 'o':
	    output_filename = optarg;
	    break;
	}
    }

    if (generating && probing) {
	fprintf(stderr, "Only one of the generating (-g) and probing (-p) options can be specified\n");
	exit(0);
    }

    if (!generating && !probing && !verify) {
	fprintf(stderr, "At least one of generating (-g), probing (-p), or verify (-v) must be specified\n");
	exit(0);
    }

    if (generating && (output_filename == NULL)) {
	fprintf(stderr, "An output filename must be specified to generate\n");
	exit(0);
    }

    if (!generating && (output_filename != NULL)) {
	fprintf(stderr, "An output filename can not be specified when probing or verifying\n");
	exit(0);
    }

    if (generating) {
	int mate_in_limit;

	tb = parse_XML_control_file(argv[optind]);

	fprintf(stderr, "Initializing tablebase\n");
	initialize_tablebase(tb);

	mate_in_limit = back_propagate_all_futurebases(tb);

	/* back_propagate_all_futurebases() printed some kind of error message already */
	if (mate_in_limit == -1) exit(1);

	fprintf(stderr, "Checking futuremoves...\n");
	if (have_all_futuremoves_been_handled(tb)) {
	    fprintf(stderr, "All futuremoves handled under move restrictions\n");
	} else {
	    fprintf(stderr, "ERROR: Some futuremoves not handled under move restrictions!\n");
	    exit(1);
	}

	fprintf(stderr, "Intra-table propagating\n");
	propagate_all_moves_within_tablebase(tb, mate_in_limit);

	write_tablebase_to_file(tb, output_filename);

	exit(1);
    }

    /* Probing / Verifying */

    init_nalimov_code();

    i = 0;
    /* calloc (unlike malloc) zeros memory */
    tbs = calloc(argc - optind + 1, sizeof(tablebase *));

    for (argi=optind; argi<argc; argi++) {
	fprintf(stderr, "Loading '%s'\n", argv[argi]);
	tbs[i] = load_futurebase_from_file(argv[argi]);
	if (verify) verify_tablebase_against_nalimov(tbs[i]);
	i++;
    }

    if (!probing) exit(1);

    /* Probing only */

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
		printf("\nNalimov score: ");
		if (EGTBProbe(global_position.side_to_move == WHITE, global_position.board, &score) == 1) {
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
#endif

	    /* Now we want to print a move list */

	    for (piece = 0; piece < tb->num_mobiles; piece++) {

		/* We only want to consider pieces of the side which is to move... */

		if (tb->mobile_piece_color[piece] != global_position.side_to_move)
		    continue;

		if (tb->mobile_piece_type[piece] != PAWN) {

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

			    if ((index2 != -1) && is_position_valid(tb, index2)) {
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

		} else {

		    /* PAWNs */

		    index_to_local_position(tb, index, &pos);
		    nextpos = pos;
		    flip_side_to_move_local(&nextpos);

		    /* normal pawn moves */

		    for (movementptr = normal_pawn_movements[pos.mobile_piece_position[piece]][tb->mobile_piece_color[piece]];
			 (movementptr->vector & pos.board_vector) == 0;
			 movementptr++) {

			if ((ROW(movementptr->square) != 0) && (ROW(movementptr->square) != 7)) {

			    nextpos.mobile_piece_position[piece] = movementptr->square;

			    index2 = local_position_to_index(tb, &nextpos);

			    /* This is the next move, so we reverse the sense of PTM and PNTM */

			    if ((index2 != -1) && is_position_valid(tb, index2)) {
				printf("   %s%s ",
				       algebraic_notation[pos.mobile_piece_position[piece]],
				       algebraic_notation[movementptr->square]);
				print_score(tb, index2, pntm, ptm);
			    }

			} else {

			    /* non-capture promotion */

			    tablebase *tb2;
			    global_position_t reversed_position;
			    int *promoted_piece;
			    int promoted_pieces[] = {QUEEN, ROOK, KNIGHT, 0};

			    index_to_global_position(tb, index, &global_capture_position);

			    flip_side_to_move_global(&global_capture_position);

			    global_capture_position.board[pos.mobile_piece_position[piece]] = 0;

			    for (promoted_piece = promoted_pieces; *promoted_piece; promoted_piece ++) {

				place_piece_in_global_position(&global_capture_position, movementptr->square,
							       tb->mobile_piece_color[piece],
							       *promoted_piece);

				reversed_position = global_capture_position;
				invert_colors_of_global_position(&reversed_position);

				if (search_tablebases_for_global_position(tbs, &global_capture_position,
									  &tb2, &index2)
				    || search_tablebases_for_global_position(tbs, &reversed_position,
									     &tb2, &index2)) {

				    if (is_position_valid(tb2, index2)) {
					printf ("   %s%s=%c ",
						algebraic_notation[pos.mobile_piece_position[piece]],
						algebraic_notation[movementptr->square],
						piece_char[*promoted_piece]);
					print_score(tb2, index2, pntm, ptm);
				    }
				} else {
				    printf("   %s%s=%c NO DATA\n",
					   algebraic_notation[pos.mobile_piece_position[piece]],
					   algebraic_notation[movementptr->square],
					   piece_char[*promoted_piece]);
				}
			    }
			}
		    }



		}

	    }
	}
    }
    write_history(".hoffman_history");
    printf("\n");
}
