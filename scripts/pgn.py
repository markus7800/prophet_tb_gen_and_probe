
s = "KNPKRN:   197 b4a5 c2a2 a5b6 a2d2 b3d2 a6b8 d7d8b b8d7 b6c7 d7c5 c7c6 c5d3 d2e4 d3e5 c6c7 e5d3 d8f6 a8a7 c7c6 d3b4 c6b5 b4d5 f6d8 a7a8 e4d6 a8b8 b5c5 d5f4 c5c6 f4d3 d8f6 b8a7 c6b5 d3e1 f6d4 a7b8 b5b6 e1d3 d4a1 d3f4 a1e5 f4g6 e5h2 g6e7 d6f5 b8a8 f5d4 e7f5 d4b5 f5d4 b5c7 a8b8 c7e6 b8a8 e6c5 d4c2 c5a4 c2e3 b6a6 e3d5 h2d6 d5c7 a6b6 c7d5 b6c6 d5e3 d6c5 e3f5 c6c7 f5h4 a4c3 h4g2 c7c6 g2e1 c3d5 a8b8 d5e7 e1f3 c5e3 f3e5 c6d5 e5d7 d5d6 d7f6 e3d4 f6h7 d6c6 h7g5 c6b6 b8a8 d4e5 g5f3 e5f4 f3e1 e7d5 e1g2 f4g5 g2e1 g5h4 e1d3 h4g3 d3b4 d5c7 a8b8 b6c5 b4c2 c7b5 b8b7 b5d6 b7a6 d6c4 a6b7 g3f4 c2e1 c4e5 e1g2 f4g5 b7c8 e5d3 c8b7 c5d4 b7a6 d3b4 a6a5 b4c2 a5a4 d4d3 a4b5 d3e2 b5a4 c2d4 a4b4 d4f3 b4b3 e2f1 b3c2 f3e5 c2c3 f1g2 c3d4 e5d7 d4c3 d7b6 c3b4 g5e3 b4b5 g2f3 b5c6 f3e4 c6d6 e3d4 d6c6 e4e5 c6c7 e5d5 c7b7 d5d6 b7a6 d6c6 a6a7 d4e5 a7a6 e5b8 a6a5 b6d5 a5a4 b8e5 a4b3 d5e3 b3b4 c6b6 b4a4 e5c3 a4b3 c3e1 b3a4 e1d2 a4b3 b6a5 b3a3 d2e1 a3b2 a5a4 b2a2 e3d1 a2a1 a4b3 a1b1 e1d2 b1a1 d2c1 a1b1 c1a3 b1a1 a3b2 a1b1 d1c3"

fen = "k7/3P4/n7/8/1K6/1N6/2r5/8 w - -"
moves = "b4a5 c2a2 a5b6 a2d2 b3d2 a6b8 d7d8b b8d7 b6c7 d7c5 c7c6 c5d3 d2e4 d3e5 c6c7 e5d3 d8f6 a8a7 c7c6 d3b4 c6b5 b4d5 f6d8 a7a8 e4d6 a8b8 b5c5 d5f4 c5c6 f4d3 d8f6 b8a7 c6b5 d3e1 f6d4 a7b8 b5b6 e1d3 d4a1 d3f4 a1e5 f4g6 e5h2 g6e7 d6f5 b8a8 f5d4 e7f5 d4b5 f5d4 b5c7 a8b8 c7e6 b8a8 e6c5 d4c2 c5a4 c2e3 b6a6 e3d5 h2d6 d5c7 a6b6 c7d5 b6c6 d5e3 d6c5 e3f5 c6c7 f5h4 a4c3 h4g2 c7c6 g2e1 c3d5 a8b8 d5e7 e1f3 c5e3 f3e5 c6d5 e5d7 d5d6 d7f6 e3d4 f6h7 d6c6 h7g5 c6b6 b8a8 d4e5 g5f3 e5f4 f3e1 e7d5 e1g2 f4g5 g2e1 g5h4 e1d3 h4g3 d3b4 d5c7 a8b8 b6c5 b4c2 c7b5 b8b7 b5d6 b7a6 d6c4 a6b7 g3f4 c2e1 c4e5 e1g2 f4g5 b7c8 e5d3 c8b7 c5d4 b7a6 d3b4 a6a5 b4c2 a5a4 d4d3 a4b5 d3e2 b5a4 c2d4 a4b4 d4f3 b4b3 e2f1 b3c2 f3e5 c2c3 f1g2 c3d4 e5d7 d4c3 d7b6 c3b4 g5e3 b4b5 g2f3 b5c6 f3e4 c6d6 e3d4 d6c6 e4e5 c6c7 e5d5 c7b7 d5d6 b7a6 d6c6 a6a7 d4e5 a7a6 e5b8 a6a5 b6d5 a5a4 b8e5 a4b3 d5e3 b3b4 c6b6 b4a4 e5c3 a4b3 c3e1 b3a4 e1d2 a4b3 b6a5 b3a3 d2e1 a3b2 a5a4 b2a2 e3d1 a2a1 a4b3 a1b1 e1d2 b1a1 d2c1 a1b1 c1a3 b1a1 a3b2 a1b1 d1c3"

import chess
import chess.pgn

game = chess.pgn.Game()
game.setup(fen)
game.add_line([chess.Move.from_uci(move) for move in moves.split()])

del game.headers["Event"]
del game.headers["Site"]
del game.headers["Date"]
del game.headers["Round"]
del game.headers["White"]
del game.headers["Black"]
del game.headers["SetUp"]
del game.headers["Result"]
s = str(game)
print(s)

# https://lichess.org/analysis/pgn/%5BFEN+%22k7%2F3P4%2Fn7%2F8%2F1K6%2F1N6%2F2r5%2F8+w+-+-+0+1%22%5D%0A%0A1.+Ka5+Ra2%2B+2.+Kb6+Rd2+3.+Nxd2+Nb8+4.+d8%3DB+Nd7%2B+5.+Kc7+Nc5+6.+Kc6+Nd3+7.+Ne4+Ne5%2B+8.+Kc7+Nd3+9.+Bf6+Ka7+10.+Kc6+Nb4%2B+11.+Kb5+Nd5+12.+Bd8+Ka8+13.+Nd6+Kb8+14.+Kc5+Nf4+15.+Kc6+Nd3+16.+Bf6+Ka7+17.+Kb5+Ne1+18.+Bd4%2B+Kb8+19.+Kb6+Nd3+20.+Ba1+Nf4+21.+Be5+Ng6+22.+Bh2+Ne7+23.+Nf5%2B+Ka8+24.+Nd4+Nf5+25.+Nb5+Nd4+26.+Nc7%2B+Kb8+27.+Ne6%2B+Ka8+28.+Nc5+Nc2+29.+Na4+Ne3+30.+Ka6+Nd5+31.+Bd6+Nc7%2B+32.+Kb6+Nd5%2B+33.+Kc6+Ne3+34.+Bc5+Nf5+35.+Kc7+Nh4+36.+Nc3+Ng2+37.+Kc6+Ne1+38.+Nd5+Kb8+39.+Ne7+Nf3+40.+Be3+Ne5%2B+41.+Kd5+Nd7+42.+Kd6+Nf6+43.+Bd4+Nh7+44.+Kc6+Ng5+45.+Kb6+Ka8+46.+Be5+Nf3+47.+Bf4+Ne1+48.+Nd5+Ng2+49.+Bg5+Ne1+50.+Bh4+Nd3+51.+Bg3+Nb4+52.+Nc7%2B+Kb8+53.+Kc5+Nc2+54.+Nb5%2B+Kb7+55.+Nd6%2B+Ka6+56.+Nc4+Kb7+57.+Bf4+Ne1+58.+Ne5+Ng2+59.+Bg5+Kc8+60.+Nd3+Kb7+61.+Kd4+Ka6+62.+Nb4%2B+Ka5+63.+Nc2+Ka4+64.+Kd3+Kb5+65.+Ke2+Ka4+66.+Nd4+Kb4+67.+Nf3+Kb3+68.+Kf1+Kc2+69.+Ne5+Kc3+70.+Kxg2+Kd4+71.+Nd7+Kc3+72.+Nb6+Kb4+73.+Be3+Kb5+74.+Kf3+Kc6+75.+Ke4+Kd6+76.+Bd4+Kc6+77.+Ke5+Kc7+78.+Kd5+Kb7+79.+Kd6+Ka6+80.+Kc6+Ka7+81.+Be5+Ka6+82.+Bb8+Ka5+83.+Nd5+Ka4+84.+Be5+Kb3+85.+Ne3+Kb4+86.+Kb6+Ka4+87.+Bc3+Kb3+88.+Be1+Ka4+89.+Bd2+Kb3+90.+Ka5+Ka3+91.+Be1+Kb2+92.+Ka4+Ka2+93.+Nd1+Ka1+94.+Kb3+Kb1+95.+Bd2+Ka1+96.+Bc1+Kb1+97.+Ba3+Ka1+98.+Bb2%2B+Kb1+99.+Nc3%23+%2A
import urllib.parse
print("https://lichess.org/analysis/pgn/"+urllib.parse.quote_plus(s)+"#1")