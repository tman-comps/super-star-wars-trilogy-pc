
#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Ppu Ppu;
#define PPU_WIDE_MAX 96

#include "snes.h"
#include "statehandler.h"

typedef struct BgLayer {
  uint16_t hScroll;
  uint16_t vScroll;
  bool tilemapWider;
  bool tilemapHigher;
  uint16_t tilemapAdr;
  uint16_t tileAdr;
  bool bigTiles;
  bool mosaicEnabled;
} BgLayer;

typedef struct Layer {
  bool mainScreenEnabled;
  bool subScreenEnabled;
  bool mainScreenWindowed;
  bool subScreenWindowed;
} Layer;

typedef struct WindowLayer {
  bool window1enabled;
  bool window2enabled;
  bool window1inversed;
  bool window2inversed;
  uint8_t maskLogic;
} WindowLayer;

struct Ppu {
  Snes* snes;
  // vram access
  uint16_t vram[0x8000];
  uint16_t vramPointer;
  bool vramIncrementOnHigh;
  uint16_t vramIncrement;
  uint8_t vramRemapMode;
  uint16_t vramReadBuffer;
  // cgram access
  uint16_t cgram[0x100];
  uint8_t cgramPointer;
  bool cgramSecondWrite;
  uint8_t cgramBuffer;
  // oam access
  uint16_t oam[0x100];
  uint8_t highOam[0x20];
  uint8_t oamAdr;
  uint8_t oamAdrWritten;
  bool oamInHigh;
  bool oamInHighWritten;
  bool oamSecondWrite;
  uint8_t oamBuffer;
  // object/sprites
  bool objPriority;
  uint16_t objTileAdr1;
  uint16_t objTileAdr2;
  uint8_t objSize;
  uint8_t objPixelBuffer[256]; // line buffers
  uint8_t objPriorityBuffer[256];
  // ---- widescreen extension (port addition; inactive while wideExtra == 0)
  bool noSpriteLimit;            // port: draw every sprite on a line (no 32-sprite / 34-tile limit)
  int wideExtra;                 // extra pixels rendered on each side (<= PPU_WIDE_MAX)
  uint32_t* wideBuffer;          // (256 + 2*PPU_WIDE_MAX) x 240, 0x00RRGGBB, x offset PPU_WIDE_MAX
  int curScreenX;                // screen x of the pixel being rendered
  int curScreenY;                // screen line being rendered
  int (*wideTileHook)(void* ctx, int layer, int lx, int ly); // tile word, -1 = use VRAM, -2 = transparent
  void* wideCtx;
  int wideClipL, wideClipR;       // extra-area columns outside [L, R) are drawn black (past the end of the level)
  uint8_t objPixW[256 + 2 * 96];
  // ---- high-resolution Mode 7 (port addition; inactive while m7hiresK == 0)
  int m7hiresK;                  // supersampling factor (2..4)
  uint32_t* m7hiresBuf;          // (256*K) x (224*K), 0x00RRGGBB
  bool m7frameUsed;              // any Mode 7 pixel this frame
  struct { bool valid; int32_t sx, sy; int16_t a, c; bool xflip, large, fill; } m7line[240];
  uint32_t m7out[240][256 + 2 * PPU_WIDE_MAX];      // final colour per pixel (with brightness), x offset PPU_WIDE_MAX
  int16_t m7delta[240][256 + 2 * PPU_WIDE_MAX][3];  // final - raw (5-bit units) for Mode 7 pixels
  uint8_t m7is[240][256 + 2 * PPU_WIDE_MAX];        // pixel came from the Mode 7 layer
  int m7bright[240];
  int m7rawLayer, m7rawR, m7rawG, m7rawB;
  uint8_t objPriW[256 + 2 * 96];
  bool timeOver;
  bool rangeOver;
  bool objInterlace;
  // background layers
  BgLayer bgLayer[4];
  uint8_t scrollPrev;
  uint8_t scrollPrev2;
  uint8_t mosaicSize;
  uint8_t mosaicStartLine;
  // layers
  Layer layer[5];
  // mode 7
  int16_t m7matrix[8]; // a, b, c, d, x, y, h, v
  uint8_t m7prev;
  bool m7largeField;
  bool m7charFill;
  bool m7xFlip;
  bool m7yFlip;
  bool m7extBg;
  // mode 7 internal
  int32_t m7startX;
  int32_t m7startY;
  // windows
  WindowLayer windowLayer[6];
  uint8_t window1left;
  uint8_t window1right;
  uint8_t window2left;
  uint8_t window2right;
  // color math
  uint8_t clipMode;
  uint8_t preventMathMode;
  bool addSubscreen;
  bool subtractColor;
  bool halfColor;
  bool mathEnabled[6];
  uint8_t fixedColorR;
  uint8_t fixedColorG;
  uint8_t fixedColorB;
  // settings
  bool forcedBlank;
  uint8_t brightness;
  uint8_t mode;
  bool bg3priority;
  bool evenFrame;
  bool pseudoHires;
  bool overscan;
  bool frameOverscan; // if we are overscanning this frame (determined at 0,225)
  bool interlace;
  bool frameInterlace; // if we are interlacing this frame (determined at start vblank)
  bool directColor;
  // latching
  uint16_t hCount;
  uint16_t vCount;
  bool hCountSecond;
  bool vCountSecond;
  bool countersLatched;
  uint8_t ppu1openBus;
  uint8_t ppu2openBus;
  // pixel buffer (xbgr)
  // times 2 for even and odd frame
  uint8_t pixelBuffer[512 * 4 * 239 * 2];
  uint8_t pixelOutputFormat;
};

enum { ppu_pixelOutputFormatXBGR = 0, ppu_pixelOutputFormatBGRX = 1 };

Ppu* ppu_init(Snes* snes);
void ppu_free(Ppu* ppu);
void ppu_reset(Ppu* ppu);
void ppu_handleState(Ppu* ppu, StateHandler* sh);
bool ppu_checkOverscan(Ppu* ppu);
void ppu_handleVblank(Ppu* ppu);
void ppu_handleFrameStart(Ppu* ppu);
void ppu_runLine(Ppu* ppu, int line);
uint8_t ppu_read(Ppu* ppu, uint8_t adr);
void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val);
void ppu_putPixels(Ppu* ppu, uint8_t* pixels);
void ppu_renderMode7Hires(Ppu* ppu);
void ppu_setPixelOutputFormat(Ppu* ppu, int pixelOutputFormat);

#endif
