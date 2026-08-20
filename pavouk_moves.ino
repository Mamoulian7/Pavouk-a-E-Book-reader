// ---------- Pohybová logika ----------

// Pavouk leží břichem na zemi (úspora proudu, bezpečný start)
void sedNaBrise() {
    for (int i = 0; i < 4; i++) {
        setLeg(i, coxaBase[i], femurSit, tibiaSit);
    }
}

// Plynulé vstávání ze sedu do základního postoje
void plynuleVstan() {
    const int femurStart = 145;
    const int tibiaStart = 130;
    const int femurEnd = 130;
    const int tibiaEnd = 125;

    for (int step = 0; step <= 30; step++) {
        float progress = (float)step / 30.0f;
        float ease = (1.0f - cos(progress * 3.14159f)) / 2.0f;

        int f = femurStart + (femurEnd - femurStart) * ease;
        int t = tibiaStart + (tibiaEnd - tibiaStart) * ease;

        for (int i = 0; i < 4; i++) {
            setLeg(i, coxaBase[i], f, t);
        }
        delay(25);
    }
    bylUzPostaven = true;
}

// Stabilní postoj s nízkým těžištěm
void zakladniPostoj() {
    float pComp = 0.0;
    float rComp = 0.0;
    if (gyroEnabled) {
        if (abs(smoothedPitch) > 2.0) {
            pComp = (smoothedPitch > 0 ? smoothedPitch - 2.0 : smoothedPitch + 2.0) * GYRO_MULTIPLIER;
        }
        if (abs(smoothedRoll) > 2.0) {
            rComp = (smoothedRoll > 0 ? smoothedRoll - 2.0 : smoothedRoll + 2.0) * GYRO_MULTIPLIER;
        }
    }

    for(int i=0; i<4; i++) {
        float finalFemur = femurBase;
        
        if (gyroEnabled) {
            // PITCH
            if (i == 0 || i == 1) finalFemur -= pComp;
            else finalFemur += pComp;
            
            // ROLL
            if (i == 0 || i == 2) finalFemur -= rComp;
            else finalFemur += rComp;
            
            finalFemur = constrain(finalFemur, 55, 180);
        }
        
        setLeg(i, coxaBase[i], finalFemur, tibiaBase); 
    }
}

void krocChuzou() {
    static float currentCycle = 0.0;
    unsigned long now = millis();
    static unsigned long lastUpdate = now;
    float dt = (now - lastUpdate) / 1000.0;
    lastUpdate = now;

    int maxJoy = max(abs(prikaz.y), abs(prikaz.x));
    if (maxJoy < 15) return; 

    float speedHz = map(maxJoy, 0, 127, 50, 150) / 100.0;
    currentCycle += speedHz * dt;
    if (currentCycle >= 1.0) currentCycle -= 1.0;

    int femurStand = 130;
    int femurLift = map(maxJoy, 0, 127, 150, 175);
    int tibiaStand = 125;
    int tibiaLift = map(maxJoy, 0, 127, 100, 90);

    float phaseOffsets[4] = {0.0, 0.50, 0.25, 0.75};

    float legCycles[4];
    int swingLeg = -1;
    float swingProgress = 0.0;

    for (int i=0; i<4; i++) {
        legCycles[i] = currentCycle + phaseOffsets[i];
        if (legCycles[i] >= 1.0) legCycles[i] -= 1.0;
        
        if (legCycles[i] < 0.25) {
            swingLeg = i;
            swingProgress = legCycles[i] / 0.25;
        }
    }

    float targetFemur[4];
    float targetTibia[4];
    for(int i=0; i<4; i++) {
        targetFemur[i] = femurStand;
        targetTibia[i] = tibiaStand;
    }

    if (swingLeg != -1) {
        float liftSine = sin(swingProgress * 3.14159);
        
        targetFemur[swingLeg] = femurStand + (femurLift - femurStand) * liftSine;
        targetTibia[swingLeg] = tibiaStand + (tibiaLift - tibiaStand) * liftSine;
        
        int opposite = 3 - swingLeg;
        targetFemur[opposite] = femurStand; // ZRUŠENÝ squat - protilehlá noha už nejde k zemi
        
        for(int j=0; j<4; j++) {
            if(j != swingLeg && j != opposite) {
                targetFemur[j] = femurStand; // ZRUŠENÝ tall - ostatní nohy už tělo nezvedají
            }
        }
    }

    float targetCoxa[4];
    
    for (int i=0; i<4; i++) {
        int legJoy = prikaz.y; 
        
        int rotace = prikaz.x;
        if (prikaz.y > 10 && prikaz.x == 0) rotace += trimRotaceVpred; 

        if (i == 0 || i == 2) legJoy += rotace;
        else legJoy -= rotace;

        legJoy = constrain(legJoy, -127, 127);

        int offFwd = map(abs(legJoy), 0, 127, 15, 45);
        int offBwd = map(abs(legJoy), 0, 127, -15, -45);

        if (legJoy < 0) {
            int temp = offFwd; offFwd = offBwd; offBwd = temp;
        }

        if (legCycles[i] < 0.25) {
            float easeProgress = (1.0 - cos(swingProgress * 3.14159)) / 2.0;
            targetCoxa[i] = coxaBase[i] + offBwd + (offFwd - offBwd) * easeProgress;
        } else {
            float stanceProgress = (legCycles[i] - 0.25) / 0.75;
            targetCoxa[i] = coxaBase[i] + offFwd - (offFwd - offBwd) * stanceProgress;
        }
    }

    float pComp = 0.0;
    float rComp = 0.0;
    if (gyroEnabled) {
        if (abs(smoothedPitch) > 2.0) {
            pComp = (smoothedPitch > 0 ? smoothedPitch - 2.0 : smoothedPitch + 2.0) * GYRO_MULTIPLIER;
        }
        if (abs(smoothedRoll) > 2.0) {
            rComp = (smoothedRoll > 0 ? smoothedRoll - 2.0 : smoothedRoll + 2.0) * GYRO_MULTIPLIER;
        }
    }

    for(int i=0; i<4; i++) {
        float finalFemur = targetFemur[i];
        
        if (gyroEnabled) {
            if (i == 0 || i == 1) finalFemur -= pComp; 
            else finalFemur += pComp; 
            
            if (i == 0 || i == 2) finalFemur -= rComp; 
            else finalFemur += rComp; 
            
            finalFemur = constrain(finalFemur, 55, 180);
        }

        setLeg(i, targetCoxa[i], finalFemur, targetTibia[i]);
    }
}

// ---------- Gesta a animace ----------
void mavejPravouPredni() {
    Serial.println("Gestikulace: Mavani");
    int tempCoxa = coxaBase[1];
    int tempFemur = femurBase;
    int tempTibia = tibiaBase;

    for(int i=0; i<=15; i++) {
        tempFemur = femurBase + i*3;
        tempCoxa = coxaBase[1] + i*2;
        setLeg(1, tempCoxa, tempFemur, tempTibia);
        delay(20);
    }
    
    for(int w=0; w<3; w++) {
        for(int i=0; i<15; i++) {
            setLeg(1, tempCoxa + i, tempFemur, tempTibia + i*2);
            delay(15);
        }
        for(int i=15; i>0; i--) {
            setLeg(1, tempCoxa + i, tempFemur, tempTibia + i*2);
            delay(15);
        }
    }
    
    for(int i=15; i>=0; i--) {
        tempFemur = femurBase + i*3;
        tempCoxa = coxaBase[1] + i*2;
        setLeg(1, tempCoxa, tempFemur, tempTibia);
        delay(20);
    }
}

void viteznyTanecek() {
    Serial.println("Gestikulace: Tanecek");
    int targetFemur = femurBase + 10;
    for(int k=0; k<4; k++) {
        setLeg(k, coxaBase[k], targetFemur, tibiaBase);
    }
    delay(200);

    for(int t=0; t<3; t++) {
        setLeg(0, coxaBase[0], targetFemur - 20, tibiaBase);
        setLeg(2, coxaBase[2], targetFemur - 20, tibiaBase);
        setLeg(1, coxaBase[1], targetFemur + 20, tibiaBase);
        setLeg(3, coxaBase[3], targetFemur + 20, tibiaBase);
        delay(250);
        
        setLeg(0, coxaBase[0], targetFemur + 20, tibiaBase);
        setLeg(2, coxaBase[2], targetFemur + 20, tibiaBase);
        setLeg(1, coxaBase[1], targetFemur - 20, tibiaBase);
        setLeg(3, coxaBase[3], targetFemur - 20, tibiaBase);
        delay(250);
    }
    
    zakladniPostoj();
}
