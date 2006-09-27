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
 *
 * Usage: hoffman -g -o <output-tablebase> <xml-control-file>     (generate mode)
 *        hoffman -v <tablebase> ...                              (verification mode)
 *        hoffman -p <tablebase> ...                              (probe mode)
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


#define ROW(square) ((square) / 8)
#define COL(square) ((square) % 8)

inline int square(int row, int col)
{
    return (col + row*8);
}

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

/* seven possible pieces: KQRBNP; 64 possible squares, up to 8 directions per piece, up to 7
 * movements in one direction
 */

#define NUM_PIECES 6
#define NUM_SQUARES 64
#define NUM_DIR 8
#define NUM_MOVEMENTS 7


/***** DATA STRUCTURES *****/

/* position - the data structures that represents a board position
 *
 * There are two kinds of positions: local and global.  Locals are faster but are tied to a specific
 * tablebase.  Globals are more general and are used to translate between different tablebases.
 *
 * Both types use a 64-bit board_vector with one bit for each board position, in addition to a flag
 * to indicate which side is to move and the en passant capture square (or -1 if no en passant
 * capture is possible).  We use board_vector to easily check if possible moves are legal by looking
 * for pieces that block our moving piece.  This is done during futurebase propagation (mobile
 * capture only), during intratable propagation, and during initialization.  It could be used to
 * check if en passant positions are legal (are the two squares behind the pawn blocked or not), but
 * that is problematic now because the board_vector isn't correct at the point where we need to
 * make that check.
 *
 * Local positions use numbers (0-63) indicating the positions of the mobile pieces, and also have a
 * quick way to check captures using a black_vector and a white_vector.  You have to look into the
 * tablebase structure to figure out what piece corresponds to each number.  "black_vector" and
 * "white_vector" are only used during tablebase initialization and in the probe code.  It's
 * starting to look like these vectors would be better arranged as PTM_vector and PNTM_vector
 * (player to move and player not to move).
 *
 * It makes sense to include these vectors in the position structures because it's easiest to
 * compute them in the routines that convert indices to positions, but if you alter the position,
 * then they get out of sync, and its tempting to just leave them that way because you rarely need
 * them to be right at that point.  This really came back to haunt me when implementing en passant.
 *
 * Global positions contain an 8x8 unsigned char array with ASCII characters representing each
 * piece.
 *
 * Sometimes I allow the board vectors, black_vector and white_vector to get out of sync with the
 * position (for speed).  This can be a problem, so it has to be done really carefully.
 *
 * We don't worry about moving a piece that's pinned on our king, for example.  The resulting
 * position will already have been flagged illegal in the table.
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
    short en_passant_square;
    short piece_position[MAX_MOBILES];
} local_position_t;

/* This is a global position, that doesn't depend on a particular tablebase.  It's slower to
 * manipulate, but is suitable for translating positions between tablebases.  Each char in the array
 * is either 0 or ' ' for an empty square, and one of the FEN characters for a chess piece.
 */

typedef struct {
    unsigned char board[64];
    int64 board_vector;
    short side_to_move;
    short en_passant_square;
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

char * piece_name[NUM_PIECES+1] = {"KING", "QUEEN", "ROOK", "BISHOP", "KNIGHT", "PAWN", NULL};
char piece_char[NUM_PIECES+1] = {'K', 'Q', 'R', 'B', 'N', 'P', 0};

char * colors[3] = {"WHITE", "BLACK", NULL};

unsigned char global_pieces[2][NUM_PIECES] = {{'K', 'Q', 'R', 'B', 'N', 'P'},
					      {'k', 'q', 'r', 'b', 'n', 'p'}};

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
    void *fileptr;
    size_t length;
    int num_mobiles;
    int move_restrictions[2];		/* one for each color */
    short piece_type[MAX_MOBILES];
    short piece_color[MAX_MOBILES];
    int64 piece_legal_squares[MAX_MOBILES];
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
	    xmlChar * location;
	    color = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "color");
	    type = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "type");
	    location = xmlGetProp(result->nodesetval->nodeTab[i], (const xmlChar *) "location");
	    tb->piece_color[i] = find_name_in_array((char *) color, colors);
	    tb->piece_type[i] = find_name_in_array((char *) type, piece_name);

	    if (location == NULL) {
		tb->piece_legal_squares[i] = allones_bitvector;
	    } else if ((location[0] >= 'a') && (location[0] <= 'h')
		       && (location[1] >= '1') && (location[1] <= '8') && (location[2] == '\0')) {
		tb->piece_legal_squares[i] = BITVECTOR(square(location[1] - '1', location[0] - 'a'));
	    } else {
		fprintf(stderr, "Illegal location (%s) in mobile\n", location);
	    }

	    if ((tb->piece_color[i] == -1) || (tb->piece_type[i] == -1)) {
		fprintf(stderr, "Illegal color (%s) or type (%s) in mobile\n", color, type);
		xmlFree(color);
	    }
	}
    }

    if ((tb->piece_color[WHITE_KING] != WHITE) || (tb->piece_type[WHITE_KING] != KING)
	|| (tb->piece_color[BLACK_KING] != BLACK) || (tb->piece_type[BLACK_KING] != KING)) {
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

    tb->fileptr = fileptr;
    tb->length = filestat.st_size;

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

void unload_futurebase(tablebase *tb)
{
    if (tb->fileptr != NULL) munmap(tb->fileptr, tb->length);
    tb->fileptr = NULL;
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
		   (const xmlChar *) colors[tb->piece_color[piece]]);
	xmlNewProp(mobile, (const xmlChar *) "type",
		   (const xmlChar *) piece_name[tb->piece_type[piece]]);
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
    xmlNewProp(node, (const xmlChar *) "version", (const xmlChar *) "$Revision: 1.74 $");

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


/***** INDICES AND POSITIONS *****/

int32 max_index(tablebase *tb)
{
    return (2<<(6*tb->num_mobiles)) - 1;
}

int32 local_position_to_index(tablebase *tb, local_position_t *pos)
{
    /* This function, given a local board position, returns an index into the tablebase.
     *
     * Initially, this function can be very simple (multiplying numbers together), but to build
     * smaller tables it can be more precise.
     *
     * For example, two kings can never be next to each other.  Pieces can never be on top of each
     * other, or on top of static pieces.  The side to move can not be in check.
     *
     * It also updates the position's board_vector (which doesn't have to be valid going in),
     * but not the white_vector or black_vector.  It does this to check for illegal en passant
     * positions.
     *
     * Returns either an index into the table, or -1 (probably) if the position is illegal.
     *
     * What exactly is an illegal position?  Well, for starters, one that index_to_local_position()
     * reports as illegal, because that's the function that initialize_tablebase() uses to figure
     * which positions need to be flagged illegal, and the program screams if you try to back prop
     * into one of them.  Since global_position_to_index() is also used to generate indices during
     * back prop, its idea of illegality also has to match with index_to_local_position().  And
     * since index_to_global_position() is used to decide which futurebase positions to back prop to
     * begin with, its idea of legality also has to consistent.
     *
     * This function is currently used only during back propagation to generate indices, and during
     * probing to convert "next move" positions into indices.
     *
     * When we back propagate a local position from either a futurebase or intratable, we generate
     * en passant positions simply by running through the pawns on the fourth and fifth ranks.  If
     * we don't look then to see if there was a piece behind the "en passant" pawn (that would have
     * prevented it from moving), and we currently don't (correctly) because the board vectors
     * aren't set right, then we have to detect that and return -1 here.  Otherwise, we would try to
     * back propagate to a position that had been labeled illegal during initialization by
     * index_to_local_position().
     *
     * This function is also used during probing to see if possible next positions are legal.
     */

    /* Keep it simple, for now */

    int shift_count = 1;
    int32 index = pos->side_to_move;  /* WHITE is 0; BLACK is 1 */
    int piece;

    pos->board_vector = 0;

    for (piece = 0; piece < tb->num_mobiles; piece ++) {
	/* I've added this pawn check because I've had some problems.  This makes the
	 * return of this function match up with the return of index_to_global_position
	 */
	if ((tb->piece_type[piece] == PAWN)
	    && ((pos->piece_position[piece] < 8) || (pos->piece_position[piece] >= 56))) {
	    return -1;
	}
	if (pos->piece_position[piece] < 0)
	    fprintf(stderr, "Bad mobile piece position in local_position_to_index()\n");  /* BREAKPOINT */

	/* The way we encode en passant capturable pawns is use the column number of the
	 * pawn.  Since there can never be a pawn (of either color) on the first rank,
	 * this is completely legit.
	 */
	if ((tb->piece_type[piece] == PAWN) && (pos->en_passant_square != -1)
	    && (((tb->piece_color[piece] == WHITE)
		 && (pos->en_passant_square + 8 == pos->piece_position[piece]))
		|| ((tb->piece_color[piece] == BLACK)
		    && (pos->en_passant_square - 8 == pos->piece_position[piece])))) {
	    index |= COL(pos->en_passant_square) << shift_count;
	} else {
	    index |= pos->piece_position[piece] << shift_count;
	}
	if (pos->board_vector & BITVECTOR(pos->piece_position[piece])) return -1;
	pos->board_vector |= BITVECTOR(pos->piece_position[piece]);

	shift_count += 6;  /* because 2^6=64 */
    }

    /* Check board_vector to make sure an en passant position is legal */

    if (pos->en_passant_square != -1) {
	if (pos->board_vector & BITVECTOR(pos->en_passant_square)) return -1;
	if (pos->side_to_move == WHITE) {
	    if (pos->board_vector & BITVECTOR(pos->en_passant_square + 8)) return -1;
	} else {
	    if (pos->board_vector & BITVECTOR(pos->en_passant_square - 8)) return -1;
	}
    }

    /* Possibly a quicker check for position legality that all that en passant stuff */

    if (tb->entries[index].movecnt == ILLEGAL_POSITION) return -1;

    return index;
}

/* Like local_position_to_index(), this routine updates the position's board vector
 * so it can check en passant legality.
 *
 * Used during futurebase back-propagation.  Same problem there with the possibility of generating
 * illegal en passant positions.
 *
 * The only other place this function is currently used is in the probe code, when we have parsed
 * FEN into a global position and are searching for it.  There, we count on this routine returning
 * -1 for positions that aren't handled by the current tablebase.
 */

int32 global_position_to_index(tablebase *tb, global_position_t *position)
{
    int32 index = position->side_to_move;  /* WHITE is 0; BLACK is 1 */
    int piece;
    int square;
    short pieces_processed_bitvector = 0;

    position->board_vector = 0;

    for (square = 0; square < NUM_SQUARES; square ++) {
	if ((position->board[square] != 0) && (position->board[square] != ' ')) {
	    for (piece = 0; piece < tb->num_mobiles; piece ++) {
		if ((position->board[square]
		     == global_pieces[tb->piece_color[piece]][tb->piece_type[piece]])
		    && !(pieces_processed_bitvector & (1 << piece))) {

		    if ((tb->piece_type[piece] == PAWN) && (position->en_passant_square != -1)
			&& (((tb->piece_color[piece] == WHITE)
			     && (position->en_passant_square + 8 == square))
			    || ((tb->piece_color[piece] == BLACK)
				&& (position->en_passant_square - 8 == square)))) {
			index |= COL(position->en_passant_square) << (1 + 6*piece);
		    } else {
			index |= square << (1 + 6*piece);
		    }

		    position->board_vector |= BITVECTOR(square);

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

    /* Check board_vector to make sure an en passant position is legal */

    if (position->en_passant_square != -1) {
	if (position->board_vector & BITVECTOR(position->en_passant_square)) return -1;
	if (position->side_to_move == WHITE) {
	    if (position->board_vector & BITVECTOR(position->en_passant_square + 8)) return -1;
	} else {
	    if (position->board_vector & BITVECTOR(position->en_passant_square - 8)) return -1;
	}
    }

    /* Possibly a quicker check for position legality that all that en passant stuff */

    if (tb->entries[index].movecnt == ILLEGAL_POSITION) return -1;

    return index;
}

boolean index_to_local_position(tablebase *tb, int32 index, local_position_t *p)
{
    /* Given an index, fill in a board position.  Obviously has to correspond to local_position_to_index()
     * and it's a big bug if it doesn't.  The boolean that gets returned is TRUE if the operation
     * succeeded (the index is at least minimally valid) and FALSE if the index is so blatantly
     * illegal (two pieces on the same square) that we can't even fill in the position.
     *
     * Used by index_to_side_to_move() (which is currently only use to check move restrictions) to
     * determine position legality.
     *
     * Used in propagate_move_within_table() to get from an index to a position, and its return
     * value is ignored there, because a position that needs_propagation() couldn't have been marked
     * illegal during initialization.
     *
     * Used in initialize_tablebase() to determine which positions are legal.
     *
     * Use to prepare a move list during probe.
     *
     * So how about a static 64-bit vector with bits set for the frozen pieces but not the mobiles?
     * Everytime we call index_to_local_position, copy from the static vector into the position
     * structure.  Then we compute the positions of the mobile pieces and plug their bits into the
     * structure's vector at the right places.
     */

    int piece;

    bzero(p, sizeof(local_position_t));
    p->en_passant_square = -1;

    p->side_to_move = index & 1;
    index >>= 1;

    for (piece = 0; piece < tb->num_mobiles; piece++) {

	int square = index & 63;

	/* En passant */
	if ((tb->piece_type[piece] == PAWN) && (square < 8)) {
	    if (p->en_passant_square != -1) return 0;  /* can't have two en passant pawns */
	    if (tb->piece_color[piece] == WHITE) {
		if (p->side_to_move != BLACK) return 0; /* en passant pawn has to be capturable */
		p->en_passant_square = square + 2*8;
		square += 3*8;
	    } else {
		if (p->side_to_move != WHITE) return 0; /* en passant pawn has to be capturable */
		p->en_passant_square = square + 5*8;
		square += 4*8;
	    }
	}

	/* I've added this pawn check because I've had some problems.  This makes the
	 * return of this function match up with the return of index_to_global_position
	 */
	if ((tb->piece_type[piece] == PAWN) && (square >= 56)) {
	    return 0;
	}

	/* The first place we handle restricted pieces, and one of most important, too, because this
	 * function is used during initialization to decide which positions are legal and which are
	 * not.
	 */

	if (!(tb->piece_legal_squares[piece] & BITVECTOR(square))) {
	    return 0;
	}

	p->piece_position[piece] = square;
	if (p->board_vector & BITVECTOR(square)) {
	    return 0;
	}
	p->board_vector |= BITVECTOR(square);
	if (tb->piece_color[piece] == WHITE) {
	    p->white_vector |= BITVECTOR(square);
	} else {
	    p->black_vector |= BITVECTOR(square);
	}
	index >>= 6;
    }

    /* If there is an en passant capturable pawn in this position, then there can't be anything
     * on the capture square or on the square right behind it (where the pawn just came from),
     * or its an illegal position.
     */

    if (p->en_passant_square != -1) {
	if (p->board_vector & BITVECTOR(p->en_passant_square)) return 0;
	if (p->side_to_move == WHITE) {
	    if (p->board_vector & BITVECTOR(p->en_passant_square + 8)) return 0;
	} else {
	    if (p->board_vector & BITVECTOR(p->en_passant_square - 8)) return 0;
	}
    }

    return 1;
}

/* index_to_global_position()
 *
 * Used during promotion, promotion capture, and mobile capture futurebase propagation.  Runs
 * through all the indices of a futurebase, and its return value is used to decide if the index
 * corresponds to a legal position, so its verdict on legal or illegal positions needs to match up
 * with the above functions.
 *
 * Also used during Nalimov tablebase verification in the same way (running through all indices in a
 * tablebase), as well as during probe code to consider possible captures and promotions because
 * they may lead out of the current tablebase.
 *
 * Seems never to be used on a tablebase under construction; only on a finished one.
 */

boolean index_to_global_position(tablebase *tb, int32 index, global_position_t *position)
{
    int piece;

    /* This check somewhat violates principles of code isolation, but since this function is never
     * used on a tablebase under construction, and we do need this to figure out which indices in a
     * futurebase are legit, it seems reasonable to get out of it early with a quick check like
     * this to speed up futurebase propagation.
     *
     * Since this is here, right now I don't bother checking piece restrictions in this function.
     */

    if (tb->entries[index].movecnt == ILLEGAL_POSITION) return 0;

    bzero(position, sizeof(global_position_t));

    position->en_passant_square = -1;
    position->side_to_move = index & 1;
    index >>= 1;

    for (piece = 0; piece < tb->num_mobiles; piece++) {

	int square = index & 63;

	/* There are other possibilities for illegal combinations, namely a king next to the other
	 * king, but that possibility is taken care of with an is_position_valid() check.  I need
	 * this check here to keep my Nalimov verification routine from screaming about pawns on the
	 * eighth rank.
	 */

	/* En passant */
	if ((tb->piece_type[piece] == PAWN) && (square < 8)) {
	    if (position->en_passant_square != -1) return 0;  /* can't have two en passant pawns */
	    if (tb->piece_color[piece] == WHITE) {
		if (position->side_to_move != BLACK) return 0; /* en passant pawn has to be capturable */
		position->en_passant_square = square + 2*8;
		square += 3*8;
	    } else {
		if (position->side_to_move != WHITE) return 0; /* en passant pawn has to be capturable */
		position->en_passant_square = square + 5*8;
		square += 4*8;
	    }
	}

	if (position->board[square] != 0) {
	    return 0;
	}

	if ((tb->piece_type[piece] == PAWN) && (square >= 56)) {
	    return 0;
	}

	position->board[square]
	    = global_pieces[tb->piece_color[piece]][tb->piece_type[piece]];

	position->board_vector |= BITVECTOR(square);

	index >>= 6;
    }

    /* If there is an en passant capturable pawn in this position, then there can't be anything
     * on the capture square or on the square right behind it (where the pawn just came from),
     * or its an illegal position.
     */

    if (position->en_passant_square != -1) {
	if (position->board_vector & BITVECTOR(position->en_passant_square)) return 0;
	if (position->side_to_move == WHITE) {
	    if (position->board_vector & BITVECTOR(position->en_passant_square + 8)) return 0;
	} else {
	    if (position->board_vector & BITVECTOR(position->en_passant_square - 8)) return 0;
	}
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

    if (global->side_to_move == WHITE) {
	global->side_to_move = BLACK;
	if (global->en_passant_square != -1) global->en_passant_square -= 3*8;
    } else {
	global->side_to_move = WHITE;
	if (global->en_passant_square != -1) global->en_passant_square += 3*8;
    }
}

/* This function works a little bit different from taking a global position, calling
 * global_position_to_index(), and then calling index_to_local_position().  It is used during
 * back-propagation of capture positions, and will leave a single piece in the local position
 * unassigned if it wasn't in the global position.  It is also used during back-propagation of
 * normal futurebases, when the single piece unassigned is in a restricted position, according to
 * the current tablebase.  In any event, returns the piece number of the unassigned piece, or -1 if
 * something went wrong.
 *
 * This thing is really starting to evolve into one of our key, key functions during futurebase back
 * propagation.  There are, right now, three different modes in which it is used.  It can convert a
 * position with one piece completely missing (used during capture processing; returns the missing
 * piece number), it can convert a position with no pieces missing and all on legal squares (used
 * during other types of futurebase back prop; returns -1), or it can convert a position with no
 * pieces missing but one of them on a restricted square (used during normal futurebase back prop;
 * returns restricted piece number).  We also want it to convert positions with one piece missing
 * and one of the other pieces on a restricted square (this is a possibility during capture back
 * prop).
 *
 * The return value is 0x8080 if all pieces converted OK; (0x8000 & missing_piece) if one piece was
 * missing but everything else was OK; ((restricted_piece << 8) | 0x0080) if one piece was on a
 * restricted square but everything else was OK; and ((restricted_piece << 8) | missing_piece) if
 * one piece was missing and one was on a restricted square, and -1 if something went wrong (more
 * than one piece missing and/or more than one piece on a restricted square).
 */

int global_position_to_local_position(tablebase *tb, global_position_t *global, local_position_t *local)
{
    int piece, piece2;
    int restricted_piece = 0x80;
    int missing_piece = 0x80;
    int square;
    short pieces_processed_bitvector = 0;

    bzero(local, sizeof(local_position_t));

    for (piece = 0; piece < tb->num_mobiles; piece ++)
	local->piece_position[piece] = -1;

    local->en_passant_square = global->en_passant_square;
    local->side_to_move = global->side_to_move;

    for (square = 0; square < NUM_SQUARES; square ++) {
	if ((global->board[square] != 0) && (global->board[square] != ' ')) {
	    for (piece = 0; piece < tb->num_mobiles; piece ++) {
		if ((global->board[square] == global_pieces[tb->piece_color[piece]][tb->piece_type[piece]])
		    && !(pieces_processed_bitvector & (1 << piece))) {

		    local->piece_position[piece] = square;
		    local->board_vector |= BITVECTOR(square);
		    if (tb->piece_color[piece] == WHITE)
			local->white_vector |= BITVECTOR(square);
		    else
			local->black_vector |= BITVECTOR(square);

		    pieces_processed_bitvector |= (1 << piece);

		    break;
		}
	    }
	}
    }


    /* Make sure all the pieces but one have been accounted for.  We count a piece as "free" if
     * either it hasn't been processed at all, or if it was processed but was outside its move
     * restriction.
     */

    for (piece = 0; piece < tb->num_mobiles; piece ++) {
	if (!(pieces_processed_bitvector & (1 << piece))) {
	    if (missing_piece == 0x80) missing_piece = piece;
	    else return -1;
	} else if (!(tb->piece_legal_squares[piece] & BITVECTOR(local->piece_position[piece]))) {
	    if (restricted_piece == 0x80) restricted_piece = piece;
	    else return -1;
	}
    }

    return ((restricted_piece << 8) | missing_piece);

#if 0
    if (piece == tb->num_mobiles) {
	/* We might want to function to do exact matches... */
	/* fprintf(stderr, "No free piece in global_position_to_local_position()\n"); */
	return -1;
    }

    for (piece2 = piece+1; piece2 < tb->num_mobiles; piece2 ++) {
	if (!(pieces_processed_bitvector & (1 << piece2))
	    || !(tb->piece_legal_squares[piece2] & BITVECTOR(local->piece_position[piece2]))) {
	    /* This might legitimately happen if the futurebase is more liberal than we are */
	    /* fprintf(stderr, "Multiple free pieces in global_position_to_local_position()\n"); */
	    return -1;
	}
    }

    return piece;
#endif
}


/***** PARSING FEN TO/FROM POSITION STRUCTURES *****/

boolean place_piece_in_local_position(tablebase *tb, local_position_t *pos, int square, int color, int type)
{
    int piece;

    if (pos->board_vector & BITVECTOR(square)) return 0;

    for (piece = 0; piece < tb->num_mobiles; piece ++) {
	if ((tb->piece_type[piece] == type) && (tb->piece_color[piece] == color)) {
	    pos->piece_position[piece] = square;
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
    pos->en_passant_square = -1;

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

    while (*FEN_string == ' ') FEN_string ++;

    /* skip castling rights (if they exist) */

    while ((*FEN_string == '-') || (*FEN_string == 'K') || (*FEN_string == 'Q')
	   || (*FEN_string == 'k') || (*FEN_string == 'q')) FEN_string ++;

    while (*FEN_string == ' ') FEN_string ++;

    /* If en passant square was specified, parse it */

    if ((FEN_string[0] >= 'a') && (FEN_string[0] <= 'h')
	&& (FEN_string[1] >= '1') && (FEN_string[1] <= '8')) {
	pos->en_passant_square = square(FEN_string[1] - '1', FEN_string[0] - 'a');
    }

    return 1;
}

boolean parse_FEN_to_global_position(char *FEN_string, global_position_t *pos)
{
    int row, col;

    bzero(pos, sizeof(global_position_t));
    pos->en_passant_square = -1;

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

    while (*FEN_string == ' ') FEN_string ++;

    /* skip castling rights (if they exist) */

    while ((*FEN_string == '-') || (*FEN_string == 'K') || (*FEN_string == 'Q')
	   || (*FEN_string == 'k') || (*FEN_string == 'q')) FEN_string ++;

    while (*FEN_string == ' ') FEN_string ++;

    /* If en passant square was specified, parse it */

    if ((FEN_string[0] >= 'a') && (FEN_string[0] <= 'h')
	&& (FEN_string[1] >= '1') && (FEN_string[1] <= '8')) {
	pos->en_passant_square = square(FEN_string[1] - '1', FEN_string[0] - 'a');
    }

    return 1;
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

    /* no castling rights */

    *(ptr++) = ' ';
    *(ptr++) = '-';
    *(ptr++) = ' ';

    if (position->en_passant_square == -1) {
	*(ptr++) = '-';
    } else {
	*(ptr++) = 'a' + COL(position->en_passant_square);
	*(ptr++) = '1' + ROW(position->en_passant_square);
    }

    *(ptr++) = '\0';

    return buffer;
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

    global->en_passant_square = -1;

    if ((global->board[destination_square] == 'P') && (origin_square == destination_square - 16)) {
	global->en_passant_square = destination_square - 8;
    }
    if ((global->board[destination_square] == 'p') && (origin_square == destination_square + 16)) {
	global->en_passant_square = destination_square + 8;
    }

    /* XXX doesn't modify board vector */

    return 1;
}


/* MORE TABLEBASE OPERATIONS - those that probe and manipulate individual position entries
 *
 * "Designed to multi-thread"
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
	fprintf(stderr, "Propagation attempt on a completed or unresolved position\n");   /* BREAKPOINT */
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

/* #define DEBUG_MOVE 4784384 */

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
	    fprintf(stderr, "Mate in count dropped in PTM_wins after propagation done!?\n"); /* BREAKPOINT */
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
	fprintf(stderr, "PTM_wins in a position where PNTM already won?!\n");   /* BREAKPOINT */
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
	fprintf(stderr, "add_one_to_PNTM_wins in an already won position!?\n");  /* BREAKPOINT */
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
 * can't be blocked in any of them.  Pawns are handled separately.
 */

int number_of_movement_directions[NUM_PIECES] = {8,8,4,4,8,0};
int maximum_movements_in_one_direction[NUM_PIECES] = {1,7,7,7,1,0};

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

/* Subroutines to backpropagate an individual index, or an individual local (or global) position
 * (these are the "mini" routines), or a set of local (or global) positions that differ
 * only in the en passant square.
 *
 * If we're back propagating from a simple capture, we can use local positions fairly easily.  If
 * we're back propagating from a promotion futurebase (or a promotion capture futurebase), we use
 * global positions, even though they're slower than local positions, because we're translating
 * between two quite different tablebases.  The cleanest (but not necessarily fastest) way to do
 * this is with global positions.
 *
 * The idea behind the en passant handling is this.  If we back propagate a position with the en
 * passant square set, then that's the only position we process.  If we back prop a position without
 * the en passant square set, then we process not only that position, but also any positions just
 * like it that have en passant set.  The idea being that we set en passant if we actually need it,
 * and we clear it if we don't need it, so if it's clear we need to process positions where it was
 * set, but we didn't use it.
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

void propagate_minilocal_position_from_futurebase(tablebase *tb, tablebase *futurebase,
						  int32 future_index, local_position_t *current_position,
						  int *mate_in_limit)
{
    int32 current_index;

    /* Look up the position in the current tablebase... */

    current_index = local_position_to_index(tb, current_position);

    if (current_index == -1) {
	/* This can happen if we don't fully check en passant legality (but right now, we do) */
	fprintf(stderr, "Can't lookup local position in futurebase propagation!\n");  /* BREAKPOINT */
	return;
    }

    propagate_index_from_futurebase(tb, futurebase, future_index, current_index, mate_in_limit);
}

void propagate_local_position_from_futurebase(tablebase *tb, tablebase *futurebase,
					      int32 future_index, local_position_t *position,
					      int *mate_in_limit)
{
    int piece;

    /* We may need to consider a bunch of additional positions here that are identical to the base
     * position except that a single one of the pawns on the fourth or fifth ranks was capturable en
     * passant.
     * 
     * We key off the en_passant flag in the position that was passed in.  If it's set, then we're
     * back propagating a position that requires en passant, so we just do it.  Otherwise, we're
     * back propagating a position that doesn't require en passant, so we check for additional
     * en passant positions.
     */

    propagate_minilocal_position_from_futurebase(tb, futurebase, future_index, position, mate_in_limit);

    if (position->en_passant_square == -1) {

	for (piece = 0; piece < tb->num_mobiles; piece ++) {

	    if (tb->piece_color[piece] == position->side_to_move) continue;
	    if (tb->piece_type[piece] != PAWN) continue;

#if 1
	    /* XXX I've taken care to update board_vector specifically so we can check for en
	     * passant legality here.
	     */

	    if ((tb->piece_color[piece] == WHITE)
		&& (ROW(position->piece_position[piece]) == 3)
		&& !(position->board_vector & BITVECTOR(position->piece_position[piece] - 8))
		&& !(position->board_vector & BITVECTOR(position->piece_position[piece] - 16))) {
		position->en_passant_square = position->piece_position[piece] - 8;
		propagate_minilocal_position_from_futurebase(tb, futurebase, future_index, position, mate_in_limit);
	    }

	    if ((tb->piece_color[piece] == BLACK)
		&& (ROW(position->piece_position[piece]) == 4)
		&& !(position->board_vector & BITVECTOR(position->piece_position[piece] + 8))
		&& !(position->board_vector & BITVECTOR(position->piece_position[piece] + 16))) {
		position->en_passant_square = position->piece_position[piece] + 8;
		propagate_minilocal_position_from_futurebase(tb, futurebase, future_index, position, mate_in_limit);
	    }

#else

	    /* XXX The problem is that the board vector might not be correct, because we moved the
	     * capturing piece without updating it.  We get around this by checking in
	     * local_position_to_index() for illegal en passant positions.
	     */

	    if ((tb->piece_color[piece] == WHITE)
		&& (ROW(position->piece_position[piece]) == 3)) {
		position->en_passant_square = position->piece_position[piece] - 8;
		propagate_minilocal_position_from_futurebase(tb, futurebase, future_index, position, mate_in_limit);
	    }

	    if ((tb->piece_color[piece] == BLACK)
		&& (ROW(position->piece_position[piece]) == 4)) {
		position->en_passant_square = position->piece_position[piece] + 8;
		propagate_minilocal_position_from_futurebase(tb, futurebase, future_index, position, mate_in_limit);
	    }
#endif

	    position->en_passant_square = -1;
	}
    }
}

void propagate_miniglobal_position_from_futurebase(tablebase *tb, tablebase *futurebase,
						   int32 future_index, global_position_t *position, int *mate_in_limit)
{
    int32 current_index;

    /* Look up the position in the current tablebase... */

    current_index = global_position_to_index(tb, position);

    if (current_index == -1) {
	/* This can happen if we don't fully check en passant legality (but right now, we do) */
	fprintf(stderr, "Can't lookup global position in futurebase propagation!\n");  /* BREAKPOINT */
	return;
    }

    propagate_index_from_futurebase(tb, futurebase, future_index, current_index, mate_in_limit);
}


void propagate_global_position_from_futurebase(tablebase *tb, tablebase *futurebase,
					       int32 future_index, global_position_t *position, int *mate_in_limit)
{
#if 0
    /* We may need to consider a bunch of additional positions here that are identical to the base
     * position except that a single one of the pawns on the fourth or fifth ranks was capturable en
     * passant.
     * 
     * We key off the en_passant flag in the position that was passed in.  If it's set, then we're
     * back propagating a position that requires en passant, so we just do it.  Otherwise, we're
     * back propagating a position that doesn't require en passant, so we check for additional
     * en passant positions.
     */

    propagate_miniglobal_position_from_futurebase(tb, futurebase, future_index, position, mate_in_limit);

    if (position->en_passant_square == -1) {

	int starting_square = (position->side_to_move == WHITE) ? 32 : 24;
	int ending_square = (position->side_to_move == WHITE) ? 39 : 31;
	unsigned char pawn = (position->side_to_move == WHITE) ? 'p' : 'P';
	int capture_row = (position->side_to_move == WHITE) ? 5 : 2;
	int back_row1 = (position->side_to_move == WHITE) ? 6 : 1;
	int sq;

	for (sq = starting_square; sq <= ending_square; sq ++) {

	    /* We do a full check for en passant legality here to make the code more robust.  This
	     * way, we should never get an illegal position in the miniglobal propagation Well, an
	     * almost full check.  We don't check to see if an enemy pawn could actually capture the
	     * en passant pawn, but the index/position code currently doesn't treat that situation
	     * as illegal, so we're OK (for now).
	     */

	    if ((position->board[sq] == pawn) && (position->board[square(capture_row, COL(sq))] == 0)
		&& (position->board[square(back_row1, COL(sq))] == 0)) {
		position->en_passant_square = square(capture_row, COL(sq));
		propagate_miniglobal_position_from_futurebase(tb, futurebase, future_index, position, mate_in_limit);
	    }
	}
    }
#else
    local_position_t local;
    int conversion_result;

    conversion_result = global_position_to_local_position(tb, position, &local);

    /* Did we match exactly?  Meaning no free pieces? */

    if (conversion_result == 0x8080) {
	propagate_local_position_from_futurebase(tb, futurebase, future_index, &local, mate_in_limit);
    } else if ((conversion_result & 0x00ff) == 0x0080) {
	/* This might legitimately happen if the futurebase is more liberal than we are */
	/* fprintf(stderr, "Restricted piece during futurebase back-prop\n"); */
    } else {
	fprintf(stderr, "Conversion error during futurebase back-prop\n");
    }

#endif
}

/* Back propagate promotion moves
 *
 * Passed a piece (a global position character) that the pawn is promoting into.  Searches
 * futurebase for positions with that piece on the last rank and back-props.
 */

void propagate_moves_from_promotion_futurebase(tablebase *tb, tablebase *futurebase,
					       int invert_colors_of_futurebase,
					       unsigned char promoted_piece, int pawn,
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

	    if (future_position.en_passant_square != -1) continue;

	    /* We're back-proping one half move to the promotion move. */

	    flip_side_to_move_global(&future_position);

	    /* Consider only positions with the promoted piece on the last rank and with an empty
	     * square right behind where a pawn could have come from.
	     */

	    for (square = first_back_rank_square; square <= last_back_rank_square; square ++) {

		if ((future_position.board[square] == promoted_piece)
		    && !(future_position.board_vector & BITVECTOR(square - promotion_move))
		    && (tb->piece_legal_squares[pawn] & BITVECTOR(square - promotion_move))) {

		    /* Replace the promoted piece with a pawn on the seventh */

		    future_position.board[square] = 0;
		    future_position.board[square - promotion_move]
			= ((promotion_color == WHITE) ? 'P' : 'p');

		    /* Back propagate the resulting position */

		    /* This function also back props any similar positions with one of the pawns
		     * from the side that didn't promote in an en passant state.
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

	    if (future_position.en_passant_square != -1) continue;

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

			/* This function also back props any similar positions with one of the pawns
			 * from the side that didn't promote in an en passant state.
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

			/* This function also back props any similar positions with one of the pawns
			 * from the side that didn't promote in an en passant state.
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

/* Propagate moves from a futurebase that resulted from capturing one of the mobile
 * pieces in the current tablebase.
 *
 * I'm thinking of changing that "invert_colors_of_futurebase" flag to be a subroutine that gets
 * passed in.  It could be a pointer to invert_colors_of_global_position to do what it does now.  Or
 * it could be a "reflect board around vertical axis" to move a d4 pawn to e4.  Also see my comments
 * on invert_colors_of_global position.
 */

void consider_possible_captures(tablebase *tb, tablebase *futurebase, int32 future_index,
				local_position_t *position,
				int capturing_piece, int captured_piece, int *mate_in_limit)
{
    int dir;
    struct movement *movementptr;

    /* We only want to consider pieces of the side which captured... */

    if (tb->piece_color[capturing_piece] == tb->piece_color[captured_piece]) return;

    /* Place the captured piece back into the position on the square from which the moving piece
     * started (i.e, ended) its move.
     *
     * Probably should use place_piece_in_local_position here, but we don't.  The board vectors
     * don't get updated, but that doesn't matter since we're never going to look at the square the
     * captured piece is on as a possible origin square for the capturing piece.
     */

    if ((tb->piece_type[captured_piece] == PAWN)
	&& ((position->piece_position[capturing_piece] < 8)
	    || (position->piece_position[capturing_piece] >= 56))) {

	/* If we "captured" a pawn on the first or eighth ranks, well, obviously that
	 * can't happen...
	 */

	return;
    }


    /* If the square we're about to put the captured piece on isn't legal for it, then
     * don't consider this capturing piece in this future position any more.
     *
     * Probably can wrap the pawn check just above into this code eventually.
     */

    if (!(tb->piece_legal_squares[captured_piece] & BITVECTOR(position->piece_position[capturing_piece]))) {
	return;
    }

    position->piece_position[captured_piece] = position->piece_position[capturing_piece];

    /* We consider all possible backwards movements of the piece which captured. */

    if (tb->piece_type[capturing_piece] != PAWN) {

	for (dir = 0; dir < number_of_movement_directions[tb->piece_type[capturing_piece]]; dir++) {

	    /* Make sure we start each movement of the capturing piece from the capture square */

	    position->piece_position[capturing_piece] = position->piece_position[captured_piece];

	    for (movementptr = movements[tb->piece_type[capturing_piece]][position->piece_position[capturing_piece]][dir];
		 (movementptr->vector & position->board_vector) == 0;
		 movementptr++) {

		/* We already checked that the captured piece was on a legal square
		 * for it.  Now check the capturing piece.
		 */

		if (! (tb->piece_legal_squares[capturing_piece] & movementptr->vector)) continue;

		/* Move the capturing piece... */

		/* I update board_vector here because I want to check for en passant legality before
		 * I call local_position_to_index().  It just makes the code a little more robust at
		 * this point, because then there should be no reason for local_position_to_index()
		 * to return -1.
		 *
		 * By the way, the piece didn't "come from" anywhere other than the capture square,
		 * which will have the captured piece on it (this is back prop), so we don't need to
		 * clear anything in board_vector.
		 */

		position->piece_position[capturing_piece] = movementptr->square;
		position->board_vector |= BITVECTOR(movementptr->square);

		/* This function also back props any similar positions with one of the pawns from
		 * the side that didn't capture in an en passant state.
		 */

		propagate_local_position_from_futurebase(tb, futurebase, future_index, position, mate_in_limit);


		position->board_vector &= ~BITVECTOR(movementptr->square);
	    }
	}

    } else {

	/* Yes, pawn captures are special */

	for (movementptr = capture_pawn_movements_bkwd[position->piece_position[capturing_piece]][tb->piece_color[capturing_piece]];
	     movementptr->square != -1;
	     movementptr++) {

	    /* Is there anything on the square the pawn had to capture from? */

	    if ((movementptr->vector & position->board_vector) != 0) continue;

	    /* Did it come from a legal square for it? */

	    if (! (tb->piece_legal_squares[capturing_piece] & movementptr->vector)) continue;

	    /* non-promotion capture */

	    /* I update board_vector here because I want to check for en passant legality before I
	     * call local_position_to_index().  It just makes the code a little more robust at this
	     * point, because then there should be no reason for local_position_to_index() to return
	     * -1.
	     *
	     * By the way, the piece didn't "come from" anywhere other than the capture square,
	     * which will have the captured piece on it (this is back prop), so we don't need to
	     * clear anything in board_vector.
	     */

	    position->piece_position[capturing_piece] = movementptr->square;
	    position->board_vector |= BITVECTOR(movementptr->square);

	    /* This function also back props any similar positions with one of the pawns from the
	     * side that didn't capture in an en passant state.
	     */

	    propagate_local_position_from_futurebase(tb, futurebase, future_index,
						     position, mate_in_limit);

	    position->board_vector &= ~BITVECTOR(movementptr->square);

	    /* The en passant special case: if both the piece that captured and the piece that was
	     * captured are both pawns, and either a white pawn captured from the fifth rank, or a
	     * black pawn captured from the fourth, then there are two possible back prop positions
	     * - the obvious one we just handled, and the one where the captured pawn was in an en
	     * passant state.  We also make sure right away that the rank is clear where the pawn
	     * had to come from, and the rank is clear where the pawn had to go to, ensuring that an
	     * en passant move was even possible.
	     */

	    if ((tb->piece_type[captured_piece] == PAWN)
		&& !(position->board_vector & BITVECTOR(position->piece_position[captured_piece]-8))
		&& !(position->board_vector & BITVECTOR(position->piece_position[captured_piece]+8))) {

		if ((tb->piece_color[capturing_piece] == BLACK) && (ROW(movementptr->square) == 3)) {

		    /* A black pawn capturing a white one (en passant)
		     *
		     * The white pawn is actually a rank higher than usual.
		     */

		    position->en_passant_square = position->piece_position[captured_piece];
		    position->piece_position[captured_piece] += 8;

		    propagate_local_position_from_futurebase(tb, futurebase, future_index,
							     position, mate_in_limit);

		    /* Yes, we're in a for loop and could might this position again, so put things
		     * back where they came from...
		     */

		    position->en_passant_square = -1;
		    position->piece_position[captured_piece] -= 8;
		}

		if ((tb->piece_color[capturing_piece] == WHITE) && (ROW(movementptr->square) == 4)) {

		    /* A white pawn capturing a black one (en passant)
		     *
		     * The black pawn is actually a rank lower than usual.
		     */

		    position->en_passant_square = position->piece_position[captured_piece];
		    position->piece_position[captured_piece] -= 8;

		    propagate_local_position_from_futurebase(tb, futurebase, future_index,
							     position, mate_in_limit);

		    /* Yes, we're in a for loop and could might this position again, so put things
		     * back where they came from...
		     */

		    position->en_passant_square = -1;
		    position->piece_position[captured_piece] += 8;
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
    int conversion_result;

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

	    if (future_position.en_passant_square != -1) continue;

	    /* Since the position resulted from a capture, we only want to consider future positions
	     * where the side to move is not the side that captured.
	     */

	    if (future_position.side_to_move != tb->piece_color[captured_piece])
		continue;

	    /* Take the global position from the futurebase and translate it into a local position
	     * for the current tablebase.  There should be one piece missing from the local
	     * position: the piece that was captured.  There could possibly be one piece on a
	     * restricted square, as well.  If so, then it must be the piece that moved in order to
	     * capture.
	     */

	    conversion_result = global_position_to_local_position(tb, &future_position, &current_position);

	    if ((conversion_result & 0xff) != captured_piece) {
		fprintf(stderr, "Conversion error during capture back-prop\n");
		continue;
	    }

	    flip_side_to_move_local(&current_position);

	    if ((conversion_result & 0xff00) == 0x8000) {

		/* No pieces were on restricted squares.  Check them all. */

		for (piece = 0; piece < tb->num_mobiles; piece++) {
		    consider_possible_captures(tb, futurebase, future_index, &current_position,
					       piece, captured_piece, mate_in_limit);
		}

	    } else {

		/* One piece was on a restricted square.  It's the only possible capturing piece. */

		consider_possible_captures(tb, futurebase, future_index, &current_position,
					   conversion_result >> 8, captured_piece, mate_in_limit);

	    }

	}
    }
}

/* A "normal" futurebase is one that's identical to our own in terms of the number and types
 * of pieces.  It differs only in the frozen positions of the pieces.
 */

void propagate_moves_from_normal_futurebase(tablebase *tb, tablebase *futurebase,
					    int invert_colors_of_futurebase, int *mate_in_limit)
{
    int32 future_index;
    int32 max_future_index_static = max_index(futurebase);
    global_position_t future_position;
    local_position_t parent_position;
    local_position_t current_position; /* i.e, last position that moved to parent_position */
    int conversion_result;
    int piece;
    int dir;
    struct movement *movementptr;

    for (future_index = 0; future_index < max_future_index_static; future_index ++) {

	if (index_to_global_position(futurebase, future_index, &future_position)) {

	    if (invert_colors_of_futurebase)
		invert_colors_of_global_position(&future_position);


	    /* We have exactly the same number and type of pieces here, but exactly one of them is
	     * on a restricted square (according to the current tablebase).  If more than one of
	     * them was on a restricted square, then there'd be no way we could get to this
	     * futurebase with a single move.  On the other hand, if none of them were on restricted
	     * squares, then this would be a position in the current tablebase.
	     */

	    conversion_result = global_position_to_local_position(tb, &future_position, &current_position);

	    if (((conversion_result & 0xff) != 0x80) || ((conversion_result & 0xff00 == 0x8000))) {
		fprintf(stderr, "Conversion error during normal back-prop\n"); /* BREAKPOINT */
		continue;
	    }

	    piece = conversion_result >> 8;

	    /* We've moving BACKWARDS in the game, so this has to be a piece of the player who is
	     * NOT TO PLAY here - this is the LAST move we're considering, not the next move.
	     */

	    if (tb->piece_color[piece] == future_position.side_to_move)
		continue;


	    /* If there are any en passant capturable pawns in the position, then the last move had
	     * to have been a pawn move.  In fact, in this case, we already know exactly what the
	     * last move had to have been.
	     */

	    if (future_position.en_passant_square != -1) {

		if (tb->piece_type[piece] != PAWN) continue;

		if (((tb->piece_color[piece] == WHITE)
		     && (current_position.piece_position[piece] != future_position.en_passant_square + 8))
		    || ((tb->piece_color[piece] == BLACK)
			&& (current_position.piece_position[piece] != future_position.en_passant_square - 8))) {

		    /* No reason to complain here.  Maybe some other pawn was the en passant pawn. */
		    continue;
		}

		flip_side_to_move_local(&current_position);
		current_position.en_passant_square = -1;

		/* I go to the trouble to update board_vector here so we can check en passant
		 * legality in propagate_one_move_within_table().
		 */

		current_position.board_vector &= ~BITVECTOR(current_position.piece_position[piece]);
		if (tb->piece_color[piece] == WHITE)
		    current_position.piece_position[piece] -= 16;
		else
		    current_position.piece_position[piece] += 16;

		current_position.board_vector |= BITVECTOR(current_position.piece_position[piece]);

		/* We never back out into a restricted position.  Since we've already decided
		 * that this is the only legal back-move from this point, well...
		 */

		if (! (tb->piece_legal_squares[piece]
		       & BITVECTOR(current_position.piece_position[piece]))) {
		    continue;
		}

		propagate_local_position_from_futurebase(tb, futurebase, future_index, &current_position, mate_in_limit);

		continue;

	    }

	    /* Abuse of notation here.  We just want to keep a copy of current_position because we
	     * change it around a lot during the loops below.
	     */

	    parent_position = current_position;

	    if (tb->piece_type[piece] != PAWN) {

		for (dir = 0; dir < number_of_movement_directions[tb->piece_type[piece]]; dir++) {

		    /* What about captures?  Well, first of all, there are no captures here!  We're
		     * moving BACKWARDS in the game... and pieces don't appear out of thin air.
		     * Captures are handled by back-propagation from futurebases, not here in the
		     * movement code.  The piece moving had to come from somewhere, and that
		     * somewhere will now be an empty square, so once we've hit another piece along
		     * a movement vector, there's absolutely no need to consider anything further.
		     */

		    for (movementptr
			     = movements[tb->piece_type[piece]][parent_position.piece_position[piece]][dir];
			 (movementptr->vector & parent_position.board_vector) == 0;
			 movementptr++) {

			/* We never back out into a restricted position (obviously) */

			if (! (tb->piece_legal_squares[piece] & movementptr->vector)) continue;

			/* Back stepping a half move here involves several things: flipping the
			 * side-to-move flag, clearing any en passant pawns into regular pawns, moving
			 * the piece (backwards), and considering a bunch of additional positions
			 * identical to the base position except that a single one of the pawns on the
			 * fourth or fifth ranks was capturable en passant.
			 *
			 * Of course, the only way we could have gotten an en passant pawn is if THIS
			 * MOVE created it.  Since this isn't a pawn move, that can't happen.  Checking
			 * additional en passant positions is taken care of in
			 * propagate_one_move_within_table()
			 */

			flip_side_to_move_local(&current_position);

			/* I go to the trouble to update board_vector here so we can check en passant
			 * legality in propagate_one_move_within_table().
			 */

			current_position.board_vector &= ~BITVECTOR(current_position.piece_position[piece]);

			current_position.piece_position[piece] = movementptr->square;

			current_position.board_vector |= BITVECTOR(movementptr->square);

			propagate_local_position_from_futurebase(tb, futurebase, future_index, &current_position, mate_in_limit);
		    }
		}

	    } else {

		/* Usual special case for pawns */

		for (movementptr = normal_pawn_movements_bkwd[parent_position.piece_position[piece]][tb->piece_color[piece]];
		     (movementptr->vector & parent_position.board_vector) == 0;
		     movementptr++) {

		    /* We never back out into a restricted position (obviously) */

		    if (! (tb->piece_legal_squares[piece] & movementptr->vector)) continue;

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
		     * worry about clearing en passant pawns here - there should be none.  Checking
		     * additional en passant positions is taken care of in
		     * propagate_one_move_within_table()
		     *
		     * But we start with an extra check to make sure this isn't a double pawn move, it
		     * which case it would result in an en passant position, not the non-en passant
		     * position we are in now (en passant got taken care of in the special case above).
		     */

		    if (((movementptr->square - parent_position.piece_position[piece]) == 16)
			|| ((movementptr->square - parent_position.piece_position[piece]) == -16)) {
			continue;
		    }

		    current_position = parent_position;

		    flip_side_to_move_local(&current_position);

		    /* I go to the trouble to update board_vector here so we can check en passant
		     * legality in propagate_one_move_within_table().
		     */

		    current_position.board_vector &= ~BITVECTOR(current_position.piece_position[piece]);

		    current_position.piece_position[piece] = movementptr->square;

		    current_position.board_vector |= BITVECTOR(current_position.piece_position[piece]);

		    propagate_local_position_from_futurebase(tb, futurebase, future_index, &current_position, mate_in_limit);

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
 * one.
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
			if ((tb->piece_type[piece] == futurebase->piece_type[future_piece])
			    && ((!invert_colors &&
				 (tb->piece_color[piece] == futurebase->piece_color[future_piece]))
				|| (invert_colors &&
				    (tb->piece_color[piece] != futurebase->piece_color[future_piece])))) {
			    if ((tb->piece_legal_squares[piece] & futurebase->piece_legal_squares[future_piece])
				!= tb->piece_legal_squares[piece]) {
				fprintf(stderr, "WARNING: matched a piece but futurebase is more restrictive\n");
			    } else {
				piece_vector ^= (1 << piece);
				break;
			    }
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
			if ((tb->piece_type[piece] == futurebase->piece_type[future_piece])
			    && ((!invert_colors &&
				 (tb->piece_color[piece] == futurebase->piece_color[future_piece]))
				|| (invert_colors &&
				    (tb->piece_color[piece] != futurebase->piece_color[future_piece])))) {
			    if ((tb->piece_legal_squares[piece] & futurebase->piece_legal_squares[future_piece])
				!= tb->piece_legal_squares[piece]) {
				fprintf(stderr, "WARNING: matched a piece but futurebase is more restrictive\n");
			    } else {
				piece_vector ^= (1 << piece);
				break;
			    }
			}
		    }
		    if (piece == tb->num_mobiles) {
			if ((promoted_piece == -1) && (futurebase->piece_type[future_piece] != PAWN)) {
			    promoted_piece = future_piece;
			} else {
			    fprintf(stderr, "'%s': Couldn't find future piece in tablebase\n", filename);
			    return -1;
			}
		    }
		}

		/* The only thing left unaccounted for in the current tablebase should be a pawn. */

		for (piece = 0; piece < tb->num_mobiles; piece ++) {
		    if ((tb->piece_type[piece] == PAWN) && (piece_vector & (1 << piece))) break;
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

		promoted_piece_char = global_pieces[tb->piece_color[piece]][futurebase->piece_type[promoted_piece]];

		fprintf(stderr, "Back propagating from '%s'\n", (char *) filename);

		propagate_moves_from_promotion_futurebase(tb, futurebase, invert_colors,
							  promoted_piece_char, piece, &mate_in_limit);

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
			if ((tb->piece_type[piece] == futurebase->piece_type[future_piece])
			    && ((!invert_colors &&
				 (tb->piece_color[piece] == futurebase->piece_color[future_piece]))
				|| (invert_colors &&
				    (tb->piece_color[piece] != futurebase->piece_color[future_piece])))) {
			    piece_vector ^= (1 << piece);
			    break;
			}
		    }
		    if (piece == tb->num_mobiles) {
			if ((promoted_piece == -1) && (futurebase->piece_type[future_piece] != PAWN)) {
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
		    if ((tb->piece_type[piece] == PAWN) && (piece_vector & (1 << piece))) break;
		}

		if (piece == tb->num_mobiles) {
		    fprintf(stderr, "'%s': No extra pawn in tablebase\n", filename);
		    return -1;
		}

		piece_vector ^= (1 << piece);

		for (piece = 0; piece < tb->num_mobiles; piece ++) {
		    if ((tb->piece_type[piece] != PAWN) && (piece_vector & (1 << piece))) break;
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


		promoted_piece_char = global_pieces[BLACK - tb->piece_color[piece]][futurebase->piece_type[promoted_piece]];
		captured_piece_char = global_pieces[tb->piece_color[piece]][tb->piece_type[piece]];

		fprintf(stderr, "Back propagating from '%s'\n", (char *) filename);

		propagate_moves_from_promotion_capture_futurebase(tb, futurebase, invert_colors,
								  promoted_piece_char,
								  captured_piece_char,
								  &mate_in_limit);

	    } else if ((type != NULL) && !strcasecmp((char *) type, "normal")) {

		fprintf(stderr, "Back propagating from '%s'\n", (char *) filename);

		propagate_moves_from_normal_futurebase(tb, futurebase, invert_colors, &mate_in_limit);

	    } else {

		fprintf(stderr, "'%s': Unknown back propagation type (%s)\n",
			(char *) filename, (char *) type);
		return -1;

	    }

	    unload_futurebase(futurebase);
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
    int max_complaints = 10;

    for (index = 0; index < max_index_static; index ++) {
	if (tb->entries[index].futuremove_cnt != 0) {
	    switch (tb->move_restrictions[index_to_side_to_move(tb, index)]) {

	    case RESTRICTION_NONE:
		{
		    global_position_t global;
		    index_to_global_position(tb, index, &global);
		    if (all_futuremoves_handled)
			fprintf(stderr, "ERROR: Some futuremoves not handled under move restrictions!\n");
		    fprintf(stderr, "%s\n", global_position_to_FEN(&global));
		    if ((-- max_complaints) == 0) return 0;
		    all_futuremoves_handled = 0;		/* BREAKPOINT */
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

/* We've got a move that needs to be propagated, so we back out one half-move to all of the
 * positions that could have gotten us here and update their counters in various obscure ways.
 */

void propagate_one_minimove_within_table(tablebase *tb, int32 parent_index, local_position_t *current_position)
{
    int32 current_index;

    /* local_position_to_index() only requires the square numbers of the pieces, not the board
     * vectors.
     */

#if NEEDED
		current_position.board_vector &= ~BITVECTOR(parent_position.piece_position[piece]);
		current_position.board_vector |= BITVECTOR(movementptr->square);
		if (tb->piece_color[piece] == WHITE) {
		    current_position.white_vector &= ~BITVECTOR(parent_position.piece_position[piece]);
		    current_position.white_vector |= BITVECTOR(movementptr->square);
		} else {
		    current_position.black_vector &= ~BITVECTOR(parent_position.piece_position[piece]);
		    current_position.black_vector |= BITVECTOR(movementptr->square);
		}
#endif

    current_index = local_position_to_index(tb, current_position);

    if (current_index == -1) {
	/* This can happen if we don't fully check en passant legality (but right now, we do) */
	fprintf(stderr, "Can't lookup position in intratable propagation!\n");  /* BREAKPOINT */
	return;
    }

#ifdef DEBUG_MOVE
    if (current_index == DEBUG_MOVE)
	printf("propagate_one_minimove_within_table:  current_index=%d; parent_index=%d\n",
	       current_index, parent_index);
#endif

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

void propagate_one_move_within_table(tablebase *tb, int32 parent_index, local_position_t *position)
{
    int piece;

    /* We may need to consider a bunch of additional positions here that are identical to the base
     * position except that a single one of the pawns on the fourth or fifth ranks was capturable en
     * passant.
     * 
     * We key off the en_passant flag in the position that was passed in.  If it's set, then we're
     * back propagating a position that requires en passant, so we just do it.  Otherwise, we're
     * back propagating a position that doesn't require en passant, so we check for additional
     * en passant positions.
     */

    propagate_one_minimove_within_table(tb, parent_index, position);

    if (position->en_passant_square == -1) {

	for (piece = 0; piece < tb->num_mobiles; piece ++) {

	    if (tb->piece_color[piece] == position->side_to_move) continue;
	    if (tb->piece_type[piece] != PAWN) continue;

#if 1
	    /* XXX I've taken care to update board_vector specifically so we can check for en
	     * passant legality here.
	     */

	    if ((tb->piece_color[piece] == WHITE)
		&& (ROW(position->piece_position[piece]) == 3)
		&& !(position->board_vector & BITVECTOR(position->piece_position[piece] - 8))
		&& !(position->board_vector & BITVECTOR(position->piece_position[piece] - 16))) {
		position->en_passant_square = position->piece_position[piece] - 8;
		propagate_one_minimove_within_table(tb, parent_index, position);
	    }

	    if ((tb->piece_color[piece] == BLACK)
		&& (ROW(position->piece_position[piece]) == 4)
		&& !(position->board_vector & BITVECTOR(position->piece_position[piece] + 8))
		&& !(position->board_vector & BITVECTOR(position->piece_position[piece] + 16))) {
		position->en_passant_square = position->piece_position[piece] + 8;
		propagate_one_minimove_within_table(tb, parent_index, position);
	    }

#else
	    /* XXX The problem is that the board vectors might not be correct, because we moved the
	     * piece without updating them.  We don't even bother to use them here.  We get around
	     * this by checking in local_position_to_index() for illegal en passant positions.
	     */

	    if ((tb->piece_color[piece] == WHITE)
		&& (ROW(position->piece_position[piece]) == 3)) {
		position->en_passant_square = position->piece_position[piece] - 8;
		propagate_one_minimove_within_table(tb, parent_index, position);
	    }

	    if ((tb->piece_color[piece] == BLACK)
		&& (ROW(position->piece_position[piece]) == 4)) {
		position->en_passant_square = position->piece_position[piece] + 8;
		propagate_one_minimove_within_table(tb, parent_index, position);
	    }
#endif

	    position->en_passant_square = -1;
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

    /* If there are any en passant capturable pawns in the position, then the last move had to
     * have been a pawn move.  In fact, in this case, we already know exactly what the last move
     * had to have been.
     */

    if (parent_position.en_passant_square != -1) {

	int en_passant_pawn = -1;

	for (piece = 0; piece < tb->num_mobiles; piece++) {

	    if (tb->piece_color[piece] == parent_position.side_to_move) continue;
	    if (tb->piece_type[piece] != PAWN) continue;

	    if (((tb->piece_color[piece] == WHITE)
		 && (parent_position.piece_position[piece] - 8 == parent_position.en_passant_square))
		|| ((tb->piece_color[piece] == BLACK)
		    && (parent_position.piece_position[piece] + 8 == parent_position.en_passant_square))) {
		if (en_passant_pawn != -1) fprintf(stderr, "Two en passant pawns in back prop?!\n");
		en_passant_pawn = piece;
	    }
	}
	if (en_passant_pawn == -1) {
	    fprintf(stderr, "No en passant pawn in back prop!?\n");
	} else {
	    current_position = parent_position;
	    flip_side_to_move_local(&current_position);
	    current_position.en_passant_square = -1;

	    /* I go to the trouble to update board_vector here so we can check en passant
	     * legality in propagate_one_move_within_table().
	     */

	    current_position.board_vector &= ~BITVECTOR(current_position.piece_position[en_passant_pawn]);
	    if (tb->piece_color[en_passant_pawn] == WHITE)
		current_position.piece_position[en_passant_pawn] -= 16;
	    else
		current_position.piece_position[en_passant_pawn] += 16;

	    current_position.board_vector |= BITVECTOR(current_position.piece_position[en_passant_pawn]);

	    /* We never back out into a restricted position.  Since we've already decided that this
	     * is the only legal back-move from this point, well...
	     */

	    if (! (tb->piece_legal_squares[en_passant_pawn]
		   & BITVECTOR(current_position.piece_position[en_passant_pawn]))) {
		return;
	    }

	    propagate_one_move_within_table(tb, parent_index, &current_position);
	}

	return;
    }

    /* foreach (mobile piece of player NOT TO PLAY) { */

    for (piece = 0; piece < tb->num_mobiles; piece++) {

	/* We've moving BACKWARDS in the game, so we want the pieces of the player who is NOT TO
	 * PLAY here - this is the LAST move we're considering, not the next move.
	 */

	if (tb->piece_color[piece] == parent_position.side_to_move)
	    continue;

	if (tb->piece_type[piece] != PAWN) {

	    for (dir = 0; dir < number_of_movement_directions[tb->piece_type[piece]]; dir++) {

		/* What about captures?  Well, first of all, there are no captures here!  We're
		 * moving BACKWARDS in the game... and pieces don't appear out of thin air.
		 * Captures are handled by back-propagation from futurebases, not here in the
		 * movement code.  The piece moving had to come from somewhere, and that somewhere
		 * will now be an empty square, so once we've hit another piece along a movement
		 * vector, there's absolutely no need to consider anything further.
		 */

		for (movementptr
			 = movements[tb->piece_type[piece]][parent_position.piece_position[piece]][dir];
		     (movementptr->vector & parent_position.board_vector) == 0;
		     movementptr++) {

		    /* We never back out into a restricted position (obviously) */

		    if (! (tb->piece_legal_squares[piece] & movementptr->vector)) continue;

		    /* XXX can we move the next several statements out of this loop (ditto below)? */

		    current_position = parent_position;

		    /* Back stepping a half move here involves several things: flipping the
		     * side-to-move flag, clearing any en passant pawns into regular pawns, moving
		     * the piece (backwards), and considering a bunch of additional positions
		     * identical to the base position except that a single one of the pawns on the
		     * fourth or fifth ranks was capturable en passant.
		     *
		     * Of course, the only way we could have gotten an en passant pawn is if THIS
		     * MOVE created it.  Since this isn't a pawn move, that can't happen.  Checking
		     * additional en passant positions is taken care of in
		     * propagate_one_move_within_table()
		     */

		    flip_side_to_move_local(&current_position);

		    /* I go to the trouble to update board_vector here so we can check en passant
		     * legality in propagate_one_move_within_table().
		     */

		    current_position.board_vector &= ~BITVECTOR(current_position.piece_position[piece]);

		    current_position.piece_position[piece] = movementptr->square;

		    current_position.board_vector |= BITVECTOR(movementptr->square);

		    propagate_one_move_within_table(tb, parent_index, &current_position);
		}
	    }

	} else {

	    /* Usual special case for pawns */

	    for (movementptr = normal_pawn_movements_bkwd[parent_position.piece_position[piece]][tb->piece_color[piece]];
		 (movementptr->vector & parent_position.board_vector) == 0;
		 movementptr++) {

		/* We never back out into a restricted position (obviously) */

		if (! (tb->piece_legal_squares[piece] & movementptr->vector)) continue;

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
		 * worry about clearing en passant pawns here - there should be none.  Checking
		 * additional en passant positions is taken care of in
		 * propagate_one_move_within_table()
		 *
		 * But we start with an extra check to make sure this isn't a double pawn move, it
		 * which case it would result in an en passant position, not the non-en passant
		 * position we are in now (en passant got taken care of in the special case above).
		 */

		if (((movementptr->square - parent_position.piece_position[piece]) == 16)
		    || ((movementptr->square - parent_position.piece_position[piece]) == -16)) {
		    continue;
		}

		current_position = parent_position;

		flip_side_to_move_local(&current_position);

		/* I go to the trouble to update board_vector here so we can check en passant
		 * legality in propagate_one_move_within_table().
		 */

		current_position.board_vector &= ~BITVECTOR(current_position.piece_position[piece]);

		current_position.piece_position[piece] = movementptr->square;

		current_position.board_vector |= BITVECTOR(current_position.piece_position[piece]);

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

		if (tb->piece_color[piece] != position.side_to_move)
		    continue;

		if (tb->piece_type[piece] != PAWN) {

		    for (dir = 0; dir < number_of_movement_directions[tb->piece_type[piece]]; dir++) {

			for (movementptr = movements[tb->piece_type[piece]][position.piece_position[piece]][dir];
			     (movementptr->vector & position.board_vector) == 0;
			     movementptr++) {

			    /* If a piece is moving outside its restricted squares, we regard this
			     * as a futurebase (since it will require back prop from futurebases)
			     */

			    if (!(tb->piece_legal_squares[piece] & BITVECTOR(movementptr->square))) {
				futuremove_cnt ++;
			    }

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
				if (movementptr->square == position.piece_position[BLACK_KING]) {
				    initialize_index_with_black_mated(tb, index);
				    goto mated;
				}
			    }
			} else {
			    if ((movementptr->vector & position.black_vector) == 0) {
				movecnt ++;
				futuremove_cnt ++;
				if (movementptr->square == position.piece_position[WHITE_KING]) {
				    initialize_index_with_white_mated(tb, index);
				    goto mated;
				}
			    }
			}
		    }

		} else {

		    /* Pawns, as always, are special */

		    for (movementptr = normal_pawn_movements[position.piece_position[piece]][tb->piece_color[piece]];
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

			    /* If a piece is moving outside its restricted squares, we regard this
			     * as a futurebase (since it will require back prop from futurebases)
			     */

			    if (!(tb->piece_legal_squares[piece] & BITVECTOR(movementptr->square))) {
				futuremove_cnt ++;
			    }

			    movecnt ++;

			}

		    }


		    /* Pawn captures.
		     *
		     * In this part of the code, we're just counting forward moves, and all captures
		     * are futurebase moves, so the only difference to us whether this is a
		     * promotion move or not is how many futuremoves get recorded.
		     */

#define ENEMY_BOARD_VECTOR(pos) ((tb->piece_color[piece] == WHITE) ? pos.black_vector : pos.white_vector)

		    for (movementptr = capture_pawn_movements[position.piece_position[piece]][tb->piece_color[piece]];
			 movementptr->square != -1;
			 movementptr++) {

			/* A special check for en passant captures.  */

			if (movementptr->square == position.en_passant_square) {
			    movecnt ++;
			    futuremove_cnt ++;
			    continue;
			}

			if ((movementptr->vector & ENEMY_BOARD_VECTOR(position)) == 0) continue;

			/* Same check as above for a mated situation */

			if (position.side_to_move == WHITE) {
			    if (movementptr->square == position.piece_position[BLACK_KING]) {
				initialize_index_with_black_mated(tb, index);
				goto mated;
			    }
			} else {
			    if (movementptr->square == position.piece_position[WHITE_KING]) {
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

#ifdef USE_NALIMOV

int EGTBProbe(int wtm, unsigned char board[64], int sqEnP, int *score);

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

	    } else if ((global.en_passant_square != -1)
		       && ((global.board[global.en_passant_square - 9] != 'P')
			   || (global.en_passant_square == 40)
			   || (global.side_to_move == BLACK))
		       && ((global.board[global.en_passant_square - 7] != 'P')
			   || (global.en_passant_square == 47)
			   || (global.side_to_move == BLACK))
		       && ((global.board[global.en_passant_square + 7] != 'p')
			   || (global.en_passant_square == 16)
			   || (global.side_to_move == WHITE))
		       && ((global.board[global.en_passant_square + 9] != 'p')
			   || (global.en_passant_square == 23)
			   || (global.side_to_move == WHITE))) {

		/* Nor does Nalimov like it if the en passant pawn can't actually be captured by
		 * another pawn.
		 */

	    } else if (EGTBProbe(global.side_to_move == WHITE, global.board, global.en_passant_square, &score) == 1) {

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

#endif /* USE_NALIMOV */


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
	if (! have_all_futuremoves_been_handled(tb)) {
	    exit(1);
	}
	fprintf(stderr, "All futuremoves handled under move restrictions\n");

	fprintf(stderr, "Intra-table propagating\n");
	propagate_all_moves_within_tablebase(tb, mate_in_limit);

	write_tablebase_to_file(tb, output_filename);

	exit(1);
    }

    /* Probing / Verifying */

#ifdef USE_NALIMOV
    init_nalimov_code();
#endif

    i = 0;
    /* calloc (unlike malloc) zeros memory */
    tbs = calloc(argc - optind + 1, sizeof(tablebase *));

    for (argi=optind; argi<argc; argi++) {
	fprintf(stderr, "Loading '%s'\n", argv[argi]);
	tbs[i] = load_futurebase_from_file(argv[argi]);
#ifdef USE_NALIMOV
	if (verify) verify_tablebase_against_nalimov(tbs[i]);
#endif
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

#ifdef USE_NALIMOV
		printf("\nNalimov score: ");
		if (EGTBProbe(global_position.side_to_move == WHITE, global_position.board, -1, &score) == 1) {
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

		if (tb->piece_color[piece] != global_position.side_to_move)
		    continue;

		if (tb->piece_type[piece] != PAWN) {

		    for (dir = 0; dir < number_of_movement_directions[tb->piece_type[piece]]; dir++) {

			index_to_local_position(tb, index, &pos);

			nextpos = pos;

			flip_side_to_move_local(&nextpos);
			nextpos.en_passant_square = -1;

			for (movementptr = movements[tb->piece_type[piece]][pos.piece_position[piece]][dir];
			     (movementptr->vector & pos.board_vector) == 0;
			     movementptr++) {

			    nextpos.piece_position[piece] = movementptr->square;

			    index2 = local_position_to_index(tb, &nextpos);

			    /* This is the next move, so we reverse the sense of PTM and PNTM */

			    if ((index2 != -1) && is_position_valid(tb, index2)) {
				printf("   %s%s    ",
				       algebraic_notation[pos.piece_position[piece]],
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

			    if ((movementptr->square == pos.piece_position[BLACK_KING])
				|| (movementptr->square == pos.piece_position[WHITE_KING])) {

				/* printf("MATE\n"); */

			    } else {
				tablebase *tb2;
				global_position_t reversed_position;

				global_capture_position.board[pos.piece_position[piece]] = 0;
				place_piece_in_global_position(&global_capture_position, movementptr->square,
							       tb->piece_color[piece],
							       tb->piece_type[piece]);

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
					printf ("   %sx%s   ",
						algebraic_notation[pos.piece_position[piece]],
						algebraic_notation[movementptr->square]);
					print_score(tb2, index2, pntm, ptm);
				    }
				} else {
				    printf("   %sx%s   NO DATA\n",
					   algebraic_notation[pos.piece_position[piece]],
					   algebraic_notation[movementptr->square]);
				}
			    }
			}
			/* end of capture search */
		    }

		} else {

		    /* PAWNs */

		    int promoted_pieces[] = {QUEEN, ROOK, KNIGHT, 0};

		    index_to_local_position(tb, index, &pos);
		    nextpos = pos;
		    flip_side_to_move_local(&nextpos);

		    /* normal pawn moves */

		    for (movementptr = normal_pawn_movements[pos.piece_position[piece]][tb->piece_color[piece]];
			 (movementptr->vector & pos.board_vector) == 0;
			 movementptr++) {

			if ((ROW(movementptr->square) != 0) && (ROW(movementptr->square) != 7)) {

			    nextpos.piece_position[piece] = movementptr->square;

			    index2 = local_position_to_index(tb, &nextpos);

			    /* This is the next move, so we reverse the sense of PTM and PNTM */

			    if ((index2 != -1) && is_position_valid(tb, index2)) {
				printf("   %s%s    ",
				       algebraic_notation[pos.piece_position[piece]],
				       algebraic_notation[movementptr->square]);
				print_score(tb, index2, pntm, ptm);
			    }

			} else {

			    /* non-capture promotion */

			    tablebase *tb2;
			    global_position_t reversed_position;
			    int *promoted_piece;

			    index_to_global_position(tb, index, &global_capture_position);

			    flip_side_to_move_global(&global_capture_position);

			    global_capture_position.board[pos.piece_position[piece]] = 0;

			    for (promoted_piece = promoted_pieces; *promoted_piece; promoted_piece ++) {

				place_piece_in_global_position(&global_capture_position, movementptr->square,
							       tb->piece_color[piece],
							       *promoted_piece);

				reversed_position = global_capture_position;
				invert_colors_of_global_position(&reversed_position);

				if (search_tablebases_for_global_position(tbs, &global_capture_position,
									  &tb2, &index2)
				    || search_tablebases_for_global_position(tbs, &reversed_position,
									     &tb2, &index2)) {

				    if (is_position_valid(tb2, index2)) {
					printf ("   %s%s=%c  ",
						algebraic_notation[pos.piece_position[piece]],
						algebraic_notation[movementptr->square],
						piece_char[*promoted_piece]);
					print_score(tb2, index2, pntm, ptm);
				    }
				} else {
				    printf("   %s%s=%c  NO DATA\n",
					   algebraic_notation[pos.piece_position[piece]],
					   algebraic_notation[movementptr->square],
					   piece_char[*promoted_piece]);
				}
			    }
			}
		    }

		    /* capture pawn moves */

		    for (movementptr = capture_pawn_movements[pos.piece_position[piece]][tb->piece_color[piece]];
			 movementptr->square != -1;
			 movementptr++) {

			if (movementptr->square == pos.en_passant_square) {

			    /* en passant capture */

			    tablebase *tb2;
			    global_position_t reversed_position;

			    global_capture_position.board[pos.piece_position[piece]] = 0;
			    place_piece_in_global_position(&global_capture_position, movementptr->square,
							   tb->piece_color[piece],
							   tb->piece_type[piece]);

			    if (tb->piece_color[piece] == WHITE) {
				global_capture_position.board[pos.en_passant_square - 8] = 0;
			    } else {
				global_capture_position.board[pos.en_passant_square + 8] = 0;
			    }

			    flip_side_to_move_global(&global_capture_position);

			    reversed_position = global_capture_position;
			    invert_colors_of_global_position(&reversed_position);

			    if (search_tablebases_for_global_position(tbs, &global_capture_position,
								      &tb2, &index2)
				|| search_tablebases_for_global_position(tbs, &reversed_position,
									 &tb2, &index2)) {

				if (is_position_valid(tb2, index2)) {
				    printf ("   %sx%s   ",
					    algebraic_notation[pos.piece_position[piece]],
					    algebraic_notation[movementptr->square]);
				    print_score(tb2, index2, pntm, ptm);
				}
			    } else {
				printf("   %sx%s   NO DATA\n",
				       algebraic_notation[pos.piece_position[piece]],
				       algebraic_notation[movementptr->square]);
			    }

			    continue;
			}

			if ((movementptr->vector & ENEMY_BOARD_VECTOR(pos)) == 0) continue;

			if ((ROW(movementptr->square) == 7) || (ROW(movementptr->square) == 0)) {

			    /* promotion capture */

			    tablebase *tb2;
			    global_position_t reversed_position;
			    int *promoted_piece;

			    index_to_global_position(tb, index, &global_capture_position);

			    flip_side_to_move_global(&global_capture_position);

			    global_capture_position.board[pos.piece_position[piece]] = 0;

			    for (promoted_piece = promoted_pieces; *promoted_piece; promoted_piece ++) {

				place_piece_in_global_position(&global_capture_position, movementptr->square,
							       tb->piece_color[piece],
							       *promoted_piece);

				reversed_position = global_capture_position;
				invert_colors_of_global_position(&reversed_position);

				if (search_tablebases_for_global_position(tbs, &global_capture_position,
									  &tb2, &index2)
				    || search_tablebases_for_global_position(tbs, &reversed_position,
									     &tb2, &index2)) {

				    if (is_position_valid(tb2, index2)) {
					printf ("   %sx%s=%c ",
						algebraic_notation[pos.piece_position[piece]],
						algebraic_notation[movementptr->square],
						piece_char[*promoted_piece]);
					print_score(tb2, index2, pntm, ptm);
				    }
				} else {
				    printf("   %sx%s=%c NO DATA\n",
					   algebraic_notation[pos.piece_position[piece]],
					   algebraic_notation[movementptr->square],
					   piece_char[*promoted_piece]);
				}
			    }

			    continue;
			}

			if ((movementptr->square == pos.piece_position[BLACK_KING])
			    || (movementptr->square == pos.piece_position[WHITE_KING])) {

			    /* printf("MATE\n"); */

			} else {
			    tablebase *tb2;
			    global_position_t reversed_position;

			    global_capture_position.board[pos.piece_position[piece]] = 0;
			    place_piece_in_global_position(&global_capture_position, movementptr->square,
							   tb->piece_color[piece],
							   tb->piece_type[piece]);

			    flip_side_to_move_global(&global_capture_position);

			    reversed_position = global_capture_position;
			    invert_colors_of_global_position(&reversed_position);

			    if (search_tablebases_for_global_position(tbs, &global_capture_position,
								      &tb2, &index2)
				|| search_tablebases_for_global_position(tbs, &reversed_position,
									 &tb2, &index2)) {

				if (is_position_valid(tb2, index2)) {
				    printf ("   %sx%s   ",
					    algebraic_notation[pos.piece_position[piece]],
					    algebraic_notation[movementptr->square]);
				    print_score(tb2, index2, pntm, ptm);
				}
			    } else {
				printf("   %sx%s   NO DATA\n",
				       algebraic_notation[pos.piece_position[piece]],
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
