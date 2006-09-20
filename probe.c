#if !defined(NOEGTB)
#  include <stdio.h>
#if 0
#  include "chess.h"
#  include "data.h"
#endif

#include <strings.h>

/* last modified 11/03/98 */
/*
 *******************************************************************************
 *                                                                             *
 *  EGTBProbe() is the interface to the new tablebase code by Eugene Nalimov.  *
 *  this is called from Search() when 5 or fewer pieces are left on the board. *
 *                                                                             *
 *******************************************************************************
 */

#  define  T_INDEX64
#  define  XX  127
#  define  C_PIECES  3  /* Maximum # of pieces of one color OTB */

#  if defined (T_INDEX64) && defined (_MSC_VER)
typedef unsigned __int64 INDEX;
#  elif defined (T_INDEX64)
typedef unsigned long long INDEX;
#  else
typedef unsigned long INDEX;
#  endif

typedef unsigned int squaret;

/* Those declarations necessary because Crafty is C, not C++ program */

#  if defined (_MSC_VER)
#    define  TB_FASTCALL  __fastcall
#  else
#    define  TB_FASTCALL
#  endif

typedef int color;

#  define  x_colorWhite  0
#  define  x_colorBlack  1
#  define  x_colorNeutral  2
#  define COLOR_DECLARED

typedef int piece;

#  define  x_pieceNone    0
#  define  x_piecePawn    1
#  define  x_pieceKnight  2
#  define  x_pieceBishop  3
#  define  x_pieceRook    4
#  define  x_pieceQueen   5
#  define  x_pieceKing    6
#  define PIECES_DECLARED
typedef signed char tb_t;

#  define pageL       65536
#  define tbbe_ssL    ((pageL-4)/2)
#  define bev_broken  (tbbe_ssL+1)      /* illegal or busted */
#  define bev_mi1     tbbe_ssL  /* mate in 1 move */
#  define bev_mimin   1 /* mate in max moves */
#  define bev_draw    0 /* draw */
#  define bev_limax   (-1)      /* mated in max moves */
#  define bev_li0     (-tbbe_ssL)
                                /* mated in 0 moves */

typedef INDEX(TB_FASTCALL * PfnCalcIndex)
 (squaret *, squaret *, squaret, int fInverse);

extern int IDescFindFromCounters(int *);
extern int FRegisteredFun(int, color);
extern PfnCalcIndex PfnIndCalcFun(int, color);
extern int TB_FASTCALL L_TbtProbeTable(int, color, INDEX);

#  define PfnIndCalc PfnIndCalcFun
#  define FRegistered FRegisteredFun

int EGTBProbe(int wtm, unsigned char board[64], int *score)
{
  int rgiCounters[10], iTb, fInvert;
  color side;
  squaret rgsqWhite[C_PIECES * 5 + 1], rgsqBlack[C_PIECES * 5 + 1];
  squaret *psqW, *psqB, sqEnP;
  INDEX ind;
  int tbValue;
  int square;

/*
 ************************************************************
 *                                                          *
 *   initialize counters and piece arrays so the probe code *
 *   can compute the modified Godel number.                 *
 *                                                          *
 ************************************************************
 */
#if 0
  VInitSqCtr(rgiCounters, rgsqWhite, 0, WhitePawns);
  VInitSqCtr(rgiCounters, rgsqWhite, 1, WhiteKnights);
  VInitSqCtr(rgiCounters, rgsqWhite, 2, WhiteBishops);
  VInitSqCtr(rgiCounters, rgsqWhite, 3, WhiteRooks);
  VInitSqCtr(rgiCounters, rgsqWhite, 4, WhiteQueens);
  VInitSqCtr(rgiCounters + 5, rgsqBlack, 0, BlackPawns);
  VInitSqCtr(rgiCounters + 5, rgsqBlack, 1, BlackKnights);
  VInitSqCtr(rgiCounters + 5, rgsqBlack, 2, BlackBishops);
  VInitSqCtr(rgiCounters + 5, rgsqBlack, 3, BlackRooks);
  VInitSqCtr(rgiCounters + 5, rgsqBlack, 4, BlackQueens);
#endif
#if 0
  /* 4=QUEEN  0=first Queen */
  if (WhiteQueenSQ != -1) rgsqWhite[4*C_PIECES+0] = WhiteQueenSQ;
  if (WhiteRookSQ != -1) rgsqWhite[3*C_PIECES+0] = WhiteRookSQ;
  if (BlackRookSQ != -1) rgsqBlack[3*C_PIECES+0] = BlackRookSQ;
  rgiCounters[0]=0; /* num of white pawns */
  rgiCounters[1]=0; /* num of white knights */
  rgiCounters[2]=0; /* num of white bishops */
  rgiCounters[3]=WhiteRookSQ != -1 ? 1 : 0; /* num of white rooks */
  rgiCounters[4]=WhiteQueenSQ != -1 ? 1: 0; /* num of white queens */
  rgiCounters[5]=0; /* black pawns */
  rgiCounters[6]=0; /* black knights */
  rgiCounters[7]=0; /* black bishops */
  rgiCounters[8]=BlackRookSQ != -1 ? 1 : 0;
  rgiCounters[9]=0;
#endif
  bzero(rgiCounters, sizeof(rgiCounters));
  for (square=0; square<64; square++) {
    switch (board[square]) {
    case 'P':
      rgsqWhite[0*C_PIECES+rgiCounters[0]] = square;
      rgiCounters[0] ++;
      break;
    case 'N':
      rgsqWhite[1*C_PIECES+rgiCounters[1]] = square;
      rgiCounters[1] ++;
      break;
    case 'B':
      rgsqWhite[2*C_PIECES+rgiCounters[2]] = square;
      rgiCounters[2] ++;
      break;
    case 'R':
      rgsqWhite[3*C_PIECES+rgiCounters[3]] = square;
      rgiCounters[3] ++;
      break;
    case 'Q':
      rgsqWhite[4*C_PIECES+rgiCounters[4]] = square;
      rgiCounters[4] ++;
      break;
    case 'K':
      rgsqWhite[5*C_PIECES] = square;
      break;
    case 'p':
      rgsqBlack[0*C_PIECES+rgiCounters[5]] = square;
      rgiCounters[5] ++;
      break;
    case 'n':
      rgsqBlack[1*C_PIECES+rgiCounters[6]] = square;
      rgiCounters[6] ++;
      break;
    case 'b':
      rgsqBlack[2*C_PIECES+rgiCounters[7]] = square;
      rgiCounters[7] ++;
      break;
    case 'r':
      rgsqBlack[3*C_PIECES+rgiCounters[8]] = square;
      rgiCounters[8] ++;
      break;
    case 'q':
      rgsqBlack[4*C_PIECES+rgiCounters[9]] = square;
      rgiCounters[9] ++;
      break;
    case 'k':
      rgsqBlack[5*C_PIECES] = square;
      break;
    }
  }
/*
 ************************************************************
 *                                                          *
 *   quick early exit.  is the tablebase for the current    *
 *   set of pieces registered?                              *
 *                                                          *
 ************************************************************
 */
  iTb = IDescFindFromCounters(rgiCounters);
  if (!iTb) {
    fprintf(stderr, "Can't find Nalimov tablebase\n");
    return (0);
  } else {
    /* fprintf(stderr, "IDescFindFromCounters returns %d\n", iTb); */
  }
/*
 ************************************************************
 *                                                          *
 *   yes, finish setting up to probe the tablebase.  if     *
 *   black is the "winning" side (more pieces) then we need *
 *   to "invert" the pieces in the lists.                   *
 *                                                          *
 ************************************************************
 */

#if 0
  rgsqWhite[C_PIECES * 5] = WhiteKingSQ;
  rgsqBlack[C_PIECES * 5] = BlackKingSQ;
#endif

  if (iTb > 0) {
    side = wtm ? x_colorWhite : x_colorBlack;
    fInvert = 0;
    psqW = rgsqWhite;
    psqB = rgsqBlack;
  } else {
    side = wtm ? x_colorBlack : x_colorWhite;
    fInvert = 1;
    psqW = rgsqBlack;
    psqB = rgsqWhite;
    iTb = -iTb;
  }
/*
 ************************************************************
 *                                                          *
 *   now check to see if this particular tablebase for this *
 *   color to move is registered.                           *
 *                                                          *
 ************************************************************
 */
  if (!FRegistered(iTb, side)) {
    fprintf(stderr, "FRegistered can't find Nalimov tablebase %d %d\n", iTb, side);
    return (0);
  }
#if 0
  sqEnP = EnPassant(ply) ? EnPassant(ply) : XX;
#else
  sqEnP = XX;
#endif
  ind = PfnIndCalc(iTb, side) (psqW, psqB, sqEnP, fInvert);
#if 0
  if (ind > 4) {
    fprintf(stderr, "ind(%d) > 4\n",ind);
    return (0);
  }
#endif
  tbValue = L_TbtProbeTable(iTb, side, ind);
  if (bev_broken == tbValue) {
    /* fprintf(stderr, "bev_broken\n"); */
    return (0);
  }
/*
 ************************************************************
 *                                                          *
 *   now convert to correct MATE range to match the value   *
 *   Crafty uses.                                           *
 *                                                          *
 ************************************************************
 */
#if 0
  if (tbValue > 0)
    *score = MATE + 2 * (-bev_mi1 + tbValue - 1);
  else if (tbValue < 0)
    *score = -MATE + 2 * (bev_mi1 + tbValue);
  else
    *score = DrawScore(wtm);
#else
  *score = tbValue;
#endif
  return (1);
}
#endif
