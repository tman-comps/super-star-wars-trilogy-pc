# Scene $13 reconstruction: layout (chunk IDs from the Death Star level bank $92, 256x256 px each).
# Upper corridor at row 4 (where the leftover spawn list puts Luke). The hex junction at column 4
# (chunk 13, which no shipped level uses) drops into a lower "compactor" corridor with the exit.
W, H = 10, 7
WALL = 24
ROWS = [[WALL] * W for _ in range(H)]
ROWS[4] = [9, 11, 8, 12, 13, 11, 8, 12, 19, 26]
ROWS[5] = [WALL, WALL, WALL, WALL, 15, 11, 8, 12, 19, 26]
GRID = bytes(c for r in ROWS for c in r)
