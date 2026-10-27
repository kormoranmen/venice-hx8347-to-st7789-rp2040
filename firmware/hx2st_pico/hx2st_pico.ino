// Przejsciowka HX8347 -> ST7789 / ILI9341 dla radiow na module Frontier Silicon Venice 6.5 (np. Numan One, Auna Connect 150)
// SPDX-License-Identifier: MIT
// Plytka: Raspberry Pi Pico albo RP2040-Zero (rdzen Arduino: Earle Philhower "Raspberry Pi Pico/RP2040")
//
// Radio steruje wyswietlaczem jak HX8347 (rejestry). Program podsluchuje te transmisje
// (PIO, bez obciazania linii), tlumaczy ja na komendy ST7789 / ILI9341 i wysyla do panelu:
//   rejestry 02..05 (okno, kolumny) -> CASET 2A
//   rejestry 06..09 (okno, wiersze) -> RASET 2B
//   rejestr  22 (zapis obrazu)      -> RAMWR 2C + piksele bez zmian (RGB565)
//   rejestr  16 (orientacja)        -> MADCTL 36
//   rejestr  28 (wyswietlanie)      -> DISPON 29 / DISPOFF 28
// Pozostale rejestry HX8347 (zasilanie, gamma) sa ignorowane - panel inicjalizuje Pico.
//
// Podlaczenie:
//   OD RADIA (linie, ktore szly do panelu)      DO PANELU ST7789 / ILI9341
//   SDA  -> GP2                                  GP8  -> D/C (RS)
//   SCK  -> GP3                                  GP9  -> CS
//   D/C  -> GP4                                  GP10 -> SCL
//   CS   -> GP5                                  GP11 -> SDA
//   (RESET z radia nie jest potrzebny)           GP12 -> RESET
//   GND radia -> GND Pico                        VCC panelu -> 3,3 V, GND -> GND
//   Podswietlenie panelu (A/K) - jak bylo, z radia.
//   Zasilanie Pico: na testy z USB; docelowo 3,3 V lub 5 V z radia na pin VSYS (Pico) / 5V (Zero).
//
// Zworki (sprawdzane przy wlaczeniu, wewnetrzne podciagniecie do plusa):
//   GP6: rozwarty = panel ST7789,      zwarty do GND = panel ILI9341
//   GP7: rozwarty = obraz normalnie,   zwarty do GND = obraz obrocony o 180 stopni
//   GP13: rozwarty = kolory normalnie, zwarty do GND = kolory odwrocone (panele IPS - "negatyw")
//
// Arduino IDE: Narzedzia -> CPU Speed -> 250 MHz (Overclock) (zalecane).
// Serial Monitor (115200) co sekunde pokazuje statystyki (tylko gdy port USB jest otwarty).

#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"

// --- piny: wejscia z radia (musza byc kolejne: SDA, SCK, DC, CS) ---
const uint PIN_IN_SDA = 2;
const uint PIN_IN_SCK = PIN_IN_SDA + 1;
const uint PIN_IN_DC  = PIN_IN_SDA + 2;
const uint PIN_IN_CS  = PIN_IN_SDA + 3;

// --- zworki konfiguracyjne ---
const uint PIN_SEL_PANEL = 6, PIN_SEL_ROT = 7, PIN_SEL_INV = 13;

// --- piny: wyjscie do panelu (SPI1) ---
const uint PIN_LCD_DC = 8, PIN_LCD_CS = 9, PIN_LCD_SCK = 10, PIN_LCD_MOSI = 11, PIN_LCD_RST = 12;
#define LCD_SPI spi1
const uint32_t LCD_HZ = 31250000;

// Korekty obrazu, gdyby byl lustrzany / obrocony / mial zamieniony czerwony z niebieskim:
//   0x80 = odbicie w poziomie (napisy), 0x40 = odbicie w pionie, 0x20 = zamien osie, 0x08 = zamien R z B
const uint8_t MADCTL_XOR_ST7789  = 0x48;  // dobrane na radiu: odbicie + panel ma kolejnosc RGB
const uint8_t MADCTL_XOR_ILI9341 = 0xC0;  // sprawdzone na radiu: odbicie, kolejnosc BGR (jak radio)
const uint8_t MADCTL_ROT180      = 0xC0;  // dokladane przy zworce GP7
const bool INVERT_ST7789 = false;          // true, jesli kolory wychodza jak w negatywie (panel IPS)
const bool INVERT_ILI9341 = false;

static bool panelILI = false;
static uint8_t madXor = MADCTL_XOR_ST7789;
static bool lcdInvert = false;
static bool rot180 = false;
static bool invJumper = false;

// Sposob odczytu bitu z radia:
//   0 = tuz PRZED zboczem narastajacym SCK (ciagle probkowanie, gdy SCK nisko)
//   1 = tuz po zboczu narastajacym - domyslny (zmierzone: dane zmieniaja sie przy zboczu opadajacym)
//   2 = na zboczu opadajacym
const int SAMPLE_MODE = 1;

// ---------------------------------------------------------------------------
// Bufor pierscieniowy zapelniany przez DMA z PIO (kazde slowo = 1 bajt z radia)
static uint32_t ring[8192] __attribute__((aligned(32768)));
static const uint32_t RING_MASK = 8191;
static int dmaCh;
static uint32_t rd = 0;
static PIO pio = pio0;
static uint sm = 0;
static uint progOff;                             // adres programu PIO (do resynchronizacji)

// Program PIO (adresy wzgledne, pio_add_program je przesunie). Indeksy pinow wzgledem GP2:
// 0 = SDA, 1 = SCK, 2 = DC, 3 = CS. Kazde slowo w FIFO: bit9 = DC, bit8 = SCK, bity 7..0 = bajt.
static uint16_t prog[16];
static int progLen;

static void buildProgram() {
  int n = 0;
  if (SAMPLE_MODE == 0) {
    // probkowanie wszystkich pinow w petli, dopoki SCK nisko; ostatnia probka sprzed zbocza
    prog[n++] = pio_encode_mov(pio_isr, pio_null);   // 0
    prog[n++] = pio_encode_wait_pin(false, 3);       // 1: CS nisko
    prog[n++] = pio_encode_wait_pin(false, 1);       // 2: SCK nisko
    prog[n++] = pio_encode_mov(pio_osr, pio_pins);   // 3: probka
    prog[n++] = pio_encode_jmp_pin(6);               // 4: SCK wysoko -> koniec bitu
    prog[n++] = pio_encode_jmp(3);                   // 5
    prog[n++] = pio_encode_in(pio_osr, 3);           // 6: DC, SCK(0), SDA
    prog[n++] = pio_encode_set(pio_x, 6);            // 7
    prog[n++] = pio_encode_wait_pin(false, 1);       // 8: SCK nisko
    prog[n++] = pio_encode_mov(pio_osr, pio_pins);   // 9
    prog[n++] = pio_encode_jmp_pin(12);              // 10
    prog[n++] = pio_encode_jmp(9);                   // 11
    prog[n++] = pio_encode_in(pio_osr, 1);           // 12: SDA
    prog[n++] = pio_encode_jmp_x_dec(8);             // 13
    prog[n++] = pio_encode_push(false, false);       // 14
  } else {
    const bool e1 = (SAMPLE_MODE == 2), e2 = !e1;
    prog[n++] = pio_encode_mov(pio_isr, pio_null);   // 0
    prog[n++] = pio_encode_wait_pin(false, 3);       // 1: CS nisko
    prog[n++] = pio_encode_wait_pin(e1, 1);          // 2
    prog[n++] = pio_encode_wait_pin(e2, 1);          // 3: zbocze odczytu
    prog[n++] = pio_encode_jmp_pin(0);               // 4: CS wysoko -> porzuc
    prog[n++] = pio_encode_in(pio_pins, 3);          // 5
    prog[n++] = pio_encode_set(pio_x, 6);            // 6
    prog[n++] = pio_encode_wait_pin(e1, 1);          // 7
    prog[n++] = pio_encode_wait_pin(e2, 1);          // 8
    prog[n++] = pio_encode_jmp_pin(0);               // 9
    prog[n++] = pio_encode_in(pio_pins, 1);          // 10
    prog[n++] = pio_encode_jmp_x_dec(7);             // 11
    prog[n++] = pio_encode_push(false, false);       // 12
  }
  progLen = n;
}

static void setupSniffer() {
  buildProgram();
  pio_program_t p = {};
  p.instructions = prog;
  p.length = progLen;
  p.origin = -1;
  uint off = pio_add_program(pio, &p);
  progOff = off;

  for (uint pin = PIN_IN_SDA; pin <= PIN_IN_CS; pin++) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);
  }

  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, off + 0, off + progLen - 1);
  sm_config_set_in_pins(&c, PIN_IN_SDA);
  sm_config_set_jmp_pin(&c, SAMPLE_MODE == 0 ? PIN_IN_SCK : PIN_IN_CS);
  sm_config_set_in_shift(&c, false, false, 32);   // przesuw w lewo, bez autopush
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  sm_config_set_clkdiv(&c, 1.0f);
  pio_sm_init(pio, sm, off, &c);

  dmaCh = dma_claim_unused_channel(true);
  dma_channel_config dc = dma_channel_get_default_config(dmaCh);
  channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
  channel_config_set_read_increment(&dc, false);
  channel_config_set_write_increment(&dc, true);
  channel_config_set_ring(&dc, true, 15);          // pierscien 32 KB
  channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));
  dma_channel_configure(dmaCh, &dc, ring, &pio->rxf[sm], 0xFFFFFFFF, true);

  pio_sm_set_enabled(pio, sm, true);
}

static inline uint32_t ringWritePos() {
  return ((dma_hw->ch[dmaCh].write_addr - (uint32_t)ring) >> 2) & RING_MASK;
}

// ---------------------------------------------------------------------------
// Panel ST7789
static uint8_t pixBuf[1024];
static uint32_t pixN = 0;
static bool inRam = false;

static void lcdFlush() {
  if (pixN) { spi_write_blocking(LCD_SPI, pixBuf, pixN); pixN = 0; }
}

static void lcdEndRam() {
  if (inRam) { lcdFlush(); gpio_put(PIN_LCD_CS, 1); inRam = false; }
}

static void lcdCmd(uint8_t c, const uint8_t *d = nullptr, size_t n = 0) {
  lcdEndRam();
  gpio_put(PIN_LCD_CS, 0);
  gpio_put(PIN_LCD_DC, 0);
  spi_write_blocking(LCD_SPI, &c, 1);
  gpio_put(PIN_LCD_DC, 1);
  if (n) spi_write_blocking(LCD_SPI, d, n);
  gpio_put(PIN_LCD_CS, 1);
}

#define LC(c, ...) do { const uint8_t _d[] = {__VA_ARGS__}; lcdCmd(c, _d, sizeof(_d)); } while (0)

static void lcdBeginRam() {
  lcdEndRam();
  gpio_put(PIN_LCD_CS, 0);
  gpio_put(PIN_LCD_DC, 0);
  uint8_t c = 0x2C;
  spi_write_blocking(LCD_SPI, &c, 1);
  gpio_put(PIN_LCD_DC, 1);
  inRam = true;
}

static inline void lcdPixByte(uint8_t b) {
  pixBuf[pixN++] = b;
  if (pixN == sizeof(pixBuf)) lcdFlush();
}

static void lcdInit() {
  spi_init(LCD_SPI, LCD_HZ);
  spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(PIN_LCD_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);
  const uint outs[3] = {PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_RST};
  for (uint p : outs) { gpio_init(p); gpio_set_dir(p, GPIO_OUT); gpio_put(p, 1); }

  gpio_put(PIN_LCD_RST, 0); sleep_ms(20);
  gpio_put(PIN_LCD_RST, 1); sleep_ms(150);
  lcdCmd(0x01); sleep_ms(150);                 // Software Reset
  if (panelILI) {
    LC(0xEF, 0x03, 0x80, 0x02);
    LC(0xCF, 0x00, 0xC1, 0x30);
    LC(0xED, 0x64, 0x03, 0x12, 0x81);
    LC(0xE8, 0x85, 0x00, 0x78);
    LC(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);
    LC(0xF7, 0x20);
    LC(0xEA, 0x00, 0x00);
    LC(0xC0, 0x23);
    LC(0xC1, 0x10);
    LC(0xC5, 0x3E, 0x28);
    LC(0xC7, 0x86);
    LC(0x3A, 0x55);                            // 16 bit/piksel
    LC(0xB1, 0x00, 0x18);
    LC(0xB6, 0x08, 0x82, 0x27);
    LC(0xF2, 0x00);
    LC(0x26, 0x01);
    LC(0xE0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00);
    LC(0xE1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);
    lcdCmd(0x11); sleep_ms(120);               // Sleep Out
  } else {
    lcdCmd(0x11); sleep_ms(120);               // Sleep Out
    LC(0x3A, 0x55);                            // 16 bit/piksel
    LC(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33);
    LC(0xB7, 0x35);
    LC(0xBB, 0x19);
    LC(0xC0, 0x2C);
    LC(0xC2, 0x01);
    LC(0xC3, 0x12);
    LC(0xC4, 0x20);
    LC(0xC6, 0x0F);
    LC(0xD0, 0xA4, 0xA1);
    LC(0xE0, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23);
    LC(0xE1, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23);
  }
  lcdCmd(lcdInvert ? 0x21 : 0x20);
  LC(0x36, (uint8_t)(0x08 ^ madXor));          // jak radio (rejestr 16 = 08)
  lcdCmd(0x13);
  lcdCmd(0x29);                                // Display On
}

// Ekran startowy: ciemnoniebieski z bialym paskiem u gory - widac, ze Pico steruje panelem
static void lcdTestPattern() {
  LC(0x2A, 0, 0, 0, 239);
  LC(0x2B, 0, 0, 0x01, 0x3F);
  lcdBeginRam();
  for (uint32_t i = 0; i < 240UL * 320UL; i++) {
    uint16_t c = (i < 240UL * 16UL) ? 0xFFFF : 0x0008;
    lcdPixByte(c >> 8);
    lcdPixByte(c & 0xFF);
  }
  lcdEndRam();
}

// ---------------------------------------------------------------------------
// Tlumaczenie protokolu HX8347
static uint8_t hxReg = 0;
static uint8_t win[8] = {0, 0, 0, 239, 0, 0, 0x01, 0x3F};   // rejestry 02..09
static bool pixMode = false;

// statystyki
static uint32_t stBytes = 0, stCmd = 0, stPix = 0, stFrames = 0, stBad = 0, stOverrun = 0;
static uint8_t lastMad = 0x08;

static void handleByte(uint32_t w) {
  // poziom SCK zapisany przy pierwszym bicie musi sie zgadzac z wybranym zboczem
  if (((w >> 8) & 1) != (SAMPLE_MODE == 1 ? 1u : 0u)) { stBad++; return; }
  bool dc = w & 0x200;
  uint8_t b = w & 0xFF;
  stBytes++;

  if (!dc) {                                  // komenda = numer rejestru HX8347
    stCmd++;
    hxReg = b;
    if (b == 0x22) {
      LC(0x2A, win[0], win[1], win[2], win[3]);
      LC(0x2B, win[4], win[5], win[6], win[7]);
      lcdBeginRam();
      pixMode = true;
      stFrames++;
    } else {
      lcdEndRam();
      pixMode = false;
    }
    return;
  }

  if (pixMode) { lcdPixByte(b); stPix++; return; }

  // dane do rejestru
  if (hxReg >= 0x02 && hxReg <= 0x09) {
    win[hxReg - 0x02] = b;
  } else if (hxReg == 0x16) {
    lastMad = b;
    LC(0x36, (uint8_t)((b & 0xFC) ^ madXor));
  } else if (hxReg == 0x28) {
    lcdCmd((b & 0x04) ? 0x29 : 0x28);
  }
}

static void readJumpers() {
  const uint sel[3] = {PIN_SEL_PANEL, PIN_SEL_ROT, PIN_SEL_INV};
  for (uint p : sel) { gpio_init(p); gpio_set_dir(p, GPIO_IN); gpio_pull_up(p); }
  sleep_ms(5);
  panelILI = !gpio_get(PIN_SEL_PANEL);
  rot180 = !gpio_get(PIN_SEL_ROT);
  madXor = (panelILI ? MADCTL_XOR_ILI9341 : MADCTL_XOR_ST7789) ^ (rot180 ? MADCTL_ROT180 : 0);
  invJumper = !gpio_get(PIN_SEL_INV);
  lcdInvert = (panelILI ? INVERT_ILI9341 : INVERT_ST7789) ^ invJumper;
}

void setup() {
  Serial.begin(115200);
  readJumpers();
  lcdInit();
  lcdTestPattern();
  setupSniffer();
}

void loop() {
  uint32_t wp = ringWritePos();
  uint32_t avail = (wp - rd) & RING_MASK;
  if (avail > RING_MASK - 256) stOverrun++;
  while (rd != wp) {
    handleByte(ring[rd]);
    rd = (rd + 1) & RING_MASK;
  }
  // gdy radio przestaje nadawac, dopchnij resztke pikseli do panelu
  if (inRam && pixN && ((ringWritePos() - rd) & RING_MASK) == 0) lcdFlush();

  if (!dma_channel_is_busy(dmaCh)) dma_channel_set_trans_count(dmaCh, 0xFFFFFFFF, true);

  // Resynchronizacja: Numan trzyma CS stale aktywne, wiec CS nie wyrownuje bajtow. Jesli konwerter
  // wystartowal w srodku bajtu, odczyt bylby przesuniety na stale. Po przerwie w transmisji (koniec
  // paska obrazu) program PIO zaczyna od nowa i odrzuca niepelny bajt.
  static uint32_t lastWp = 0, lastRx = 0;
  static bool synced = false;
  if (wp != lastWp) { lastWp = wp; lastRx = micros(); synced = false; }
  else if (!synced && micros() - lastRx > 300) {
    pio_sm_exec(pio, sm, pio_encode_jmp(progOff));
    synced = true;
  }

  static uint32_t t = 0;
  if (millis() - t >= 1000) {
    t = millis();
    if (Serial) Serial.printf("%s%s%s | CPU %lu MHz | bajty %lu komendy %lu obrazy %lu piksele(B) %lu | zle %lu przepelnienia %lu | okno %u..%u x %u..%u | rej16=%02X\n",
                  panelILI ? "ILI9341" : "ST7789", rot180 ? " obrot180" : "", invJumper ? " inwersja" : "",
                  (unsigned long)(clock_get_hz(clk_sys) / 1000000),
                  (unsigned long)stBytes, (unsigned long)stCmd, (unsigned long)stFrames, (unsigned long)stPix,
                  (unsigned long)stBad, (unsigned long)stOverrun,
                  (win[0] << 8) | win[1], (win[2] << 8) | win[3], (win[4] << 8) | win[5], (win[6] << 8) | win[7],
                  lastMad);
    stBytes = stCmd = stPix = stFrames = 0;
  }
}
