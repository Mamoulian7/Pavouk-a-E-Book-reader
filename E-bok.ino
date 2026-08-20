// --- ESP32 E-Paper Reader ---
// Koncept pro Arduino IDE

// --- Nastavení Sítě a MQTT ---
const char* WIFI_SSID = "xxxxx";
const char* WIFI_PASS = "xxxxx";
const char* MQTT_USER = "xxxxx";
const char* MQTT_PASS = "xxxxxx";

#include <Arduino.h>
#include <GxEPD2_BW.h>   // Knihovna pro e-paper (přímá podpora Waveshare)
#include <U8g2_for_Adafruit_GFX.h> // Pro české fonty s diakritikou! (nutno doinstalovat v Arduino IDE)
#include <TJpg_Decoder.h> // Knihovna pro vykreslování JPEG obálek
#include <FS.h>
#include <SD.h>          // Pro SD kartu (knihy, záložky)
#include <WiFi.h>
#include <HTTPClient.h>

// 1. Stavový automat (State Machine)
enum ViewState { OFF, MENU, LIBRARY, READER, HA_API, DIALOG };
ViewState currentView = MENU;

// --- Připojení Hardware Modulů ---

// 1. Modul nabíječky: TP4056 (HW-373 V1.2.1)
// B+ / B- -> Přímo na Li-Ion / Li-Pol baterii (ochrana proti podbití/přebití)
// OUT+    -> Napájení pro ESP32 (VCC / 3.3V pin přes vhodný LDO regulátor, příp. 5V pin pokud má deska vlastní regulátor)
// OUT-    -> Společná zem (GND)

// 2. Modul MicroSD: Adafruit (připojeno na standardní VSPI piny)
// Nezapomeňte na napájení! VCC/VIN -> 5V (nebo 3.3V dle modulu), GND -> GND
// Využijeme hardwarové VSPI piny, které jsou na desce volně přístupné:
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   21  

// 3. Hardware Piny
// Hardwarové stránkovací tlačítka
#define BTN_LEFT 2  
#define BTN_RIGHT 12 

// Piny pro LED


// Piny Trackball modulu
#define PIN_BTN 32 
#define PIN_RHT 33
#define PIN_LFT 22 
#define PIN_DWN 4
#define PIN_UP  16 
#define PIN_BATTERY 35 

// RGBW LED na Trackballu (Modrou a bílou nezapojujeme)
#define PIN_RED 5  
#define PIN_GRN 17 
//#define PIN_BLU -1 
//#define PIN_WHT -1

// Poznámka k Trackballu (Blackberry modul):
// Tento breakout modul nespíná jako obyčejná tlačítka, ale generuje 
// pulzy. V obou osách (X, Y) z něj "padají" signály při pohybu.
// V Arduino IDE je na to ideální zavěsit tzv. přerušení (attachInterrupt)
// pro každý směr, nebo použít knihovny určené pro dekódování rotačních enkodérů.
// Pro přesun po řádcích (Y osa) přidáme konstantu "skoku" slov.

// --- Displej a Knihovny (GxEPD2) ---
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
// Konfigurace pro "Waveshare E-Paper ESP32 Driver Board"
// Tato deska má e-ink přímo připojený na specifické piny (HSPI):
// SCK=13, DIN(MOSI)=14, CS=15, DC=27, RST=26, BUSY=25
// 
// DŮLEŽITÉ: Níže musíte zvolit správnou třídu (class) podle vašeho přesného modelu displeje!
// Příklad: GxEPD2_750_T7 (pro 7.5" V2 800x480), GxEPD2_420 (pro 4.2"), GxEPD2_970 (pro 9.7") atd.
// Nesprávná třída způsobí rozsypaný obraz (artefakty nebo zrcadlení)!
GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(GxEPD2_750_T7(/*CS*/ 15, /*DC*/ 27, /*RST*/ 26, /*BUSY*/ 25));

U8G2_FOR_ADAFRUIT_GFX u8g2Fonts; // Instance pro vykreslování s diakritikou

// --- SD Karta a Sběrnice ---
#include <SPI.h>
#include <SD.h>
// Vytvoříme pro SD kartu samostatnou SPI instanci (VSPI), aby nekolidovala se sběrnicí displeje.
SPIClass sdSPI(VSPI);

// --- Wi-Fi, OTA a WebServer ---
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>

WebServer server(80);

// --- Globální proměnné stavu ---
enum AppState { STATE_MENU, STATE_LIBRARY, STATE_READING, STATE_BOOKMARKS, STATE_WIFI_SERVER, STATE_SYSTEM };
AppState currentState = STATE_MENU;

// --- Slovník ---
bool lookupMode = false;
int selectedWordIndex = 0;
String lookupResult = "";

String lookupWordWikipedia(String word); // Deklarace pro funkci dole

String lookupWord(String word) {
    word.replace(".", ""); word.replace(",", ""); word.replace("?", ""); word.replace("!", ""); word.replace("\"", "");
    word.trim();
    if (word.length() == 0) return "";
    
    // Rychlé připojení k naposledy použité Wi-Fi síti (neblokující)
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(); // Použije uložené přihlašovací údaje
        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 40) { // Max 4 vteřiny
            delay(100);
            retries++;
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        String wiki = lookupWordWikipedia(word);
        if (wiki != "Definice nenalezena.") return "Wiki: " + wiki;
    }
    
    return "Nenalezeno (Zkontrolujte Wi-Fi)";
}

// Proměnné pro knihovnu
String fileList[10];
int fileCount = 0;
int selectedFileIndex = 0;

// Struktura pro záložky a historii rozečtených knih
struct Bookmark {
    String filename;
    uint32_t position;
    int percent;
};
Bookmark recentBookmarks[5];
int recentBookmarkCount = 0;
int selectedBookmarkIndex = 0;

// Proměnné pro řízení LED diod, podsvícení a šetření energie
bool ledIsOn = true;
unsigned long ledOnTime = 0;



String menuItems[] = {
  "1. Pokračovat ve čtení",
  "2. Knihovna",
  "3. Wi-Fi a Web server",
  "4. Nastavení"
};
const int menuCount = 4;
int selectedMenuIndex = 0;
bool fullUpdateNeeded = true; 

bool displayNeedsUpdate = true; // Změněno na true! Musí se vykreslit první text po zapnutí.
unsigned long lastActivity = 0;
String currentText = "Zapínám čtečku...";

int activeFontType = 0; // 0 = Bezpatkové (Helvetica), 1 = Patkové (Times), 2 = Monospace (Courier)
int activeFontSize = 12; // 12 nebo 14 pixelů

// Pomocná funkce, která vrátí správný ukazatel na font podle nastavení
const uint8_t* getActiveFont() {
    if (activeFontType == 0) {
        return (activeFontSize == 12) ? u8g2_font_helvR12_te : u8g2_font_helvR14_te;
    } else if (activeFontType == 1) {
        return (activeFontSize == 12) ? u8g2_font_timR12_tr : u8g2_font_timR14_tr;
    } else {
        return (activeFontSize == 12) ? u8g2_font_courR12_tr : u8g2_font_courR14_tr;
    }
}

// --- Proměnné pro čtení knihy ---
String currentFilename = "";
uint32_t currentFilePos = 0;
uint32_t nextFilePos = 0;
uint32_t pageHistory[100]; 
int pageHistoryCount = 0;
String currentPageText = "";

// --- Funkce pro čtení baterie ---
float getBatteryVoltage() {
    // Pro vysokoodporový dělič je lepší provést více měření
    uint32_t mvTotal = 0;
    const int samples = 10;
    for (int i = 0; i < samples; i++) {
        // analogReadMilliVolts používá tovární kalibraci ESP32!
        mvTotal += analogReadMilliVolts(PIN_BATTERY);
        delay(2);
    }
    float mv = (float)mvTotal / samples;
    
    // Měříme polovinu napětí (dělič 1:2), takže reálné napětí je 2x vyšší
    float voltage = (mv / 1000.0) * 2.0;
    
    // Pokud je napětí úplný nesmysl (např. dělič chybí nebo je ADC vypnuté), 
    // zabráníme bláznivým záporným číslům
    if (voltage < 2.0) voltage = 0.0;
    
    return voltage;
}

int getBatteryPercentage() {
    float voltage = getBatteryVoltage();
    if (voltage < 2.0) return 0;
    
    // LiPo baterie: 4.2V = 100%, 3.2V = 0%
    int percent = (int)((voltage - 3.2) / (4.2 - 3.2) * 100.0);
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    return percent;
}

// --- 1. Jádro - Inicializace Displeje a SD karty ---

void initDisplay() {
    // Pro Waveshare ESP32 Driver Board inicializujeme výchozí hardware SPI pro e-ink.
    // Dle schématu desky jsou piny: SCK=13, DIN(MOSI)=14, CS=15.
    // MISO pro displej nepotřebujeme (zadáme -1, knihovna si s tím poradí).
    SPI.begin(13, -1, 14, 15);
    
    display.init(115200);   // Spustí komunikaci s displejem a loguje do konzole
    display.setRotation(1); // 1 = Otočení na šířku (landscape)
    
    // Inicializace JPEG dekodéru pro obálky
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tjpg_output);
    
    u8g2Fonts.begin(display);
    u8g2Fonts.setFontMode(1);                 // Průhledné pozadí
    u8g2Fonts.setFontDirection(0);            // Směr textu (zleva doprava)
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    // '_te' znamená plná česká UTF-8 tabulka!
    u8g2Fonts.setFont(u8g2_font_helvR12_te); 
    
    display.fillScreen(GxEPD_WHITE);
}

void updateEpaperDisplay() {
    // Zvolíme typ překreslení: 
    // fullUpdateNeeded = celé vyčištění displeje (blikne do černa)
    // false = částečný update (jen překreslí změněné pixely, mnohem rychlejší!)
    if (fullUpdateNeeded) {
        display.setFullWindow();
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        if (currentState == STATE_MENU) {
            u8g2Fonts.setFont(u8g2_font_helvB18_te); // Větší tučné pro nadpis menu
            u8g2Fonts.setCursor(20, 40);
            u8g2Fonts.print("=== HLAVNÍ MENU ===");
            
            u8g2Fonts.setFont(u8g2_font_helvR18_te); // Větší písmo pro položky
            for (int i = 0; i < menuCount; i++) {
                int yPos = 90 + (i * 35); // Zmenšené rozestupy
                u8g2Fonts.setCursor(30, yPos);
                
                if (i == selectedMenuIndex) {
                    u8g2Fonts.print("-> ");
                    u8g2Fonts.print(menuItems[i]);
                    u8g2Fonts.print(" <-");
                } else {
                    u8g2Fonts.print("   ");
                    u8g2Fonts.print(menuItems[i]);
                }
            }

            // Vykreslení systémových informací ve spodní části
            u8g2Fonts.setFont(u8g2_font_helvR12_te);
            float batVolts = getBatteryVoltage();
            int batPercent = getBatteryPercentage();
            
            // Linka oddělující menu a info
            display.drawLine(20, 225, display.width() - 20, 225, GxEPD_BLACK);
            
            int col1 = 30;
            int col2 = 200;
            
            u8g2Fonts.setCursor(col1, 245);
            u8g2Fonts.print(("Baterie: " + String(batPercent) + "% (" + String(batVolts, 1) + "V)").c_str());
            u8g2Fonts.setCursor(col1, 265);
            u8g2Fonts.print("Firmware: v2.1 (OTA)");
            uint32_t freeRAM = ESP.getFreeHeap() / 1024;
            u8g2Fonts.setCursor(col1, 285);
            u8g2Fonts.print(("RAM volná: " + String(freeRAM) + " KB").c_str());
            
            u8g2Fonts.setCursor(col2, 245);
            u8g2Fonts.print("CPU: ESP32-WROOM");
            uint32_t freeSD_MB = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
            u8g2Fonts.setCursor(col2, 265);
            u8g2Fonts.print(("SD volno: " + String(freeSD_MB) + " MB").c_str());
            
            u8g2Fonts.setCursor(20, display.height() - 10);
            u8g2Fonts.print("[ Levé tl. (dlouze) = Uspat čtečku ]");
        } else if (currentState == STATE_BOOKMARKS) {
            u8g2Fonts.setFont(u8g2_font_helvB18_te); // Větší tučné pro nadpis
            u8g2Fonts.setCursor(20, 40);
            u8g2Fonts.print("=== ROZEČTENÉ KNIHY ===");
            
            u8g2Fonts.setFont(u8g2_font_helvR14_te); // Střední písmo pro položky
            if (recentBookmarkCount == 0) {
                u8g2Fonts.setCursor(40, 120);
                u8g2Fonts.print("Žádné záložky v historii.");
                u8g2Fonts.setCursor(40, 150);
                u8g2Fonts.print("Otevřete knihu z Knihovny.");
            } else {
                for (int i = 0; i < recentBookmarkCount; i++) {
                    int yPos = 90 + (i * 38);
                    u8g2Fonts.setCursor(40, yPos);
                    
                    String displayStr = recentBookmarks[i].filename;
                    if (displayStr.endsWith(".txt") || displayStr.endsWith(".TXT")) {
                        displayStr = displayStr.substring(0, displayStr.length() - 4);
                    }
                    
                    if (i == selectedBookmarkIndex) {
                        u8g2Fonts.print("-> " + displayStr + " (" + String(recentBookmarks[i].percent) + "%) <-");
                    } else {
                        u8g2Fonts.print("   " + displayStr + " (" + String(recentBookmarks[i].percent) + "%)");
                    }
                }
            }
            u8g2Fonts.setFont(u8g2_font_helvR12_te); // Menší pro nápovědu dole
            u8g2Fonts.setCursor(20, display.height() - 20);
            u8g2Fonts.print("[ Pravé tl. (dlouze) = Zpět | Levé tl. (dlouze) = Uspat ]");
        } else if (currentState == STATE_LIBRARY) {
            u8g2Fonts.setFont(u8g2_font_helvB18_te); // Větší tučné pro nadpis knihovny
            u8g2Fonts.setCursor(20, 40);
            u8g2Fonts.print("=== KNIHOVNA ===");
            
            u8g2Fonts.setFont(u8g2_font_helvR14_te); // Střední písmo pro soubory
            for (int i = 0; i < fileCount; i++) {
                int yPos = 90 + (i * 35);
                u8g2Fonts.setCursor(40, yPos);
                if (i == selectedFileIndex) {
                    u8g2Fonts.print("-> "); u8g2Fonts.print(fileList[i]); u8g2Fonts.print(" <-");
                } else {
                    u8g2Fonts.print("   "); u8g2Fonts.print(fileList[i]);
                }
            }
            u8g2Fonts.setFont(u8g2_font_helvR12_te); // Menší pro nápovědu dole
            u8g2Fonts.setCursor(20, display.height() - 20);
            u8g2Fonts.print("[ Pravé tl. (dlouze) = Zpět | Levé tl. (dlouze) = Uspat ]");
        } else if (currentState == STATE_SYSTEM) {
            u8g2Fonts.setFont(u8g2_font_helvB18_te); // Větší tučné pro nadpis
            u8g2Fonts.setCursor(20, 40);
            u8g2Fonts.print("=== NASTAVENÍ ===");
            
            u8g2Fonts.setFont(u8g2_font_helvR14_te);
            u8g2Fonts.setCursor(40, 90);
            
            String fontName = "Neznamy";
            if (activeFontType == 0) fontName = "Helvetica (Bezpatkove)";
            else if (activeFontType == 1) fontName = "Times (Patkove)";
            else if (activeFontType == 2) fontName = "Courier (Monospace)";
            u8g2Fonts.print(("-> Pismo: " + fontName).c_str());

            u8g2Fonts.setCursor(40, 130);
            u8g2Fonts.print(("-> Velikost: " + String(activeFontSize) + " px").c_str());

            u8g2Fonts.setCursor(40, 180);
            u8g2Fonts.print("Nahled:");
            
            u8g2Fonts.setFont(getActiveFont());
            u8g2Fonts.setCursor(40, 210);
            u8g2Fonts.print("Nepropadejte panice!");
            
            u8g2Fonts.setFont(u8g2_font_helvR12_te); // Menší pro nápovědu dole
            u8g2Fonts.setCursor(20, display.height() - 40);
            u8g2Fonts.print("[ Klik = Změň písmo | L/P trackball = Změň velikost ]");
            u8g2Fonts.setCursor(20, display.height() - 20);
            u8g2Fonts.print("[ Pravé tl. (dlouze) = Zpět | Levé tl. (dlouze) = Uspat ]");
        } else if (currentState == STATE_READING) {
            u8g2Fonts.setFont(u8g2_font_helvR12_te); 
            
            if (!lookupMode) {
                u8g2Fonts.setCursor(4, 15);
                u8g2Fonts.print(currentPageText);
            } else {
                int x = 4;
                int y = 15;
                int lineHeight = u8g2Fonts.getFontAscent() - u8g2Fonts.getFontDescent() + 4;
                
                int wordIdx = 0;
                String currentWord = "";
                for (int i = 0; i <= currentPageText.length(); i++) {
                    char c = (i < currentPageText.length()) ? currentPageText[i] : ' ';
                    
                    if (c == ' ' || c == '\n' || i == currentPageText.length()) {
                        if (currentWord.length() > 0) {
                            if (wordIdx == selectedWordIndex) {
                                int wordW = u8g2Fonts.getUTF8Width(currentWord.c_str());
                                display.fillRect(x - 2, y - u8g2Fonts.getFontAscent() - 2, wordW + 4, lineHeight, GxEPD_BLACK);
                                u8g2Fonts.setForegroundColor(GxEPD_WHITE);
                                u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
                            } else {
                                u8g2Fonts.setForegroundColor(GxEPD_BLACK);
                                u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
                            }
                            
                            u8g2Fonts.setCursor(x, y);
                            u8g2Fonts.print(currentWord);
                            
                            u8g2Fonts.setForegroundColor(GxEPD_BLACK);
                            u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
                            
                            x += u8g2Fonts.getUTF8Width(currentWord.c_str()) + u8g2Fonts.getUTF8Width(" ");
                            wordIdx++;
                            currentWord = "";
                        }
                        if (c == '\n') {
                            x = 4;
                            y += lineHeight;
                        }
                    } else {
                        currentWord += c;
                    }
                }
                
                if (lookupResult != "") {
                    // Vykresleni velkeho okna s vysledkem z Wikipedie
                    int boxW = display.width() - 40;
                    int boxH = 140;
                    int boxX = 20;
                    int boxY = (display.height() - boxH) / 2;
                    
                    display.fillRect(boxX, boxY, boxW, boxH, GxEPD_WHITE);
                    display.drawRect(boxX, boxY, boxW, boxH, GxEPD_BLACK);
                    display.drawRect(boxX+1, boxY+1, boxW-2, boxH-2, GxEPD_BLACK);
                    
                    display.drawLine(boxX, boxY + 25, boxX + boxW, boxY + 25, GxEPD_BLACK);
                    u8g2Fonts.setFont(u8g2_font_helvB12_te);
                    u8g2Fonts.setCursor(boxX + 10, boxY + 18);
                    u8g2Fonts.print("Slovník (Wikipedia)");
                    
                    u8g2Fonts.setFont(u8g2_font_helvR12_te);
                    String temp = "";
                    int textY = boxY + 45;
                    int textMaxWidth = boxW - 20;
                    
                    for (int i = 0; i <= lookupResult.length(); i++) {
                        char c = (i < lookupResult.length()) ? lookupResult[i] : ' ';
                        if (c == ' ' || i == lookupResult.length()) {
                            if (u8g2Fonts.getUTF8Width(temp.c_str()) > textMaxWidth) {
                                int lastSpace = temp.lastIndexOf(' ');
                                if (lastSpace > 0) {
                                    String line = temp.substring(0, lastSpace);
                                    u8g2Fonts.setCursor(boxX + 10, textY);
                                    u8g2Fonts.print(line);
                                    textY += 20;
                                    temp = temp.substring(lastSpace + 1);
                                }
                            }
                        }
                        if (i < lookupResult.length()) temp += c;
                    }
                    if (temp.length() > 0) {
                        u8g2Fonts.setCursor(boxX + 10, textY);
                        u8g2Fonts.print(temp);
                    }
                    
                    u8g2Fonts.setFont(u8g2_font_helvR10_te);
                    u8g2Fonts.setCursor(boxX + 10, boxY + boxH - 10);
                    u8g2Fonts.print("[Stisk pro zavřeni]");
                    
                } else {
                    display.fillRect(0, display.height() - 40, display.width(), 40, GxEPD_WHITE);
                    display.drawLine(0, display.height() - 40, display.width(), display.height() - 40, GxEPD_BLACK);
                    u8g2Fonts.setCursor(4, display.height() - 15);
                    u8g2Fonts.print("[Stisk Trackballu = Přeložit | Zpět = Zrušit]");
                }
            }
        }
        
    } while (display.nextPage());
}

void initSDCard() {
    // Inicializace samostatné SPI sběrnice pro SD kartu (standardní VSPI piny)
    // Tip: CS pin nastavujeme na -1, aby se hardware SPI nepokoušel pin sám řídit,
    // to necháme výhradně na knihovně SD.h. To řeší spoustu konfliktů na ESP32!
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
    
    // Ošetření stability: U spojení kablíky může být výchozí frekvence příliš vysoká.
    // Zkusíme inicializaci s max. rychlostí 4 MHz (nebo i 1 MHz, např. 1000000, pokud to bude stále zlobit)
    if (!SD.begin(SD_CS, sdSPI, 4000000)) {
        Serial.println("Chyba: SD karta nenalezena, nebo ji nelze prečíst!");
        if (PIN_RED != -1) digitalWrite(PIN_RED, HIGH); // Červená LED při chybě
        currentPageText = "Chyba SD karty! Zkontrolujte:\n1. Zda je formát FAT32\n2. Zda jsou kablíky SCK=18, MISO=19, MOSI=23\n3. Zda je napajení 5V (dle modulu).";
        currentState = STATE_READING;
        displayNeedsUpdate = true;
        return;
    }
    Serial.println("SD karta připojena (samostatná sběrnice sdSPI).");
}

void loadBookmarks() {
    // Příklad formátu: "1984.txt=1500" (kde 1500 je aktuální bajt v textu knihy)
    if (!SD.exists("/bookmarks.txt")) {
        Serial.println("Záložky neexistují. Čistá knihovna.");
        return;
    }
    File file = SD.open("/bookmarks.txt", FILE_READ);
    if (!file) {
        Serial.println("Nepovedlo se otevrit bookmarks.txt");
        return;
    }
    Serial.println("Nacteny rozectene knihy:");
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        Serial.println(" -> " + line); 
    }
    file.close();
}

void loadLibrary() {
    fileCount = 0;
    File root = SD.open("/");
    if (!root) {
        fileList[0] = "Chyba čtení SD!";
        fileCount = 1;
        return;
    }
    File file = root.openNextFile();
    while (file && fileCount < 10) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (!name.startsWith(".") && (name.endsWith(".txt") || name.endsWith(".TXT"))) { // Pouze soubory .txt
                fileList[fileCount] = name;
                fileCount++;
            }
        }
        file = root.openNextFile();
    }
    if (fileCount == 0) {
        fileList[0] = "Žádné knihy (.txt) na SD.";
        fileCount = 1;
    }
}

void preparePageText() {
    u8g2Fonts.setFont(getActiveFont()); // Ujistit se, že počítáme s fontem pro čtení
    File f = SD.open("/" + currentFilename);
    if (!f) {
        currentPageText = "Nelze otevřít soubor.";
        return;
    }
    
    f.seek(currentFilePos);
    currentPageText = "";
    
    int x = 0;
    int y = 0;
    // Těsnější řádkování bez "+ 2" pro usnadnění čtení a úsporu místa
    int lineHeight = u8g2Fonts.getFontAscent() - u8g2Fonts.getFontDescent(); 
    int spaceWidth = u8g2Fonts.getUTF8Width(" ");
    
    int maxWidth = display.width() - 8;  // Odsazení od okrajů
    // Zvětšíme využitelnou výšku displeje (začínáme na y=15, takže zbyde více místa dole)
    int maxHeight = display.height() - 15;
    
    String currentWord = "";
    bool pageFull = false;
    uint32_t bytesRead = 0;
    
    while (f.available() && !pageFull) {
        char c = (char)f.read();
        bytesRead++;
        
        if (c == '\r') continue;
        
        if (c == ' ' || c == '\n') {
            int wordWidth = u8g2Fonts.getUTF8Width(currentWord.c_str());
            
            if (x + wordWidth > maxWidth) { 
                currentPageText += '\n';
                x = 0;
                y += lineHeight;
            }
            
            if (y + lineHeight > maxHeight) {
                pageFull = true;
                // Vrátíme se na začátek slova (snížíme další start pozici)
                nextFilePos = currentFilePos + bytesRead - currentWord.length() - 1;
                break;
            }
            
            currentPageText += currentWord;
            x += wordWidth;
            
            if (c == '\n') {
                currentPageText += '\n';
                x = 0;
                y += lineHeight;
            } else {
                currentPageText += ' ';
                x += spaceWidth;
            }
            
            currentWord = "";
            
            if (y + lineHeight > maxHeight) {
                 pageFull = true;
                 nextFilePos = currentFilePos + bytesRead;
                 break;
            }
        } else {
            currentWord += c;
        }
    }
    
    if (!pageFull) {
        int wordWidth = u8g2Fonts.getUTF8Width(currentWord.c_str());
        if (x + wordWidth > maxWidth) {
            currentPageText += '\n';
        }
        currentPageText += currentWord;
        nextFilePos = currentFilePos + bytesRead;
    }
    
    f.close();
}

void pushHistory(uint32_t pos) {
    if (pageHistoryCount < 100) {
        pageHistory[pageHistoryCount++] = pos;
    } else {
        for (int i = 0; i < 99; i++) pageHistory[i] = pageHistory[i+1];
        pageHistory[99] = pos;
    }
}

void openFile(String filename, uint32_t startPos = 0) {
    if (filename == "Chyba čtení SD!" || filename == "Žádné soubory na SD.") return;
    
    currentFilename = filename;
    currentFilePos = startPos;
    nextFilePos = 0;
    pageHistoryCount = 0;
    
    preparePageText();
}

void saveBookmark(String filename, uint32_t pos) {
    if (filename == "" || filename.endsWith(".pos") || filename.startsWith(".")) return;
    
    // Nejprve si přečteme stávající záložky, abychom je mohli posunout v historii
    String files[5];
    uint32_t positions[5];
    int count = 0;
    
    File fRead = SD.open("/.bookmarks");
    if (fRead) {
        while (fRead.available() && count < 5) {
            String fname = fRead.readStringUntil('\n');
            fname.trim();
            String posStr = fRead.readStringUntil('\n');
            posStr.trim();
            if (fname != "" && posStr != "") {
                files[count] = fname;
                positions[count] = posStr.toInt();
                count++;
            }
        }
        fRead.close();
    }
    
    // Zapíšeme aktualizovaný seznam (aktuálně čtená kniha jde na první místo)
    File fWrite = SD.open("/.bookmarks", FILE_WRITE);
    if (fWrite) {
        fWrite.println(filename);
        fWrite.println(pos);
        int written = 1;
        for (int i = 0; i < count; i++) {
            if (files[i] != filename && written < 5) {
                fWrite.println(files[i]);
                fWrite.println(positions[i]);
                written++;
            }
        }
        fWrite.close();
    }
    
    // Zpětná kompatibilita s jedním starším .bookmark souborem
    File fLegacy = SD.open("/.bookmark", FILE_WRITE);
    if (fLegacy) {
        fLegacy.println(filename);
        fLegacy.println(pos);
        fLegacy.close();
    }
    Serial.println("Záložka uložena: " + filename + " (pozice " + String(pos) + ")");
}

void saveBookmark() {
    saveBookmark(currentFilename, currentFilePos);
}

void loadBookmarksList() {
    recentBookmarkCount = 0;
    selectedBookmarkIndex = 0;
    
    File f = SD.open("/.bookmarks");
    if (f) {
        while (f.available() && recentBookmarkCount < 5) {
            String fname = f.readStringUntil('\n');
            fname.trim();
            String posStr = f.readStringUntil('\n');
            posStr.trim();
            
            if (fname != "" && posStr != "") {
                recentBookmarks[recentBookmarkCount].filename = fname;
                recentBookmarks[recentBookmarkCount].position = posStr.toInt();
                
                // Spočítáme procento rozečtení na základě velikosti souboru
                int percent = 0;
                File bookFile = SD.open("/" + fname);
                if (bookFile) {
                    uint32_t size = bookFile.size();
                    if (size > 0) {
                        percent = (recentBookmarks[recentBookmarkCount].position * 100) / size;
                    }
                    bookFile.close();
                }
                recentBookmarks[recentBookmarkCount].percent = percent;
                recentBookmarkCount++;
            }
        }
        f.close();
    }
    
    // Pokud seznam neexistuje, zkusíme načíst z legacy .bookmark souboru
    if (recentBookmarkCount == 0) {
        File fLegacy = SD.open("/.bookmark");
        if (fLegacy) {
            String fname = fLegacy.readStringUntil('\n');
            fname.trim();
            String posStr = fLegacy.readStringUntil('\n');
            posStr.trim();
            fLegacy.close();
            
            if (fname != "" && posStr != "") {
                recentBookmarks[0].filename = fname;
                recentBookmarks[0].position = posStr.toInt();
                
                int percent = 0;
                File bookFile = SD.open("/" + fname);
                if (bookFile) {
                    uint32_t size = bookFile.size();
                    if (size > 0) {
                        percent = (recentBookmarks[0].position * 100) / size;
                    }
                    bookFile.close();
                }
                recentBookmarks[0].percent = percent;
                recentBookmarkCount = 1;
            }
        }
    }
}

void loadBookmark() {
    loadBookmarksList();
    if (recentBookmarkCount > 0) {
        currentFilename = recentBookmarks[0].filename;
        currentFilePos = recentBookmarks[0].position;
        nextFilePos = 0;
        pageHistoryCount = 0;
        preparePageText();
        currentState = STATE_READING;
        displayNeedsUpdate = true;
        fullUpdateNeeded = true;
        Serial.println("Načtena poslední aktivní kniha: " + currentFilename);
        return;
    }
}

// --- Wi-Fi a Web server Setup ---
void setupWiFi() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setFont(u8g2_font_helvB18_te);
        u8g2Fonts.setCursor(20, 60);
        u8g2Fonts.print("Připojování k Wi-Fi...");
        u8g2Fonts.setFont(u8g2_font_helvR12_te);
        u8g2Fonts.setCursor(20, 100);
        u8g2Fonts.print(WIFI_SSID);
    } while (display.nextPage());

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setFont(u8g2_font_helvB18_te);
        u8g2Fonts.setCursor(20, 60);
        if (WiFi.status() == WL_CONNECTED) {
            u8g2Fonts.print("Wi-Fi Připojeno!");
            u8g2Fonts.setFont(u8g2_font_helvR12_te);
            u8g2Fonts.setCursor(20, 100);
            u8g2Fonts.print("IP adresa: ");
            u8g2Fonts.print(WiFi.localIP().toString());
            u8g2Fonts.setCursor(20, 130);
            u8g2Fonts.print("Otevři tuto adresu v prohlížeči na PC.");
            u8g2Fonts.setCursor(20, 160);
            u8g2Fonts.print("Pro nahrání souborů přes web server.");
            u8g2Fonts.setCursor(20, 190);
            u8g2Fonts.print("OTA aktualizace jsou aktivní.");
        } else {
            u8g2Fonts.print("Chyba připojení k Wi-Fi.");
            u8g2Fonts.setFont(u8g2_font_helvR12_te);
            u8g2Fonts.setCursor(20, 100);
            u8g2Fonts.print("Zkontroluj SSID a heslo v kódu.");
        }
        
        u8g2Fonts.setCursor(20, display.height() - 20);
        u8g2Fonts.print("[ Pravé tl. (dlouze) = Zpět | Levé tl. (dlouze) = Uspat ]");
    } while (display.nextPage());

    if (WiFi.status() == WL_CONNECTED) {
        // Start OTA
        ArduinoOTA.onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH) {
                type = "sketch";
            } else { // U_SPIFFS
                type = "filesystem";
            }
            Serial.println("Start updating " + type);
        });
        ArduinoOTA.onEnd([]() {
            Serial.println("\nEnd");
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        });
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) {
                Serial.println("Auth Failed");
            } else if (error == OTA_BEGIN_ERROR) {
                Serial.println("Begin Failed");
            } else if (error == OTA_CONNECT_ERROR) {
                Serial.println("Connect Failed");
            } else if (error == OTA_RECEIVE_ERROR) {
                Serial.println("Receive Failed");
            } else if (error == OTA_END_ERROR) {
                Serial.println("End Failed");
            }
        });
        ArduinoOTA.begin();

        // Setup WebServer
        server.on("/", HTTP_GET, []() {
            server.sendHeader("Connection", "close");
            String html = "<html><head><meta charset='utf-8'><title>ESP32 E-Reader Správce</title>";
            html += "<style>body{font-family:sans-serif;max-width:800px;margin:2rem auto;padding:1rem;background:#f4f4f5;color:#333;} .box{background:#fff;padding:2rem;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);} th, td{text-align:left;padding:12px;border-bottom:1px solid #ddd;} table{width:100%;border-collapse:collapse;} .btn{padding:8px 16px;background:#3b82f6;color:white;text-decoration:none;border-radius:4px;border:none;cursor:pointer;font-weight:bold;} .btn-del{background:#ef4444;font-size:12px;padding:6px 12px;}</style></head><body>";
            html += "<div class='box'><h2>Správce souborů (SD Karta)</h2>";
            html += "<form method='POST' action='/upload' enctype='multipart/form-data' style='margin-bottom:20px;padding:15px;background:#f8fafc;border:1px solid #e2e8f0;border-radius:6px;'><input type='file' name='upload' required style='margin-right:10px;'><input type='submit' value='Nahrát na SD' class='btn'></form>";
            html += "<table><tr><th>Název souboru</th><th>Velikost (B)</th><th>Akce</th></tr>";

            File root = SD.open("/");
            if(!root){
                html += "<tr><td colspan='3'>Chyba čtení SD karty (nebo prázdná karta).</td></tr>";
            } else {
                File file = root.openNextFile();
                bool hasFiles = false;
                while(file){
                    if(!file.isDirectory()){
                        hasFiles = true;
                        html += "<tr><td>" + String(file.name()) + "</td><td>" + String(file.size()) + "</td>";
                        html += "<td><a href='/delete?f=" + String(file.name()) + "' class='btn btn-del' onclick=\"return confirm('Opravdu smazat " + String(file.name()) + "?');\">Smazat</a></td></tr>";
                    }
                    file = root.openNextFile();
                }
                if (!hasFiles) {
                     html += "<tr><td colspan='3'>Žádné soubory nenalezeny.</td></tr>";
                }
            }
            html += "</table></div></body></html>";
            server.send(200, "text/html", html);
        });

        server.on("/delete", HTTP_GET, []() {
            if(server.hasArg("f")){
                String filename = "/" + server.arg("f");
                if(SD.exists(filename)){
                    SD.remove(filename);
                }
            }
            server.sendHeader("Location", "/");
            server.send(303);
        });
        
        server.on("/upload", HTTP_POST, []() {
            server.sendHeader("Connection", "close");
            server.send(200, "text/html", "<html><head><meta charset='utf-8'><title>Upload OK</title></head><body><h2>Soubor nahran!</h2><a href='/'>Zpet</a></body></html>");
        }, []() {
            HTTPUpload& upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                Serial.printf("Upload Name: %s\n", upload.filename.c_str());
                String filePath = "/" + upload.filename;
                if(SD.exists(filePath)) {
                    SD.remove(filePath);
                }
                File uploadFile = SD.open(filePath, FILE_WRITE);
                if (!uploadFile) {
                    Serial.println("Nepodařilo se otevřít soubor pro zápis na SD!");
                } else {
                    uploadFile.close();
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                String filePath = "/" + upload.filename;
                File uploadFile = SD.open(filePath, FILE_APPEND);
                if (uploadFile) {
                    uploadFile.write(upload.buf, upload.currentSize);
                    uploadFile.close();
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                Serial.printf("Upload Size: %u\n", upload.totalSize);
            }
        });

        server.begin();
    }
    
    currentState = STATE_WIFI_SERVER;
    lastActivity = millis();
}

// --- Řízení vstupů, podsvícení a spánku ---
void resetActivityTimer() {
    lastActivity = millis();

    
    if (!ledIsOn) {
        digitalWrite(PIN_GRN, HIGH);
        ledIsOn = true;
    }
    ledOnTime = millis();
}

void checkHardwareButtons() {
    static int lastLeftBtn = HIGH;
    static int lastRightBtn = HIGH;
    static unsigned long leftPressStart = 0;
    static unsigned long rightPressStart = 0;
    static bool leftIsPressed = false;
    static bool rightIsPressed = false;
    static bool leftLongPressed = false;
    static bool rightLongPressed = false;

    const unsigned long LONG_PRESS_TIME = 1000; // 1 sekunda pro dlouhý stisk

    int currentLeft = digitalRead(BTN_LEFT);
    int currentRight = digitalRead(BTN_RIGHT);

    // --- LEVÉ TLAČÍTKO ---
    if (currentLeft == LOW) {
        if (!leftIsPressed) {
            leftIsPressed = true;
            leftPressStart = millis();
            leftLongPressed = false;
            resetActivityTimer();
        } else if (!leftLongPressed && (millis() - leftPressStart >= LONG_PRESS_TIME)) {
            leftLongPressed = true;
            // AKCE: Dlouhý stisk levého -> vypnutí / zapnutí (Deep Sleep)
            Serial.println("Dlouhý stisk levého tlačítka -> jdu spát.");
            goToDeepSleep();
        }
    } else {
        if (leftIsPressed) {
            leftIsPressed = false;
            if (!leftLongPressed) {
                // Krátký stisk: Strana zpět
                resetActivityTimer();
                if (currentState == STATE_READING) {
                    if (pageHistoryCount > 0) {
                        pageHistoryCount--;
                        currentFilePos = pageHistory[pageHistoryCount];
                        preparePageText();
                        displayNeedsUpdate = true;
                        fullUpdateNeeded = true;
                    }
                }
            }
        }
    }

    // --- PRAVÉ TLAČÍTKO ---
    if (currentRight == LOW) {
        if (!rightIsPressed) {
            rightIsPressed = true;
            rightPressStart = millis();
            rightLongPressed = false;
            resetActivityTimer();
        } else if (!rightLongPressed && (millis() - rightPressStart >= LONG_PRESS_TIME)) {
            rightLongPressed = true;
            // AKCE: Dlouhý stisk pravého -> Zpět (Level up)
            Serial.println("Dlouhý stisk pravého tlačítka -> Zpět.");
            resetActivityTimer();
            if (currentState == STATE_READING) {
                if (lookupMode) {
                    lookupMode = false;
                    displayNeedsUpdate = true;
                    fullUpdateNeeded = false;
                } else {
                    saveBookmark();
                    loadBookmarksList();
                    currentState = STATE_BOOKMARKS;
                    displayNeedsUpdate = true;
                    fullUpdateNeeded = true;
                }
            } else if (currentState == STATE_BOOKMARKS || currentState == STATE_LIBRARY || currentState == STATE_WIFI_SERVER || currentState == STATE_SYSTEM) {
                if (currentState == STATE_WIFI_SERVER) {
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                }
                currentState = STATE_MENU;
                displayNeedsUpdate = true;
                fullUpdateNeeded = true;
            }
        }
    } else {
        if (rightIsPressed) {
            rightIsPressed = false;
            if (!rightLongPressed) {
                // Krátký stisk: Strana vpřed
                resetActivityTimer();
                if (currentState == STATE_READING) {
                    if (nextFilePos > currentFilePos) {
                        pushHistory(currentFilePos);
                        currentFilePos = nextFilePos;
                        preparePageText();
                        displayNeedsUpdate = true;
                        fullUpdateNeeded = true;
                    }
                }
            }
        }
    }
}

void checkTrackball() {
    // Trackball nefunguje jako obyčejná tlačítko, ale jako ROTAČNÍ ENKODÉR!
    // Když se kulička zastaví, senzor může zůstat trvale ve stavu LOW nebo HIGH.
    // Musíme tedy reagovat POUZE NA ZMĚNU STAVU (hranu), jinak menu roluje samo.
    static int lastUp = HIGH;
    static int lastDown = HIGH;
    static int lastBtn = HIGH;
    static int lastLeft = HIGH;
    static int lastRight = HIGH;

    // Akumulátory pro plynulejší skrolování trackballem (1, 2 nebo 3 pozice podle délky pohybu)
    static int upPulses = 0;
    static int downPulses = 0;
    static unsigned long firstPulseTime = 0;

    int currentUp = digitalRead(PIN_UP);
    int currentDown = digitalRead(PIN_DWN);
    int currentBtn = digitalRead(PIN_BTN);
    int currentLeft = digitalRead(PIN_LFT);
    int currentRight = digitalRead(PIN_RHT);

    // Detekce sestupné hrany (z HIGH na LOW)
    bool upRaw = (currentUp == LOW && lastUp == HIGH);
    bool downRaw = (currentDown == LOW && lastDown == HIGH);
    bool btn = (currentBtn == LOW && lastBtn == HIGH);
    bool left = (currentLeft == LOW && lastLeft == HIGH);
    bool right = (currentRight == LOW && lastRight == HIGH);

    lastUp = currentUp;
    lastDown = currentDown;
    lastBtn = currentBtn;
    lastLeft = currentLeft;
    lastRight = currentRight;

    if (upRaw || downRaw || left || right || btn) {
        resetActivityTimer(); // Reset časovače pro usínání a rozsvícení podsvícení
    }

    if (upRaw) upPulses++;
    if (downRaw) downPulses++;

    if ((upPulses > 0 || downPulses > 0) && firstPulseTime == 0) {
        firstPulseTime = millis();
    }

    // Pokud stiskneme jiné tlačítko, okamžitě resetujeme akumulátor skrolování
    if (btn || left || right) {
        upPulses = 0;
        downPulses = 0;
        firstPulseTime = 0;
    }

    bool up = false;
    bool down = false;
    int jumpSteps = 0;

    // Pokud uběhlo 150ms od prvního pulzu z trackballu, vyhodnotíme délku pohybu
    if (firstPulseTime > 0 && (millis() - firstPulseTime > 150)) {
        if (upPulses > downPulses) {
            up = true;
            if (upPulses >= 4) jumpSteps = 3;
            else if (upPulses >= 2) jumpSteps = 2;
            else jumpSteps = 1;
        } else if (downPulses > upPulses) {
            down = true;
            if (downPulses >= 4) jumpSteps = 3;
            else if (downPulses >= 2) jumpSteps = 2;
            else jumpSteps = 1;
        }
        
        // Reset akumulátorů
        upPulses = 0;
        downPulses = 0;
        firstPulseTime = 0;
    }

    if (currentState == STATE_MENU) {
        if (up) {
            selectedMenuIndex -= jumpSteps;
            while (selectedMenuIndex < 0) selectedMenuIndex += menuCount;
            displayNeedsUpdate = true;
            fullUpdateNeeded = false; // Rychlé překreslení bez blikání
        }
        else if (down) {
            selectedMenuIndex += jumpSteps;
            while (selectedMenuIndex >= menuCount) selectedMenuIndex -= menuCount;
            displayNeedsUpdate = true;
            fullUpdateNeeded = false; // Rychlé překreslení
        }
        else if (btn) {
            // Potvrzení položky v menu
            if (selectedMenuIndex == 0) { // 1. Pokracovat ve cteni (Seznam zalozek)
                loadBookmarksList();
                currentState = STATE_BOOKMARKS;
                displayNeedsUpdate = true;
                fullUpdateNeeded = true;
            } else if (selectedMenuIndex == 1) { // 2. Knihovna (SD Karta)
                currentState = STATE_LIBRARY;
                loadLibrary();
                selectedFileIndex = 0;
                displayNeedsUpdate = true;
                fullUpdateNeeded = true;
            } else if (selectedMenuIndex == 2) { // 3. Wi-Fi a Web server
                setupWiFi();
            } else if (selectedMenuIndex == 3) { // 4. Systém
                currentState = STATE_SYSTEM;
                displayNeedsUpdate = true;
                fullUpdateNeeded = true;
            }
        }
    } 
    else if (currentState == STATE_BOOKMARKS) {
        if (up) {
            if (recentBookmarkCount > 0) {
                selectedBookmarkIndex -= jumpSteps;
                while (selectedBookmarkIndex < 0) selectedBookmarkIndex += recentBookmarkCount;
            }
            displayNeedsUpdate = true; fullUpdateNeeded = false;
        } else if (down) {
            if (recentBookmarkCount > 0) {
                selectedBookmarkIndex += jumpSteps;
                while (selectedBookmarkIndex >= recentBookmarkCount) selectedBookmarkIndex -= recentBookmarkCount;
            }
            displayNeedsUpdate = true; fullUpdateNeeded = false;
        } else if (btn) {
            if (recentBookmarkCount > 0) {
                Bookmark b = recentBookmarks[selectedBookmarkIndex];
                openFile(b.filename, b.position);
                currentState = STATE_READING;
                displayNeedsUpdate = true; fullUpdateNeeded = true;
            } else {
                currentState = STATE_MENU;
                displayNeedsUpdate = true; fullUpdateNeeded = true;
            }
        }
    }
    else if (currentState == STATE_LIBRARY) {
        if (up) {
            if (fileCount > 0) {
                selectedFileIndex -= jumpSteps;
                while (selectedFileIndex < 0) selectedFileIndex += fileCount;
            }
            displayNeedsUpdate = true; fullUpdateNeeded = false;
        } else if (down) {
            if (fileCount > 0) {
                selectedFileIndex += jumpSteps;
                while (selectedFileIndex >= fileCount) selectedFileIndex -= fileCount;
            }
            displayNeedsUpdate = true; fullUpdateNeeded = false;
        } else if (btn) {
            openFile(fileList[selectedFileIndex]);
            currentState = STATE_READING;
            displayNeedsUpdate = true; fullUpdateNeeded = true;
        }
    }
    else if (currentState == STATE_READING) {
        if (!lookupMode) {
            if (btn) {
                lookupMode = true;
                selectedWordIndex = 0;
                lookupResult = "";
                displayNeedsUpdate = true;
                fullUpdateNeeded = false;
            } else if (up) {
                skipPages(-10 * jumpSteps); // Skočit o cca kapitolu zpět
            } else if (down) {
                skipPages(10 * jumpSteps);  // Skočit o cca kapitolu vpřed
            }
        } else {
            // Režim slovníku
            if (right) {
                selectedWordIndex++; lookupResult = "";
                displayNeedsUpdate = true; fullUpdateNeeded = false;
            } else if (left) {
                if (selectedWordIndex > 0) selectedWordIndex--; lookupResult = "";
                displayNeedsUpdate = true; fullUpdateNeeded = false;
            } else if (up) {
                selectedWordIndex = max(0, selectedWordIndex - (8 * jumpSteps)); lookupResult = "";
                displayNeedsUpdate = true; fullUpdateNeeded = false;
            } else if (down) {
                selectedWordIndex += (8 * jumpSteps); lookupResult = "";
                displayNeedsUpdate = true; fullUpdateNeeded = false;
            } else if (btn) {
                if (lookupResult != "") {
                    lookupMode = false;
                    lookupResult = "";
                    displayNeedsUpdate = true; fullUpdateNeeded = true;
                } else {
                // Najít konkrétní slovo v textu podle indexu
                int wordIdx = 0;
                String selectedWord = "";
                String currentWord = "";
                for (int i = 0; i <= currentPageText.length(); i++) {
                    char c = (i < currentPageText.length()) ? currentPageText[i] : ' ';
                    if (c == ' ' || c == '\n' || i == currentPageText.length()) {
                        if (currentWord.length() > 0) {
                            if (wordIdx == selectedWordIndex) selectedWord = currentWord;
                            wordIdx++;
                            currentWord = "";
                        }
                    } else {
                        currentWord += c;
                    }
                }
                
                lookupResult = lookupWord(selectedWord);
                displayNeedsUpdate = true;
                fullUpdateNeeded = false;
                }
            }
        }
    }
    else if (currentState == STATE_SYSTEM) {
        if (btn) {
            activeFontType = (activeFontType + 1) % 3;
            preparePageText();
            displayNeedsUpdate = true;
            fullUpdateNeeded = true;
        } else if (left || up) {
            activeFontSize = 12;
            preparePageText();
            displayNeedsUpdate = true;
            fullUpdateNeeded = true;
        } else if (right || down) {
            activeFontSize = 14;
            preparePageText();
            displayNeedsUpdate = true;
            fullUpdateNeeded = true;
        }
    }
}

// --- JPEG Callback pro obálky knih ---
bool tjpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            uint16_t color = bitmap[j * w + i];
            // Převod RGB565 na Lumu (jas) pro černo/bílý e-ink
            uint8_t r = (color & 0xF800) >> 8;
            uint8_t g = (color & 0x07E0) >> 3;
            uint8_t b = (color & 0x001F) << 3;
            uint8_t luma = (r * 77 + g * 150 + b * 29) >> 8;
            if (luma > 127) {
                display.drawPixel(x + i, y + j, GxEPD_WHITE);
            } else {
                display.drawPixel(x + i, y + j, GxEPD_BLACK);
            }
        }
    }
    return true;
}

// --- Rychlé listování ---
void skipPages(int numPages) {
    if (numPages > 0) {
        for (int i = 0; i < numPages; i++) {
            pushHistory(currentFilePos);
            currentFilePos = nextFilePos;
            preparePageText(); // Vypočítá nextFilePos bez fyzického vykreslování
            if (currentFilePos == nextFilePos) break; // Konec souboru (zabrání zacyklení a Watchdogu)
            yield(); // Zabrání pádu kvůli Watchdogu na ESP32
        }
    } else {
        for (int i = 0; i < (-numPages); i++) {
            if (pageHistoryCount > 0) {
                pageHistoryCount--;
                currentFilePos = pageHistory[pageHistoryCount];
            } else {
                currentFilePos = 0;
                break;
            }
        }
        preparePageText();
    }
    displayNeedsUpdate = true;
    fullUpdateNeeded = true;
}

void goToDeepSleep() { 
    Serial.println("Jdu do režimu spánku (Deep Sleep)...");
    saveBookmark(); // Pro jistotu ulozime zalozku i pred spanim
    
    // Zhasneme všechny LEDky a podsvícení před usnutím
    if (PIN_RED != -1) digitalWrite(PIN_RED, LOW);
    digitalWrite(PIN_GRN, LOW);
    ledIsOn = false;
    // Před usnutím displej vyčistíme / vypíšeme status
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        
        bool coverDrawn = false;
        if (currentFilename != "") {
            String coverPath = "/" + currentFilename;
            if (coverPath.endsWith(".txt") || coverPath.endsWith(".TXT")) {
                coverPath = coverPath.substring(0, coverPath.length() - 4);
            }
            coverPath += ".jpg";
            
            if (SD.exists(coverPath)) {
                // Zjistíme rozměry obrázku
                uint16_t w = 0, h = 0;
                TJpgDec.getSdJpgSize(&w, &h, coverPath.c_str());
                
                // Vypočteme vhodné zmenšení (TJpgDec umí jen /1, /2, /4, /8)
                uint8_t scale = 1;
                while ((w / scale > display.width()) || (h / scale > display.height() - 40)) {
                    scale *= 2;
                    if (scale >= 8) break;
                }
                TJpgDec.setJpgScale(scale);
                
                // Vypočítáme vycentrování obrázku na střed plochy (bez 40px spodní lišty)
                int drawW = w / scale;
                int drawH = h / scale;
                int xPos = (display.width() - drawW) / 2;
                int yPos = ((display.height() - 40) - drawH) / 2;
                if (xPos < 0) xPos = 0;
                if (yPos < 0) yPos = 0;

                // Nakreslíme obálku!
                TJpgDec.drawSdJpg(xPos, yPos, coverPath.c_str());
                coverDrawn = true;
                
                // Vrátíme měřítko zpět pro další použití
                TJpgDec.setJpgScale(1);
            }
        }
        
        if (!coverDrawn) {
            // Vnější rámeček jako záloha
            display.drawRect(10, 10, display.width() - 20, display.height() - 20, GxEPD_BLACK);
            display.drawRect(12, 12, display.width() - 24, display.height() - 24, GxEPD_BLACK);
            
            // Velký černý blok uprostřed
            int boxX = 20;
            int boxY = 40;
            int boxW = display.width() - 40;
            int boxH = display.height() - 120;
            display.fillRect(boxX, boxY, boxW, boxH, GxEPD_BLACK);
            
            // Texty uvnitř černého bloku (bílým písmem)
            u8g2Fonts.setForegroundColor(GxEPD_WHITE);
            u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
            
            u8g2Fonts.setFont(u8g2_font_helvB18_te); // Tučné větší písmo
            
            String title = (currentFilename == "") ? "ESP32 E-READER" : currentFilename;
            if (title.endsWith(".txt") || title.endsWith(".TXT")) {
                title = title.substring(0, title.length() - 4);
            }
            
            // Vycentrování nadpisu
            int titleWidth = u8g2Fonts.getUTF8Width(title.c_str());
            u8g2Fonts.setCursor((display.width() - titleWidth) / 2, boxY + (boxH / 2));
            u8g2Fonts.print(title);
        }
        
        // Vracíme zpět černou pro zbytek
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
        
        // Vyčistíme pozadí pro spodní lištu (pokud by obálka zasahovala příliš hluboko)
        display.fillRect(0, display.height() - 40, display.width(), 40, GxEPD_WHITE);
        
        // Spodní informace
        display.drawLine(10, display.height() - 40, display.width() - 10, display.height() - 40, GxEPD_BLACK);
        display.drawLine(10, display.height() - 39, display.width() - 10, display.height() - 39, GxEPD_BLACK);
        
        u8g2Fonts.setFont(u8g2_font_helvB10_te);
        u8g2Fonts.setCursor(15, display.height() - 15);
        if (currentState == STATE_READING) {
            u8g2Fonts.print("ZÁLOŽKA ULOŽENA");
        } else {
            u8g2Fonts.print("VYPNUTO Z MENU");
        }
        
        int uspanoWidth = u8g2Fonts.getUTF8Width("USPÁNO");
        u8g2Fonts.setCursor(display.width() - uspanoWidth - 15, display.height() - 15);
        u8g2Fonts.print("USPÁNO");

    } while (display.nextPage());
    
    // Vypnutí e-inku, aby nebral energii a obraz se zafixoval
    display.powerOff(); 

    // Povolíme probuzení z Deep Sleep na pinu LEVÉHO TLAČÍTKA (dlouhý stisk zapne čtečku)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_LEFT, 0); // Probuzení při LOW

    esp_deep_sleep_start(); 
}

void setup() {
    // Sériový monitor je zpět! TX a RX (Piny 1 a 3) jsou volné.
    Serial.begin(115200);
    
    // Piny pro Trackball jako vstupy s vnitřním PULLUP rezistorem
    pinMode(PIN_UP, INPUT_PULLUP);
    pinMode(PIN_DWN, INPUT_PULLUP);
    pinMode(PIN_LFT, INPUT_PULLUP);
    pinMode(PIN_RHT, INPUT_PULLUP);
    pinMode(PIN_BTN, INPUT_PULLUP);

    // Hardwarová tlačítka pro stránkování
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);

    // Nastavení pinu pro LED, podsvícení a krátké probliknutí (Probuzení = Zelená)
    if (PIN_RED != -1) pinMode(PIN_RED, OUTPUT); 
    pinMode(PIN_GRN, OUTPUT); 
    digitalWrite(PIN_GRN, HIGH); 
    if (PIN_RED != -1) digitalWrite(PIN_RED, LOW);
    

    
    ledIsOn = true;
    ledOnTime = millis();
    
    // Zjištění důvodu probuzení
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        // Zařízení bylo probuzeno tlačítkem trackballu
        Serial.println("Probuzeno tlačítkem z Deep Sleep!");
    }

    initDisplay();
    initSDCard();
    
    // Načteme historii záložek
    loadBookmarksList();
    
    // Ujistíme se, že se menu vykreslí ihned po startu
    displayNeedsUpdate = true; 
    fullUpdateNeeded = true;
    lastActivity = millis();
}

void loop() {
    if (currentState == STATE_WIFI_SERVER && WiFi.status() == WL_CONNECTED) {
        server.handleClient();
        ArduinoOTA.handle();
    }

    // 3. Neoblokující čtení vstupů
    checkHardwareButtons();
    checkTrackball();

    // Softwarové zhasnutí LED pro šetření energií (1 minuta po probuzení / startu)
    if (ledIsOn && (millis() - ledOnTime > 60000)) {
        if (PIN_RED != -1) digitalWrite(PIN_RED, LOW);
        digitalWrite(PIN_GRN, LOW);
        ledIsOn = false;
        Serial.println("LED diody automaticky zhasnutý po 1 minute nečinnosti pro šetření energie.");
    }

    // 3.5 Kontrola baterie (červená LED varování)
    static unsigned long lastBatCheck = 0;
    if (millis() - lastBatCheck > 10000) { // Zkontrolovat každých 10 vteřin
        lastBatCheck = millis();
        int bat = getBatteryPercentage();
        if (bat > 0 && bat <= 20) {
            if (PIN_RED != -1) digitalWrite(PIN_RED, HIGH);
        } else {
            if (PIN_RED != -1 && !ledIsOn) digitalWrite(PIN_RED, LOW);
        }
    }

    // 4. Překreslení displeje pouze při změně stavu (Partial/Full refresh)
    if (displayNeedsUpdate) {
        updateEpaperDisplay();
        displayNeedsUpdate = false;
    }

    // 5. Uspání do Deep Sleep při nečinnosti (> 10 minut)
    if (millis() - lastActivity > 600000) {
        goToDeepSleep();
    }
}

// --- Home Assistant přes MQTT ---
// Místo REST API můžeme použít MQTT (např. knihovna PubSubClient)
#include <PubSubClient.h>

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool sendMqttCommand(String topic, String payload) {
    // 1. Zapnutí Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(100);
        retries++;
    }

    if(WiFi.status() != WL_CONNECTED) return false;

    // 2. Připojení k MQTT brokeru
    mqttClient.setServer("192.168.1.xxx", 1883); // IP vašeho HA / Mosquitto
    if (mqttClient.connect("ESPeReader_Client", MQTT_USER, MQTT_PASS)) {
        // 3. Odeslání zprávy (např. do Node-RED nebo HA automatizace)
        mqttClient.publish(topic.c_str(), payload.c_str());
        
        delay(50); // Krátká pauza pro odeslání bufferu
        mqttClient.disconnect();
    }

    // 4. Okamžité vypnutí Wi-Fi pro šetření baterie
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    return true;
}

// --- WiFiManager (Jako ve WLED) ---
// Zapojíme knihovnu WiFiManager, abychom nemuseli psát heslo do kódu
#include <WiFiManager.h>

void connectWifiSafely() {
    WiFiManager wifiManager;
    // Nastavíme timeout (např. 180 vteřin), aby čtečka nevybila 
    // baterii čekáním na konfiguraci, pokud není známá síť v dosahu.
    wifiManager.setConfigPortalTimeout(180);
    
    // Zkusí se připojit. Pokud selže, vytvoří AP "Ctecka_Setup"
    if (!wifiManager.autoConnect("Ctecka_Setup")) {
        Serial.println("Chyba připojení, nebo timeout. Jdu spát.");
        goToDeepSleep();
    }
    Serial.println("Připojeno k Wi-Fi!");
}

// --- Online Slovník / Wikipedia API ---
#include <HTTPClient.h>
#include <ArduinoJson.h> // Pro parsování JSON odpovědi (instalovat přes správce)

String lookupWordWikipedia(String word) {
    if(WiFi.status() != WL_CONNECTED) return "Definice nenalezena.";

    HTTPClient http;
    // URL Wikipedie pro stručný výtah hledaného slova (cs.wikipedia.org)
    String url = "https://cs.wikipedia.org/api/rest_v1/page/summary/" + word;
    
    http.begin(url);
    int httpCode = http.GET();
    String payload = "Definice nenalezena.";

    if (httpCode == 200) {
        String response = http.getString();
        
        // Zpracování obdrženého JSONu a vytažení konkrétního políčka "extract"
        JsonDocument doc;
        deserializeJson(doc, response);
        payload = doc["extract"].as<String>(); 
    }
    
    http.end();
    return payload; // Vrátí čistý text pro displej
}

// --- Práce s pamětí a textem ---
void loadBookChunk(int pageNumber) {
    // C++ efektivně nahrává z SD karty jen požadovanou část souboru (CHUNK)
    // do PSRAM (pokud má deska paměť WROVER), nebo po malých blocích do RAM.
    // Nepokoušíme se načíst celý ePub/TXT najednou!
}
