find . -type f -execdir zip '{}.zip' '{}' \;

ASSERT(IS_SET(val)) failes after it held in stored??
KNPKQ:
CORRUPT: 289181868: -5060  "1111110000111100"

correct value is -964      "1110110000111100"


LEVEL=21, LOSS_EGTB->TB[maybe_loss_ix]=-15076
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 8
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 7
 +---+---+---+---+---+---+---+---+
 |   |   | K |   | B |   |   |   | 6
 +---+---+---+---+---+---+---+---+
 |   |   |   |   | q |   |   |   | 5
 +---+---+---+---+---+---+---+---+
 | R |   |   |   |   |   |   |   | 4
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 3
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 2
 +---+---+---+---+---+---+---+---+
 |   |   | b | k |   |   |   |   | 1
 +---+---+---+---+---+---+---+---+
   a   b   c   d   e   f   g   h    STM: BLACK - 
8/8/2K1B3/4q3/R7/8/8/2bk4 b - - 
  c1b2 1110 at ix: 3152516964
  c1d2 1110 at ix: 3152946624
  c1a3 -10956 at ix: 3152259168
  c1e3 1110 at ix: 3257926884
  c1f4 -10974 at ix: 3258127392
  c1g5 1110 at ix: 3258327900
  c1h6 1110 at ix: 3258528408
  e5a1 967 at ix: 3047868420
  e5e1 -10978 at ix: 3152747964
  e5b2 -10978 at ix: 3047871654
  e5e2 -10978 at ix: 3152747502
  e5h2 -10978 at ix: 3152758590
  e5c3 1110 at ix: 3047874426
  e5e3 -10978 at ix: 3152747040
  e5g3 -10978 at ix: 3152754432
  e5d4 963 at ix: 3047906304
  e5e4 965 at ix: 3152746578
  e5f4 0 at ix: 3152750274
  e5a5 961 at ix: 3046119288
  e5b5 963 at ix: 3047870268
  e5c5 965 at ix: 3047873502
  e5d5 967 at ix: 3047905842
  e5f5 963 at ix: 3152749812
  e5g5 1110 at ix: 3152753508
  e5h5 1110 at ix: 3152757204
  e5d6 963 at ix: 3047905380
  e5e6x -980 at ix: 7634454
  e5f6 1110 at ix: 3152749350
  e5c7 961 at ix: 3047873040
  e5g7 -10978 at ix: 3152752584
  e5b8 1110 at ix: 3047868882
  e5h8 -10978 at ix: 3152755818
  d1e1 1110 at ix: 3257583622
  d1c2 -10978 at ix: 3152717467
  d1d2 -10978 at ix: 3152746115
  d1e2 1110 at ix: 3257583621
INCONSISTENCY: at ix 270266951 max_val: 10977 vs tb_val:979

bitstring(Int16(-15076))
"1100010100011100"
bitstring(Int16(-11000+22))
"1101010100011110"


KNKRBN: CORRUPT: 3834976428: -5076
KNKRBN: PREV CORRUPT: 3834976428: -980

KBKQRB: CORRUPT: 787988652: -5082
KBKQRB: PREV CORRUPT: 787988652: -986
 
KBKQQB: CORRUPT: 1167485100: -5092
KBKQQB: PREV CORRUPT: 1167485100: -996

KBKQQQ: CORRUPT: 120180908: -5090
KBKQQQ: PREV CORRUPT: 120180908: -994

KRKRBN: CORRUPT: 2460803244: -5034
KRKRBN: PREV CORRUPT: 2460803244: -938

KRKQQB: CORRUPT: 2381426860: -5084
KRKQQB: PREV CORRUPT: 2381426860: -988

KKQRRR: CORRUPT: 72179884: -5094
KKQRRR: PREV CORRUPT: 72179884: -998

zstd bfd8ad8
but since the nnothing really changed in the source except github workflows