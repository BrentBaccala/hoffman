
typedef int_fast8_t boolean;

#ifndef PRIu32
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIu64 "llu"
#define PRIx64 "llx"
#endif

typedef uint32_t index_t;
#define PRIindex PRIu32
#define INVALID_INDEX 0xffffffff

#define MAX_FORMAT_BYTES 16

/* Futuremoves are moves like captures and promotions that lead to a different tablebase.
 * 'futurevectors' are bit vectors used to track which futuremoves have been handled in a particular
 * position.  They are of type futurevector_t, and the primary operations used to construct them are
 * FUTUREVECTOR(move) to get a futurevector with a bit set in move's position, and
 * FUTUREVECTORS(move,n) to get a futurevector with n bits set starting with move, although the
 * actual tables are now stored in a more compact format that only uses as many bits as are needed
 * for the particular tablebase being generated.
 */

typedef uint32_t futurevector_t;
#define get_futurevector_t_field get_uint32_t_field
#define set_futurevector_t_field set_uint32_t_field
#define FUTUREVECTOR_HEX_FORMAT "0x%" PRIx32
#define FUTUREVECTOR(move) (1ULL << (move))
#define FUTUREVECTORS(move, n) (((1ULL << (n)) - 1) << (move))
#define NO_FUTUREMOVE -1


typedef struct tablebase tablebase_t;
extern tablebase_t * current_tb;

index_t max_index(tablebase_t * tb);

/* This is a global position, that doesn't depend on a particular tablebase.  It's slower to
 * manipulate, but is suitable for probing tablebases.  Each char in the array is either 0 for an
 * empty square, and one of the FEN characters for a chess piece.
 */

typedef struct {
    unsigned char board[64];
    short side_to_move;
    short en_passant_square;
    short variant;
} global_position_t;

/* Support functions */

char * global_position_to_FEN(global_position_t *position);

void fatal (const char * format, ...);
void info (const char * format, ...);
void warning (const char * format, ...);

/* Proptable interface functions - these functions are called by the main program */

extern int proptable_writes;
extern struct timeval proptable_write_time;

int initialize_proptable(int proptable_MBs);

void proptable_pass(int target_dtm);
void finalize_proptable_pass(void);

void insert_new_propentry(index_t index, int dtm, unsigned int movecnt, boolean PTM_wins_flag, int futuremove);

/* ...and these main program functions are called by proptable_pass() */

int get_entry_DTM(index_t index);
void back_propagate_index(index_t index, int target_dtm);
void commit_entry(index_t index, int dtm, uint8_t PTM_wins_flag, int movecnt, futurevector_t futurevector);
