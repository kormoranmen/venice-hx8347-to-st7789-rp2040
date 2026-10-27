# Konwerter wyświetlacza HX8347 → ST7789 / ILI9341

> 🇬🇧 English version: [README.md](README.md)

Instrukcja dla przejściówki na **Raspberry Pi Pico** albo **RP2040-Zero** do radiów na module **Frontier Silicon Venice 6.5** (sprawdzone: Numan One, Auna Connect 150).

---

## 1. Do czego to służy

Firmware tych radiów steruje wyświetlaczem tak, jakby miał sterownik **HX8347** (stary sterownik firmy Himax, dziś prawie niedostępny). Tanie i łatwo dostępne panele 2,4″ i 2,8″ 240×320 mają sterownik **ST7789** albo **ILI9341**, które rozumieją inne komendy. Wpięte bezpośrednio do radia pokazują **biały ekran**.

Konwerter:

1. **podsłuchuje** transmisję radia do wyświetlacza (sprzętowo, przez PIO, bez obciążania linii),
2. **tłumaczy** komendy HX8347 na ST7789/ILI9341:

   | Radio (HX8347) | Panel (ST7789 / ILI9341) |
   |---|---|
   | rejestry `02`–`05` (okno, kolumny) | `2A` CASET |
   | rejestry `06`–`09` (okno, wiersze) | `2B` RASET |
   | rejestr `22` (zapis obrazu) | `2C` RAMWR + piksele bez zmian (RGB565) |
   | rejestr `16` (orientacja) | `36` MADCTL |
   | rejestr `28` (włączenie wyświetlania) | `29` / `28` |

**3.** **inicjalizuje panel samodzielnie** przy włączeniu zasilania (inicjalizacja HX8347 wysyłana przez radio jest ignorowana),

**4.** wysyła obraz do panelu własnym, szybkim SPI (31 MHz).

---

## 2. Co jest potrzebne

- płytka **Raspberry Pi Pico** albo **RP2040-Zero** (Waveshare lub klon),
- panel TFT **240×320, SPI 4-przewodowy (z linią D/C)**, sterownik **ST7789** lub **ILI9341**, np. 2,8″ albo 2,4″ z taśmą 10 pin,
- cienkie przewody (najlepiej drut nawojowy), lutownica, topnik.

---

## 3. Wgranie programu

Zawartość repozytorium:

| Ścieżka | Zawartość |
|---|---|
| `bin/hx2st_PICO.uf2` | gotowy program dla Raspberry Pi Pico |
| `bin/hx2st_RP2040-ZERO.uf2` | gotowy program dla RP2040-Zero |
| `firmware/hx2st_pico/hx2st_pico.ino` | kod źródłowy (Arduino) |
| `docs/` | ta instrukcja w PDF (po polsku i po angielsku) |

**Wgrywanie bez Arduino IDE:**

1. Przytrzymaj przycisk **BOOT** (na Pico: **BOOTSEL**).
2. Trzymając go, podłącz płytkę kablem USB do komputera i puść przycisk.
3. Pojawi się dysk **RPI-RP2**.
4. Przeciągnij na niego właściwy plik `.uf2`. Płytka sama się zrestartuje i uruchomi program.

Jeśli dysk się nie pojawia, użyj innego kabla. Część kabli USB-C służy tylko do ładowania.

**Kompilacja ze źródła (opcjonalnie):** Arduino IDE z rdzeniem *„Raspberry Pi Pico/RP2040/RP2350” (Earle F. Philhower)*, płytka *Raspberry Pi Pico* lub *Waveshare RP2040 Zero*, **CPU Speed: 250 MHz (Overclock)**.

---

## 4. Podłączenie

### 4.1 Od radia do konwertera (wejścia)

| Linia w radiu | Pico / RP2040-Zero |
|---|---|
| **SDA** (dane) | **GP2** |
| **SCK** (zegar) | **GP3** |
| **D/C** (RS, komenda/dane) | **GP4** |
| **CS** (wybór wyświetlacza) | **GP5** |
| **GND** | **GND** |

> **⚠ Ważne: sygnały bierz bezpośrednio przy module Venice, PRZED rezystorami szeregowymi** (w Numanie One: 3,3 kΩ na każdej linii). Za rezystorami, na długiej taśmie i przewodach, impulsy zegara (ok. 5 MHz, ok. 100 ns) zanikają i konwerter gubi dane.
>
> Właściwy koniec rezystora: ten, który ma **ok. 0 Ω** do pinu modułu (sprawdź miernikiem przy wyłączonym radiu).

Linii **RESET** z radia nie podłączasz, konwerter jej nie używa.

### 4.2 Od konwertera do panelu (wyjścia)

| Pico / RP2040-Zero | Panel | Nr pinu (taśma 10 pin) | Nr pinu (Auna, goldpin 9 pin) |
|---|---|---|---|
| **GP8** | RS / D/C | 2 | 1 |
| **GP9** | CS | 3 | 2 |
| **GP10** | SCL (zegar) | 4 | 3 |
| **GP11** | SDA (dane) | 5 | 4 |
| **GP12** | RESET | 6 | 5 |
| **3V3** | VCC | 7 | 6 |
| **GND** | GND | 1 i 8 | 7 |
| *(z radia, jak było)* | A (LED+) | 9 | 8 |
| *(z radia, jak było)* | K (LED−) | 10 | 9 |

Układ pinów taśmy 10 pin. Taki sam mają oryginalne panele HX8347 z tych radiów (Numan One/Two) i tanie zamienniki ST7789:

```
 1 GND   2 RS(D/C)   3 CS   4 SCL   5 SDA   6 RESET   7 VCC   8 GND   9 A(LED+)   10 K(LED-)
```

W **Aunie Connect 150** płytka wyświetlacza łączy się z radiem listwą goldpin **9 pin**. Układ jest taki sam jak w taśmie 10 pin, tylko bez pierwszego pinu GND:

```
 1 RS(D/C)   2 CS   3 SCL   4 SDA   5 RESET   6 VCC   7 GND   8 A(LED+)   9 K(LED-)
```

> **⚠ Panel może dostawać sygnał tylko z jednego źródła, czyli z konwertera.** Jeśli stara ścieżka z radia nadal prowadzi do złącza panelu, odetnij ją: wylutuj rezystory szeregowe albo przetnij ścieżki za punktem odczepu.

### 4.3 Zasilanie

- **Na stałe:** z radia. Na **Pico**: 3,3–5 V na pin **VSYS**. Na **RP2040-Zero**: 5 V na pin **5V** albo 3,3 V na pin **3V3**. Masy radia i konwertera muszą być połączone.
- **Do diagnostyki:** z USB komputera.
- Nie zasilaj płytki jednocześnie z 5 V radia i z USB.

### 4.4 Zworki konfiguracyjne

Sprawdzane **raz, przy włączeniu zasilania** (po zmianie zrestartuj płytkę). Zworkę zwierasz **do GND**.

| Pin | Rozwarty (nic) | Zwarty do GND |
|---|---|---|
| **GP6** | panel **ST7789** | panel **ILI9341** |
| **GP7** | obraz normalnie | obraz **obrócony o 180°** |
| **GP13** | kolory normalnie | kolory **odwrócone** (panele IPS: biały zamiast czarnego) |

Sprawdzone ustawienia:

| Radio | Panel | GP6 | GP7 | GP13 |
|---|---|---|---|---|
| Numan One | 2,8″ ST7789 (TN, 10 pin) | – | – | – |
| Auna Connect 150 | 2,4″ ST7789V IPS (FPC 10 pin) | – | GND | GND |
| Numan One | ILI9341 (moduł SPI) | GND | – | – |

### 4.5 Schemat

```
  RADIO (przy module Venice,       KONWERTER                 PANEL
         PRZED rezystorami)        Pico / RP2040-Zero        ST7789 / ILI9341

  SDA ---------------------------> GP2        GP8  -------> RS (D/C)
  SCK ---------------------------> GP3        GP9  -------> CS
  D/C ---------------------------> GP4        GP10 -------> SCL
  CS  ---------------------------> GP5        GP11 -------> SDA
  GND ---------------------------> GND        GP12 -------> RESET
  5V lub 3,3V -------------------> 5V / VSYS  3V3  -------> VCC
                                              GND  -------> GND
                                   GP6, GP7, GP13 -> zworki do GND
  podswietlenie LED+/LED- --------------------------------> A / K
```

---

### 4.6 Przykład montażu: Auna Connect 150

- **Panel:** 2,4″ 240×320 ST7789V IPS z taśmą FPC **10 pin**, o takim samym układzie jak oryginał. Kupiony jako moduł „Wyświetlacz LCD TFT 2.4 240x320 SPI 8 pin ST7789V”: niebieska płytka **GMT024-08-SPI8P** z listwą 8 pin (GND VCC SCL SDA RST DC CS BL) i złączem FPC 10 pin z tyłu. „8 pin” w nazwie to listwa płytki, nie taśma panelu. Panel odklejony od tej płytki i przylutowany taśmą w miejsce oryginalnego panelu na płytce wyświetlacza radia (PCB-WK28055V1).
- **Konwerter:** RP2040-Zero przyklejony z tyłu płytki wyświetlacza i wpięty w szereg. Ścieżki między listwą goldpin (9 pin, od radia) a panelem są przecięte. Listwa idzie na wejścia GP2–GP5, a wyjścia GP8–GP12 na pady panelu.
- **Zasilanie** z radia (VCC i GND z listwy).
- **Zworki:** GP7 i GP13 do GND (obrót o 180° i odwrócone kolory panelu IPS).

![Auna Connect 150 z nowym panelem](img/auna150_dzialajacy.jpg)

![Płytka wyświetlacza z nowym panelem 2,4″](img/auna150_panel_przod.jpg)

![RP2040-Zero z tyłu płytki wyświetlacza](img/auna150_konwerter_tyl.jpg)

## 5. Uruchomienie: czego się spodziewać

1. Po włączeniu zasilania konwerter inicjalizuje panel i rysuje **ciemnoniebieski ekran z białym paskiem**. Ten widok potwierdza, że połączenie konwerter → panel działa.
2. Ok. 1–2 s po włączeniu radia bootloader wysyła **logo startowe**. Rysuje się ono **powoli, ok. 1,3 s**, bo radio tak je wysyła.
3. Po ok. 20–30 s startuje program radia i pojawia się **interfejs**.

Numan odświeża ekran ok. 60 razy na sekundę, Auna tylko przy zmianach na ekranie.

---

## 6. Diagnostyka przez USB

Podłącz płytkę do komputera i otwórz port szeregowy **115200**: Serial Monitor w Arduino IDE, CoolTerm albo w terminalu macOS:

```
ls /dev/cu.usbmodem*
screen /dev/cu.usbmodemXXXX 115200      (wyjście: Ctrl+A, K, Y)
```

Co sekundę pojawia się linia, np.:

```
ST7789 obrot180 | CPU 250 MHz | bajty 305198 komendy 441 obrazy 63 piksele(B) 304379 | zle 0 przepelnienia 0 | okno 232..239 x 0..319 | rej16=08
```

| Pole | Znaczenie | Prawidłowo |
|---|---|---|
| początek | typ panelu i aktywne zworki | zgodnie z ustawieniem |
| bajty | bajty odebrane z radia w ostatniej sekundzie | > 0, gdy radio rysuje |
| komendy | komendy (D/C = 0) | > 0, gdy radio rysuje |
| obrazy | liczba zapisów obrazu (rejestr `22`) | > 0, gdy radio rysuje |
| piksele(B) | bajty pikseli przekazane do panelu | ≈ bajty |
| **zle** | bajty z błędnym taktowaniem (łącznie od startu) | **0** |
| **przepelnienia** | przepełnienia bufora (łącznie) | **0** |
| okno | ostatnie okno ustawione przez radio | w zakresie 0..239 × 0..319 |
| rej16 | orientacja wysłana przez radio | zwykle `08` |

Statystyki są wysyłane tylko wtedy, gdy port jest otwarty na komputerze.

---

## 7. Rozwiązywanie problemów

| Objaw | Przyczyna / rozwiązanie |
|---|---|
| Brak niebieskiego ekranu startowego | Połączenie konwerter → panel (GP8–GP12, VCC, GND), zasilanie płytki, podświetlenie. Przy panelu ILI9341 zewrzyj GP6. |
| Niebieski ekran zostaje, „bajty 0” | Radio nic nie wysyła (wyłączone, brak modułu Venice, radio jeszcze startuje, Auna bez zmian na ekranie) albo nie dochodzą linie CS/SCK. Sprawdź połączenia GP2–GP5 i masę. |
| Szum, poszarpany obraz, „zle” > 0 | Zegar gubi impulsy: odczep za rezystorami albo za długie przewody. Przenieś odczep przed rezystory, skróć przewody, popraw masę. |
| Obraz do góry nogami | Zworka **GP7** do GND. |
| Obraz lustrzany (tylko lewo–prawo lub góra–dół) | Zmiana stałej `MADCTL_XOR_ST7789` / `MADCTL_XOR_ILI9341` w kodzie: bit `0x80` odbija w poziomie (napisy od prawej do lewej), bit `0x40` w pionie. |
| Negatyw (biały zamiast czarnego) | Zworka **GP13** do GND (panel IPS). |
| Zamienione kolory czerwony ↔ niebieski | Zmiana bitu `0x08` w `MADCTL_XOR_…` w kodzie. |
| Na ekranie zostaje tylko logo startowe, a w statystykach co kilkanaście sekund 64 komendy i ok. 153 KB pikseli | **Radio się restartuje** (problem radia, nie konwertera): uszkodzony moduł Venice albo firmware niepasujący do sprzętu. |
| Biały ekran po podłączeniu USB przy pracującym radiu | Płytka zrestartowała się przy zmianie zasilania. Panel dostał nową inicjalizację, ale radio nie wysłało jeszcze nowego obrazu. Zrestartuj radio. |

---

## 8. Dane techniczne (dla ciekawych)

- Odbiór: PIO RP2040, odczyt bitu na zboczu narastającym SCK, tylko przy CS = 0 (ruch przy CS = 1, np. odczyty pamięci flash modułu po tej samej magistrali, jest ignorowany). D/C odczytywane z pierwszym bitem każdego bajtu.
- Resynchronizacja: Numan trzyma CS stale aktywne, więc CS nie wyrównuje bajtów. Po każdej przerwie w transmisji dłuższej niż 0,3 ms (koniec paska obrazu) odbiornik zaczyna liczenie bitów od nowa. Dzięki temu konwerter można włączyć albo zrestartować przy pracującym radiu.
- Bufor wejściowy: 8192 bajty (DMA, pierścień 32 KB), ok. 13–27 ms zapasu.
- Wyjście: sprzętowe SPI1, 31,25 MHz, tryb 0. CS/DC panelu sterowane przez procesor.
- Zmierzone w Numanie One: zegar radia ok. 5 MHz (ok. 100 ns / 96 ns), ok. 300 KB/s, 0 błędów.
- Zegar procesora: 250 MHz.
- Sekwencje inicjalizacyjne HX8347 nagrane z radiów (do porównania):
    - Numan One: `18=99 19=01 1B=1E 1C=02 1A=02 24=3F 25=47 23=70 …`
    - Auna Connect 150: `18=88 19=01 1B=1E 1C=02 1A=01 24=38 25=5F 23=68 …`
    - reszta (E2–EB, 1F, gamma 40–5D, 36=00, 17=05, 16=08, okno 02–09, 22, 28=38/3C) identyczna.

---

## Licencja i zastrzeżenia

Projekt udostępniony na [licencji MIT](LICENSE).

To niezależny projekt hobbystyczny, niezwiązany z firmami Frontier Smart Technologies, Numan, Auna ani innymi producentami. Nazwy produktów służą tylko do opisu zgodności. Przeróbki radia wykonujesz na własne ryzyko.

Zgłoszenia z innych radiów (model, wersja firmware, panel, ustawienia zworek) mile widziane jako GitHub issues.
