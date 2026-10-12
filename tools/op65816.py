# 65816 opcode table: opcode -> (mnemonic, mode)
# modes: imp acc immM immX imm8 dp dpx dpy dpi dpxi dpiy dpil dpily abs absx absy
#        absl abslx absi absxi absil rel rel16 sr sriy blk
_T = """
00 brk imm8|01 ora dpxi|02 cop imm8|03 ora sr|04 tsb dp|05 ora dp|06 asl dp|07 ora dpil
08 php imp|09 ora immM|0a asl acc|0b phd imp|0c tsb abs|0d ora abs|0e asl abs|0f ora absl
10 bpl rel|11 ora dpiy|12 ora dpi|13 ora sriy|14 trb dp|15 ora dpx|16 asl dpx|17 ora dpily
18 clc imp|19 ora absy|1a inc acc|1b tcs imp|1c trb abs|1d ora absx|1e asl absx|1f ora abslx
20 jsr abs|21 and dpxi|22 jsl absl|23 and sr|24 bit dp|25 and dp|26 rol dp|27 and dpil
28 plp imp|29 and immM|2a rol acc|2b pld imp|2c bit abs|2d and abs|2e rol abs|2f and absl
30 bmi rel|31 and dpiy|32 and dpi|33 and sriy|34 bit dpx|35 and dpx|36 rol dpx|37 and dpily
38 sec imp|39 and absy|3a dec acc|3b tsc imp|3c bit absx|3d and absx|3e rol absx|3f and abslx
40 rti imp|41 eor dpxi|42 wdm imm8|43 eor sr|44 mvp blk|45 eor dp|46 lsr dp|47 eor dpil
48 pha imp|49 eor immM|4a lsr acc|4b phk imp|4c jmp abs|4d eor abs|4e lsr abs|4f eor absl
50 bvc rel|51 eor dpiy|52 eor dpi|53 eor sriy|54 mvn blk|55 eor dpx|56 lsr dpx|57 eor dpily
58 cli imp|59 eor absy|5a phy imp|5b tcd imp|5c jml absl|5d eor absx|5e lsr absx|5f eor abslx
60 rts imp|61 adc dpxi|62 per rel16|63 adc sr|64 stz dp|65 adc dp|66 ror dp|67 adc dpil
68 pla imp|69 adc immM|6a ror acc|6b rtl imp|6c jmp absi|6d adc abs|6e ror abs|6f adc absl
70 bvs rel|71 adc dpiy|72 adc dpi|73 adc sriy|74 stz dpx|75 adc dpx|76 ror dpx|77 adc dpily
78 sei imp|79 adc absy|7a ply imp|7b tdc imp|7c jmp absxi|7d adc absx|7e ror absx|7f adc abslx
80 bra rel|81 sta dpxi|82 brl rel16|83 sta sr|84 sty dp|85 sta dp|86 stx dp|87 sta dpil
88 dey imp|89 bit immM|8a txa imp|8b phb imp|8c sty abs|8d sta abs|8e stx abs|8f sta absl
90 bcc rel|91 sta dpiy|92 sta dpi|93 sta sriy|94 sty dpx|95 sta dpx|96 stx dpy|97 sta dpily
98 tya imp|99 sta absy|9a txs imp|9b txy imp|9c stz abs|9d sta absx|9e stz absx|9f sta abslx
a0 ldy immX|a1 lda dpxi|a2 ldx immX|a3 lda sr|a4 ldy dp|a5 lda dp|a6 ldx dp|a7 lda dpil
a8 tay imp|a9 lda immM|aa tax imp|ab plb imp|ac ldy abs|ad lda abs|ae ldx abs|af lda absl
b0 bcs rel|b1 lda dpiy|b2 lda dpi|b3 lda sriy|b4 ldy dpx|b5 lda dpx|b6 ldx dpy|b7 lda dpily
b8 clv imp|b9 lda absy|ba tsx imp|bb tyx imp|bc ldy absx|bd lda absx|be ldx absy|bf lda abslx
c0 cpy immX|c1 cmp dpxi|c2 rep imm8|c3 cmp sr|c4 cpy dp|c5 cmp dp|c6 dec dp|c7 cmp dpil
c8 iny imp|c9 cmp immM|ca dex imp|cb wai imp|cc cpy abs|cd cmp abs|ce dec abs|cf cmp absl
d0 bne rel|d1 cmp dpiy|d2 cmp dpi|d3 cmp sriy|d4 pei dp|d5 cmp dpx|d6 dec dpx|d7 cmp dpily
d8 cld imp|d9 cmp absy|da phx imp|db stp imp|dc jml absil|dd cmp absx|de dec absx|df cmp abslx
e0 cpx immX|e1 sbc dpxi|e2 sep imm8|e3 sbc sr|e4 cpx dp|e5 sbc dp|e6 inc dp|e7 sbc dpil
e8 inx imp|e9 sbc immM|ea nop imp|eb xba imp|ec cpx abs|ed sbc abs|ee inc abs|ef sbc absl
f0 beq rel|f1 sbc dpiy|f2 sbc dpi|f3 sbc sriy|f4 pea abs|f5 sbc dpx|f6 inc dpx|f7 sbc dpily
f8 sed imp|f9 sbc absy|fa plx imp|fb xce imp|fc jsr absxi|fd sbc absx|fe inc absx|ff sbc abslx
"""
OPS = {}
for chunk in _T.replace("\n", "|").split("|"):
    chunk = chunk.strip()
    if not chunk:
        continue
    o, m, mode = chunk.split()
    OPS[int(o, 16)] = (m, mode)
assert len(OPS) == 256

FIXED = {"imp": 0, "acc": 0, "imm8": 1, "dp": 1, "dpx": 1, "dpy": 1, "dpi": 1, "dpxi": 1,
         "dpiy": 1, "dpil": 1, "dpily": 1, "abs": 2, "absx": 2, "absy": 2, "absl": 3,
         "abslx": 3, "absi": 2, "absxi": 2, "absil": 2, "rel": 1, "rel16": 2, "sr": 1,
         "sriy": 1, "blk": 2}


def oplen(op, m, x):
    mn, mode = OPS[op]
    if mode == "immM":
        return 1 + (1 if m else 2)
    if mode == "immX":
        return 1 + (1 if x else 2)
    return 1 + FIXED[mode]
