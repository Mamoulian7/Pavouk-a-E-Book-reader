/*
 * Pavouk – Robot (ESP32-C3 SuperMini)
 * Ovládání 12 serv přes PCA9685 (I2C)
 * Komunikace přes ESP-NOW s ovladačem (M5StickS3)
 *
 * Úprava: zvednutí těžiště (vyšší výchozí femur, menší "tall" úhel -> delší noha)
 * - snížení neutrálního femuru (víc natáhnuto) pro zvýšení těžiště
 * - lehké přizpůsobení squat/tall hodnot
 * - ponechána adaptivní délka a rychlost kroku
 */

#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "pavouk_config.h"

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

const int POCET_SERV = 12;

// ---------- Fyzické úhly (Základní postoj a sed) ----------
const int coxaBase[4] = {130, 115, 45, 35};
const int femurBase = 130; 
const int tibiaBase = 125;
const int femurSit = 145;
const int tibiaSit = 130;

// Nastavení pulzů pro serva (v tickách pro PCA9685)
// 50Hz = 20ms perioda = 20000us
// 1 tick = 20000us / 4096 = 4.88us
// Min pulz 500us = ~102 ticků
// Max pulz 2400us = ~492 ticků
#define SERVOMIN  102 
#define SERVOMAX  492

// ---------- Softwarová inverze serv ----------
// Pokud servo chodí na druhou stranu (nebo je mechanicky zrcadlově), změňte false na true.
// Indexy: 0=Coxa, 1=Femur, 2=Tibia
bool invertServo[12] = {
    true,  false, false, // Noha 0 (Přední Levá)   - Coxa invertována
    false, true,  true,  // Noha 1 (Přední Pravá)  - Femur a Tibia invertovány
    true,  true,  true,  // Noha 2 (Zadní Levá)    - VŠE invertováno (dle vašeho zjištění)
    false, false, false  // Noha 3 (Zadní Pravá)
};

// ---------- Mechanická korekce (Trim) ----------
// Pokud se pavouk při chůzi vpřed mírně stáčí na jednu stranu (mechanická asymetrie),
// můžete sem přidat kompenzaci (např. -5 nebo 5). 
int trimRotaceVpred = -26; // Záporné = korekce doleva, Kladné = korekce doprava

// ---------- ESP-NOW struktury ----------
typedef struct pavouk_command {
    int8_t x;
    int8_t y;
    bool tlA;
    bool tlB;
} pavouk_command;

typedef struct pavouk_telemetry {
    uint8_t stav; // 0=STOJ, 1=CHUZE, 2=PREKAZKA!, 3=SED
} pavouk_telemetry;

// --- GYRO (MPU-6050) ---
#define MPU_ADDR 0x68
float smoothedPitch = 0.0;
float smoothedRoll = 0.0;
bool gyroEnabled = true; // Přepínač pro auto-leveling
float GYRO_MULTIPLIER = 0.6; // Stabilní síla vyvažování

pavouk_command prikaz;
pavouk_telemetry telemetrie;
uint8_t controller_mac[6] = {0, 0, 0, 0, 0, 0};
bool controller_known = false;

enum Stav { SED, STOJ, CHUZE, VYHY_PREKAZCE, MAVANI, TANECEK };
Stav aktualniStav = SED;
bool bylUzPostaven = false;

// ---------- ČTENÍ GYROSKOPU S FILTREM ----------
void readGyro() {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B); // Start registr pro Akcelerometr
    if (Wire.endTransmission(false) != 0) return; // Gyro není připojeno nebo I2C chyba
    
    // Ochrana proti selhání I2C při napěťových špičkách od serv
    uint8_t bytes = Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6, (uint8_t)true);
    if (bytes != 6) {
        while(Wire.available()) Wire.read();
        return;
    }
    
    // Explicitní sekvenční čtení po bajtech (řeší problém s pořadím vyhodnocení v C++)
    uint8_t x_h = Wire.read();
    uint8_t x_l = Wire.read();
    uint8_t y_h = Wire.read();
    uint8_t y_l = Wire.read();
    uint8_t z_h = Wire.read();
    uint8_t z_l = Wire.read();
    
    int16_t ax = (int16_t)((x_h << 8) | x_l);
    int16_t ay = (int16_t)((y_h << 8) | y_l);
    int16_t az = (int16_t)((z_h << 8) | z_l);
    
    // Ochrana proti nesmyslným datům (všechny bity v 1 nebo 0)
    if (ax == -1 && ay == -1 && az == -1) return;
    if (ax == 0 && ay == 0 && az == 0) return;

    // KRITICKÁ OCHRANA PROTI PŘEPADNUTÍ NA TLAMU:
    // Pokud je az < 2000 (ráz z chůze, odlehčení nohy), atan2 by skočilo o 180 stupňů a vystřelilo nohu do maxima!
    if (az < 2000) return;

    float rawPitch = atan2((float)ax, (float)az) * 57.29578f; 
    float rawRoll  = atan2((float)ay, (float)az) * 57.29578f; 
    
    // Bezpečnostní limit náklonu pro kompenzaci (max ±30 stupňů)
    rawPitch = constrain(rawPitch, -30.0f, 30.0f);
    rawRoll  = constrain(rawRoll,  -30.0f, 30.0f);
    
    // Hladký dolnopropustný filtr (Low-Pass)
    smoothedPitch = smoothedPitch * 0.85f + rawPitch * 0.15f;
    smoothedRoll  = smoothedRoll  * 0.85f + rawRoll  * 0.15f;
}

// ---------- Funkce pro serva (Hardware PWM přes PCA9685) ----------
void servoWrite(int servoNum, int angle) {
    if (servoNum >= 0 && servoNum < POCET_SERV) {
        // Aplikace inverze, pokud je pro dané servo zapnutá
        if (invertServo[servoNum]) {
            angle = 180 - angle;
        }
        
        if (angle < 0) angle = 0;
        if (angle > 180) angle = 180;
        
        // Převod úhlu 0-180 na PCA9685 ticky (SERVOMIN až SERVOMAX)
        int pulselen = map(angle, 0, 180, SERVOMIN, SERVOMAX);
        pwm.setPWM(servoNum, 0, pulselen);
    }
}

// Pomocná funkce pro nastavení celé nohy najednou
// Nohy: 0=Přední Levá, 1=Přední Pravá, 2=Zadní Levá, 3=Zadní Pravá
void setLeg(int leg, int coxa, int femur, int tibia) {
    int c_pin = leg * 3;
    int f_pin = leg * 3 + 1;
    int t_pin = leg * 3 + 2;

    // Zrcadlení už neřešíme tady, řeší ho pole invertServo[] automaticky!

    servoWrite(c_pin, coxa);
    servoWrite(f_pin, femur);
    servoWrite(t_pin, tibia);
}

// ---------- ESP-NOW Callbacks ----------
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (len == sizeof(pavouk_command)) {
        memcpy(&prikaz, incomingData, sizeof(pavouk_command));

        if (!controller_known) {
            memcpy(controller_mac, info->src_addr, 6);
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, controller_mac, 6);
            peerInfo.channel = 0;
            peerInfo.encrypt = false;
            if (esp_now_add_peer(&peerInfo) == ESP_OK) {
                controller_known = true;
                Serial.println("Ovladač spárován!");
            }
        }
    }
}


void setup() {
    Serial.begin(115200);

    // Inicializace I2C a PCA9685
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Test připojení PCA9685
    Wire.beginTransmission(0x40);
    if (Wire.endTransmission() == 0) {
        Serial.println("PCA9685 NALEZEN na I2C!");
    } else {
        Serial.println("CHYBA: PCA9685 NENALEZEN! Zkontroluj SDA (8) a SCL (9) a napájení VCC.");
    }

    // Probuzení gyroskopu (zápis nuly do Power Management registru)
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x6B); // PWR_MGMT_1 registr
    Wire.write(0);    // 0 = probudit
    Wire.endTransmission(true);

    // Hardwarový digitální filtr (DLPF) v čipu MPU6050 na 10 Hz
    // Zlikviduje veškeré vibrace serv a mechanické rázy před výpočtem
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x1A); // CONFIG registr
    Wire.write(0x05); // DLPF_CFG = 5 (10 Hz low pass filtr)
    Wire.endTransmission(true);

    // Nastavení rozsahu akcelerometru na ±4g
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x1C); // ACCEL_CONFIG registr
    Wire.write(0x08); // ±4g
    Wire.endTransmission(true);

    pwm.begin();
    pwm.setOscillatorFrequency(27000000);
    pwm.setPWMFreq(50); // Analogová serva běží na ~50 Hz
    delay(10);
    
    // Po zapnutí si bezpečně lehne břichem na zem (žádné přetížení serv ani pád)
    sedNaBrise();

    // ESP-NOW
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Chyba ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);

    Serial.print("Pavouk MAC: ");
    Serial.println(WiFi.macAddress());
}

// ---------- Gesta a animace ----------
void loop() {
    // 1. Pokud ještě nebyl spárován ovladač, sedí v klidu na břiše
    if (!controller_known) {
        aktualniStav = SED;
        sedNaBrise();
        telemetrie.stav = 3;
        delay(50);
        return;
    }

    // 2. Jakmile se ovladač poprvé připojí, plynule vstane
    if (!bylUzPostaven) {
        Serial.println("Ovladač připojen -> Vstávám do základního postoje!");
        plynuleVstan();
        aktualniStav = STOJ;
    }

    // Reagujeme na gesta (tlačítka) nebo na pohyb
    if (prikaz.tlA) {
        aktualniStav = MAVANI;
    } else if (prikaz.tlB) {
        aktualniStav = TANECEK;
    } else if (abs(prikaz.y) > 15 || abs(prikaz.x) > 15) {
        aktualniStav = CHUZE;
    } else {
        aktualniStav = STOJ;
    }

    // Načtení gyra pro auto-leveling
    readGyro();

    switch(aktualniStav) {
        case SED:
            sedNaBrise();
            telemetrie.stav = 3;
            break;
        case STOJ:
            zakladniPostoj();
            telemetrie.stav = 0;
            break;
        case CHUZE:
            krocChuzou();
            telemetrie.stav = 1;
            break;
        case VYHY_PREKAZCE:
            zakladniPostoj();
            telemetrie.stav = 2;
            break;
        case MAVANI:
            mavejPravouPredni();
            // Návrat tlačítka - vyčkáme, až se pustí
            while(prikaz.tlA) delay(50); 
            zakladniPostoj();
            aktualniStav = STOJ;
            telemetrie.stav = 0;
            break;
        case TANECEK:
            viteznyTanecek();
            while(prikaz.tlB) delay(50);
            zakladniPostoj();
            aktualniStav = STOJ;
            telemetrie.stav = 0;
            break;
    }

    static unsigned long lastTele = 0;
    if (millis() - lastTele > 1000 && controller_known) {
        esp_now_send(controller_mac, (uint8_t *)&telemetrie, sizeof(telemetrie));
        lastTele = millis();
    }

    delay(20);
}
