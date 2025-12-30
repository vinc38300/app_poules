#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <time.h>

// Définir les broches des relais
#define RELAY_1_PIN 2  // Distribution de grain
#define RELAY_2_PIN 4  // Ouverture de la porte

// Constantes de fuseau horaire français
#define TIMEZONE_OFFSET_WINTER 1    // UTC+1 (heure d'hiver)
#define TIMEZONE_OFFSET_SUMMER 2    // UTC+2 (heure d'été)

// UUIDs exactement identiques à ceux de l'app
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define TIME_CHARACTERISTIC_UUID "123e4567-e89b-12d3-a456-426614174000"
#define RELAY1_TIME_UUID "5a786b5e-1234-5678-9abc-def012345678"
#define RELAY2_TIME_UUID "6b897c6f-2345-6789-abcd-ef0123456789"
#define TIME_SYNC_UUID "12345678-1234-1234-1234-123456789abc"
#define STATS_UUID "87654321-4321-4321-4321-210987654321"
#define WEEK_STATS_UUID "11111111-2222-3333-4444-555555555555"

// Variables BLE
BLEServer* pServer;
BLEService* pService;
BLECharacteristic* pCharacteristic;
BLECharacteristic* pTimeCharacteristic;
BLECharacteristic* pRelay1TimeCharacteristic;
BLECharacteristic* pRelay2TimeCharacteristic;
BLECharacteristic* pTimeSyncCharacteristic;
BLECharacteristic* pStatsCharacteristic;
BLECharacteristic* pWeekStatsCharacteristic;
bool deviceConnected = false;
bool servicesReady = false;

// Variables time management
unsigned long timeOffset = 0;
unsigned long bootTime = 0;
bool isDST = false;
Preferences preferences;

// Variables relais - Distribution de grain (durée fixe)
int startHour1 = 8, startMinute1 = 0, startSecond1 = 0;
unsigned long durationSeconds1 = 1200; // 20 minutes par défaut
bool manualOverride1 = false;
bool relay1State = false;
unsigned long relay1LastActivation = 0;
unsigned long relay1DailyCount = 0;

// Variables relais - Ouverture porte (plage horaire)
int startHour2 = 7, startMinute2 = 0, startSecond2 = 0;
int stopHour2 = 21, stopMinute2 = 0, stopSecond2 = 0;
bool manualOverride2 = false;
bool relay2State = false;
unsigned long relay2LastActivation = 0;
unsigned long relay2DailyCount = 0;

// Variables statistiques
unsigned long systemStartTime = 0;
unsigned long lastStatsReset = 0;

// Statistiques de la semaine (7 jours)
uint16_t weekGrainStats[7] = {0, 0, 0, 0, 0, 0, 0}; // Lun-Dim
uint16_t weekDoorStats[7] = {0, 0, 0, 0, 0, 0, 0};  // Lun-Dim
int currentDayOfWeek = 0; // 0=Lundi, 6=Dimanche

// Variables pour forcer les notifications
bool forceNotifyRelay1 = false;
bool forceNotifyRelay2 = false;
bool forceNotifyStats = false;
bool forceNotifyWeekStats = false;

// Fonction pour obtenir le jour de la semaine (0=Lundi, 6=Dimanche)
int getDayOfWeek(unsigned long timestamp) {
  unsigned long days = timestamp / 86400;
  return (days + 3) % 7; // +3 pour ajuster au format Lundi=0
}

// Fonction pour déterminer si on est en heure d'été (DST)
bool isDaylightSavingTime(int year, int month, int day) {
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;
  
  int lastSunday;
  if (month == 3) {
    lastSunday = 31 - ((5 * year / 4 + 4) % 7);
    return day >= lastSunday;
  } else {
    lastSunday = 31 - ((5 * year / 4 + 1) % 7);
    return day < lastSunday;
  }
}

// Mettre à jour les statistiques de la semaine
void updateWeekStats(unsigned long timestamp, uint8_t relayNumber, bool action, bool isAuto) {
  if (!action || !isAuto) return; // Compter seulement les activations automatiques
  
  int dayOfWeek = getDayOfWeek(timestamp);
  
  if (relayNumber == 1 && dayOfWeek >= 0 && dayOfWeek < 7) {
    weekGrainStats[dayOfWeek]++;
    Serial.printf("📊 Statistiques grain jour %d: %d\n", dayOfWeek, weekGrainStats[dayOfWeek]);
  } else if (relayNumber == 2 && dayOfWeek >= 0 && dayOfWeek < 7) {
    weekDoorStats[dayOfWeek]++;
    Serial.printf("📊 Statistiques porte jour %d: %d\n", dayOfWeek, weekDoorStats[dayOfWeek]);
  }
  
  saveWeekStats();
  forceNotifyWeekStats = true; // CORRECTION: Forcer la notification
}

// Réinitialiser les compteurs journaliers à minuit
void checkDailyReset() {
  if (!isTimeSynchronized()) return;
  
  unsigned long currentTimestamp = timeOffset + ((millis() - bootTime) / 1000);
  unsigned long currentDay = currentTimestamp / 86400;
  unsigned long lastResetDay = lastStatsReset / 86400;
  
  if (currentDay > lastResetDay) {
    relay1DailyCount = 0;
    relay2DailyCount = 0;
    lastStatsReset = currentTimestamp;
    
    // Mise à jour du jour de la semaine
    currentDayOfWeek = getDayOfWeek(currentTimestamp);
    
    Serial.println("📊 Compteurs journaliers réinitialisés");
    Serial.printf("📅 Jour de la semaine: %d (0=Lun, 6=Dim)\n", currentDayOfWeek);
    saveSettings();
    forceNotifyStats = true; // CORRECTION: Forcer la notification
  }
}

// Sauvegarder les statistiques de la semaine
void saveWeekStats() {
  preferences.begin("weekstats", false);
  for (int i = 0; i < 7; i++) {
    String keyGrain = "grain" + String(i);
    String keyDoor = "door" + String(i);
    preferences.putUShort(keyGrain.c_str(), weekGrainStats[i]);
    preferences.putUShort(keyDoor.c_str(), weekDoorStats[i]);
  }
  preferences.putInt("currentDay", currentDayOfWeek);
  preferences.end();
}

// Charger les statistiques de la semaine
void loadWeekStats() {
  preferences.begin("weekstats", true);
  for (int i = 0; i < 7; i++) {
    String keyGrain = "grain" + String(i);
    String keyDoor = "door" + String(i);
    weekGrainStats[i] = preferences.getUShort(keyGrain.c_str(), 0);
    weekDoorStats[i] = preferences.getUShort(keyDoor.c_str(), 0);
  }
  currentDayOfWeek = preferences.getInt("currentDay", 0);
  preferences.end();
  Serial.println("📊 Statistiques de la semaine chargées");
}

void saveSettings() {
  preferences.begin("relays", false);
  preferences.putInt("startHour1", startHour1);
  preferences.putInt("startMinute1", startMinute1);
  preferences.putInt("startSecond1", startSecond1);
  preferences.putULong("duration1", durationSeconds1);
  preferences.putInt("startHour2", startHour2);
  preferences.putInt("startMinute2", startMinute2);
  preferences.putInt("startSecond2", startSecond2);
  preferences.putInt("stopHour2", stopHour2);
  preferences.putInt("stopMinute2", stopMinute2);
  preferences.putInt("stopSecond2", stopSecond2);
  preferences.putULong("timeOffset", timeOffset);
  preferences.putBool("isDST", isDST);
  preferences.putULong("relay1DailyCount", relay1DailyCount);
  preferences.putULong("relay2DailyCount", relay2DailyCount);
  preferences.putULong("lastStatsReset", lastStatsReset);
  preferences.putULong("systemStartTime", systemStartTime);
  preferences.end();
  Serial.println("✅ Paramètres sauvegardés");
}

void loadSettings() {
  preferences.begin("relays", true);
  startHour1 = preferences.getInt("startHour1", 8);
  startMinute1 = preferences.getInt("startMinute1", 0);
  startSecond1 = preferences.getInt("startSecond1", 0);
  durationSeconds1 = preferences.getULong("duration1", 1200); // 20 min par défaut
  startHour2 = preferences.getInt("startHour2", 7);
  startMinute2 = preferences.getInt("startMinute2", 0);
  startSecond2 = preferences.getInt("startSecond2", 0);
  stopHour2 = preferences.getInt("stopHour2", 21);
  stopMinute2 = preferences.getInt("stopMinute2", 0);
  stopSecond2 = preferences.getInt("stopSecond2", 0);
  timeOffset = preferences.getULong("timeOffset", 0);
  isDST = preferences.getBool("isDST", false);
  relay1DailyCount = preferences.getULong("relay1DailyCount", 0);
  relay2DailyCount = preferences.getULong("relay2DailyCount", 0);
  lastStatsReset = preferences.getULong("lastStatsReset", 0);
  systemStartTime = preferences.getULong("systemStartTime", millis());
  preferences.end();
  Serial.println("📂 Paramètres chargés");
}

void getCurrentTime(int& hours, int& minutes, int& seconds) {
  if (!isTimeSynchronized()) {
    hours = 0; minutes = 0; seconds = 0;
    return;
  }
  
  unsigned long currentTimestamp = timeOffset + ((millis() - bootTime) / 1000);
  unsigned long daySeconds = currentTimestamp % 86400;
  
  hours = daySeconds / 3600;
  minutes = (daySeconds % 3600) / 60;
  seconds = daySeconds % 60;
}

bool isTimeSynchronized() {
  return timeOffset != 0;
}

void setTimeFromApp(unsigned long timestamp) {
  bootTime = millis();
  
  time_t rawTime = (time_t)timestamp;
  struct tm *timeInfo = gmtime(&rawTime);
  
  int year = timeInfo->tm_year + 1900;
  int month = timeInfo->tm_mon + 1;
  int day = timeInfo->tm_mday;
  
  isDST = isDaylightSavingTime(year, month, day);
  int offsetHours = isDST ? TIMEZONE_OFFSET_SUMMER : TIMEZONE_OFFSET_WINTER;
  unsigned long localTimestamp = timestamp + (offsetHours * 3600);
  
  timeOffset = localTimestamp;
  if (systemStartTime == 0) {
    systemStartTime = millis();
  }
  
  currentDayOfWeek = getDayOfWeek(localTimestamp);
  
  saveSettings();
  
  Serial.printf("⏰ Heure synchronisée (France): %02d:%02d:%02d\n", 
    timeInfo->tm_hour + offsetHours, timeInfo->tm_min, timeInfo->tm_sec);
  
  int h, m, s;
  getCurrentTime(h, m, s);
  Serial.printf("🕒 Heure française actuelle: %02d:%02d:%02d\n", h, m, s);
  Serial.printf("📅 Jour de la semaine: %d (0=Lun, 6=Dim)\n", currentDayOfWeek);
}

// CORRECTION: Fonction améliorée pour contrôler les relais
void controlRelay(int relay, bool state, bool isAutomatic = false) {
  bool previousState = (relay == 1) ? relay1State : relay2State;
  
  if (relay == 1) {
    digitalWrite(RELAY_1_PIN, state ? LOW : HIGH);
    relay1State = state;
    if (state && !previousState) {
      relay1LastActivation = millis();
      if (isAutomatic) relay1DailyCount++;
    }
    Serial.printf("🌾 Distribution de grain: %s (%s)\n", 
      state ? "ACTIVÉE" : "ARRÊTÉE", isAutomatic ? "Auto" : "Manuel");
  } else if (relay == 2) {
    digitalWrite(RELAY_2_PIN, state ? LOW : HIGH);
    relay2State = state;
    if (state && !previousState) {
      relay2LastActivation = millis();
      if (isAutomatic) relay2DailyCount++;
    }
    Serial.printf("🚪 Ouverture de la porte: %s (%s)\n", 
      state ? "OUVERTE" : "FERMÉE", isAutomatic ? "Auto" : "Manuel");
  }
  
  // Ajouter à l'historique seulement si l'état change
  if (previousState != state) {
    addActionToHistory(relay, state, isAutomatic);
    
    // CORRECTION: Forcer les notifications
    if (relay == 1) forceNotifyRelay1 = true;
    else forceNotifyRelay2 = true;
    forceNotifyStats = true;
  }
}

// Ajouter une action à l'historique
void addActionToHistory(uint8_t relayNumber, bool action, bool isAuto) {
  if (!isTimeSynchronized()) return;
  
  unsigned long currentTimestamp = timeOffset + ((millis() - bootTime) / 1000);
  
  // Mettre à jour les statistiques de la semaine
  updateWeekStats(currentTimestamp, relayNumber, action, isAuto);
  
  Serial.printf("📋 Historique: Relais %d %s (%s)\n", 
    relayNumber, action ? "ON" : "OFF", isAuto ? "Auto" : "Manuel");
}

unsigned long timeToSeconds(int hours, int minutes, int seconds) {
  return hours * 3600 + minutes * 60 + seconds;
}

bool isTimeInRange(unsigned long current, unsigned long start, unsigned long duration) {
  unsigned long end = (start + duration) % 86400;
  if (start < end) {
    return current >= start && current < end;
  } else {
    return current >= start || current < end;
  }
}

bool isTimeInRangeStartStop(unsigned long current, unsigned long start, unsigned long stop) {
  if (start < stop) {
    return current >= start && current < stop;
  } else {
    return current >= start || current < stop;
  }
}

// CORRECTION: Fonction améliorée pour les notifications
void updateTimeCharacteristic() {
  if (!deviceConnected || !servicesReady) return;
  
  if (!isTimeSynchronized()) {
    pTimeCharacteristic->setValue("Non sync");
    pTimeCharacteristic->notify();
    return;
  }
  
  int hours, minutes, seconds;
  getCurrentTime(hours, minutes, seconds);
  char timeStr[20];
  sprintf(timeStr, "%02d:%02d:%02d FR", hours, minutes, seconds);
  pTimeCharacteristic->setValue(timeStr);
  pTimeCharacteristic->notify();
}

// CORRECTION: Fonction améliorée pour les notifications relais
void updateRelayTimesCharacteristic() {
  if (!deviceConnected || !servicesReady) return;
  
  // Format pour Distribution de grain (durée fixe): HH:MM:SS,duration,state,FR,GRAIN,dailyCount
  char relay1TimeStr[80];
  sprintf(relay1TimeStr, "%02d:%02d:%02d,%lu,%s,FR,GRAIN,%lu", 
    startHour1, startMinute1, startSecond1, 
    durationSeconds1,
    relay1State ? "ON" : "OFF",
    relay1DailyCount);
  pRelay1TimeCharacteristic->setValue(relay1TimeStr);
  pRelay1TimeCharacteristic->notify();

  // Format pour Ouverture porte (plage horaire): HH:MM:SS,HH:MM:SS,state,FR,PORTE,dailyCount
  char relay2TimeStr[80];
  sprintf(relay2TimeStr, "%02d:%02d:%02d,%02d:%02d:%02d,%s,FR,PORTE,%lu", 
    startHour2, startMinute2, startSecond2, 
    stopHour2, stopMinute2, stopSecond2,
    relay2State ? "ON" : "OFF",
    relay2DailyCount);
  pRelay2TimeCharacteristic->setValue(relay2TimeStr);
  pRelay2TimeCharacteristic->notify();
  
  Serial.printf("📡 Notifications relais envoyées - Grain:%s Porte:%s\n", 
    relay1State ? "ON" : "OFF", relay2State ? "ON" : "OFF");
}

void updateStatsCharacteristic() {
  if (!deviceConnected || !servicesReady) return;
  
  unsigned long uptime = (millis() - systemStartTime) / 1000 / 3600; // heures
  
  int h, m, s;
  getCurrentTime(h, m, s);
  char lastSyncTime[10];
  sprintf(lastSyncTime, "%02d:%02d", h, m);
  
  char statsStr[100];
  sprintf(statsStr, "%lu,%lu,%lu,%s", 
    relay1DailyCount,    // distributions aujourd'hui
    relay2DailyCount,    // ouvertures aujourd'hui
    uptime,              // temps de fonctionnement en heures
    lastSyncTime);       // dernière synchronisation
  
  pStatsCharacteristic->setValue(statsStr);
  pStatsCharacteristic->notify();
  
  Serial.printf("📊 Stats jour envoyées: %s\n", statsStr);
}

// CORRECTION: Fonction améliorée pour les statistiques de la semaine
void updateWeekStatsCharacteristic() {
  if (!deviceConnected || !servicesReady) return;
  
  // Format: "grain:0,1,2,3,4,5,6|door:0,1,2,3,4,5,6"
  char weekStatsStr[200];
  sprintf(weekStatsStr, "grain:%d,%d,%d,%d,%d,%d,%d|door:%d,%d,%d,%d,%d,%d,%d",
    weekGrainStats[0], weekGrainStats[1], weekGrainStats[2], weekGrainStats[3],
    weekGrainStats[4], weekGrainStats[5], weekGrainStats[6],
    weekDoorStats[0], weekDoorStats[1], weekDoorStats[2], weekDoorStats[3],
    weekDoorStats[4], weekDoorStats[5], weekDoorStats[6]);
  
  pWeekStatsCharacteristic->setValue(weekStatsStr);
  pWeekStatsCharacteristic->notify();
  
  Serial.printf("📊 Stats semaine envoyées: %s\n", weekStatsStr);
}

void printCurrentSettings() {
  Serial.println("\n🔍 === POULAILLER - ÉTAT ACTUEL v4.0 ===");
  Serial.printf("Heure synchronisée: %s\n", isTimeSynchronized() ? "OUI" : "NON");
  
  if (isTimeSynchronized()) {
    int h, m, s;
    getCurrentTime(h, m, s);
    Serial.printf("🕒 Heure française: %02d:%02d:%02d\n", h, m, s);
    Serial.printf("📅 Jour de la semaine: %d (0=Lun, 6=Dim)\n", currentDayOfWeek);
  }
  
  Serial.println("\n--- 🌾 DISTRIBUTION DE GRAIN ---");
  Serial.printf("Mode: %s\n", manualOverride1 ? "Manuel" : "Automatique");
  Serial.printf("État: %s\n", relay1State ? "ACTIVE" : "ARRÊTÉE");
  Serial.printf("Programmation: %02d:%02d:%02d durée %lu min\n", 
    startHour1, startMinute1, startSecond1, durationSeconds1/60);
  Serial.printf("Distributions aujourd'hui: %lu\n", relay1DailyCount);
    
  Serial.println("\n--- 🚪 OUVERTURE DE LA PORTE ---");
  Serial.printf("Mode: %s\n", manualOverride2 ? "Manuel" : "Automatique");
  Serial.printf("État: %s\n", relay2State ? "OUVERTE" : "FERMÉE");
  Serial.printf("Programmation: %02d:%02d:%02d → %02d:%02d:%02d\n", 
    startHour2, startMinute2, startSecond2, stopHour2, stopMinute2, stopSecond2);
  Serial.printf("Ouvertures aujourd'hui: %lu\n", relay2DailyCount);
  
  Serial.println("\n--- 📊 STATISTIQUES SEMAINE ---");
  Serial.print("Grain (L-D): ");
  for (int i = 0; i < 7; i++) {
    Serial.printf("%d ", weekGrainStats[i]);
  }
  Serial.print("\nPorte (L-D): ");
  for (int i = 0; i < 7; i++) {
    Serial.printf("%d ", weekDoorStats[i]);
  }
  
  Serial.printf("\n\n📊 Temps de fonctionnement: %lu heures\n", 
    (millis() - systemStartTime) / 1000 / 3600);
  Serial.println("==========================================\n");
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("📱 Application connectée");
      
      Serial.println("⏳ Attente découverte des services...");
      delay(2000);
      
      servicesReady = true;
      Serial.println("✅ Services prêts pour notifications v4.0");
      
      // CORRECTION: Envoyer immédiatement toutes les données
      delay(500);
      updateTimeCharacteristic();
      delay(100);
      updateRelayTimesCharacteristic();
      delay(100);
      updateStatsCharacteristic();
      delay(100);
      updateWeekStatsCharacteristic();
      
      BLEDevice::startAdvertising();
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      servicesReady = false;
      Serial.println("📱 Application déconnectée");
      delay(1000);
      BLEDevice::startAdvertising();
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
  public:
    void onWrite(BLECharacteristic* pCharacteristic) {
      String uuid = pCharacteristic->getUUID().toString();
      String value = pCharacteristic->getValue();

      Serial.printf("🔧 Commande reçue: UUID %s, Longueur: %d\n", 
        uuid.c_str(), value.length());

      // SYNCHRONISATION TEMPS (code 6)
      if (uuid.equalsIgnoreCase(CHARACTERISTIC_UUID) && value.length() >= 5 && value[0] == 6) {
        unsigned long timestamp = ((unsigned long)(uint8_t)value[1] << 24) | 
                                 ((unsigned long)(uint8_t)value[2] << 16) | 
                                 ((unsigned long)(uint8_t)value[3] << 8) | 
                                 ((unsigned long)(uint8_t)value[4]);
        
        Serial.println("⏰ Synchronisation heure française...");
        setTimeFromApp(timestamp);
        
        delay(100);
        updateTimeCharacteristic();
        updateRelayTimesCharacteristic();
        updateStatsCharacteristic();
        updateWeekStatsCharacteristic();
        return;
      }

      // CONTRÔLE MANUEL DES RELAIS
      if (uuid.equalsIgnoreCase(CHARACTERISTIC_UUID) && value.length() > 0) {
        
        if (value[0] == 1) {
          // Distribution de grain manuelle
          bool state = value[1] == 1;
          manualOverride1 = true;
          controlRelay(1, state, false);
          Serial.printf("🌾 Distribution manuelle: %s\n", state ? "ACTIVÉE" : "ARRÊTÉE");
        } 
        else if (value[0] == 2) {
          // Ouverture porte manuelle
          bool state = value[1] == 1;
          manualOverride2 = true;
          controlRelay(2, state, false);
          Serial.printf("🚪 Porte manuelle: %s\n", state ? "OUVERTE" : "FERMÉE");
        }
        else if (value[0] == 3 && value.length() >= 6) {
          // Programmer distribution de grain (durée fixe)
          startHour1 = value[1];
          startMinute1 = value[2];
          startSecond1 = value[3];
          // Durée en minutes depuis l'app, convertie en secondes
          durationSeconds1 = ((uint16_t)value[4] << 8) | value[5];
          durationSeconds1 *= 60; // Convertir minutes en secondes
          
          // Validation des valeurs
          if (startHour1 > 23) startHour1 = 23;
          if (startMinute1 > 59) startMinute1 = 59;
          if (startSecond1 > 59) startSecond1 = 0;
          if (durationSeconds1 > 86400) durationSeconds1 = 1200; // Max 24h, défaut 20min
          
          manualOverride1 = false;
          saveSettings();
          
          Serial.printf("📅 Distribution programmée: %02d:%02d:%02d pour %lu min\n", 
            startHour1, startMinute1, startSecond1, durationSeconds1/60);
        }
        else if (value[0] == 4 && value.length() >= 7) {
          // Programmer ouverture porte (plage horaire)
          startHour2 = value[1];
          startMinute2 = value[2];
          startSecond2 = value[3];
          stopHour2 = value[4];
          stopMinute2 = value[5];
          stopSecond2 = value[6];
          
          // Validation des valeurs
          if (startHour2 > 23) startHour2 = 23;
          if (startMinute2 > 59) startMinute2 = 59;
          if (startSecond2 > 59) startSecond2 = 0;
          if (stopHour2 > 23) stopHour2 = 23;
          if (stopMinute2 > 59) stopMinute2 = 59;
          if (stopSecond2 > 59) stopSecond2 = 0;
          
          manualOverride2 = false;
          saveSettings();
          
          Serial.printf("📅 Ouverture programmée: %02d:%02d → %02d:%02d\n", 
            startHour2, startMinute2, stopHour2, stopMinute2);
        }
        else if (value[0] == 5) {
          // Mode automatique
          if (value[1] == 1) {
            manualOverride1 = false;
            Serial.println("🔄 Distribution en mode automatique");
          }
          if (value[1] == 2) {
            manualOverride2 = false;
            Serial.println("🔄 Porte en mode automatique");
          }
        }
        
        // CORRECTION: Mettre à jour les notifications immédiatement
        delay(50);
        updateRelayTimesCharacteristic();
        updateStatsCharacteristic();
        updateWeekStatsCharacteristic();
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("🚀 ESP32 Contrôleur Poulailler v4.0 - AVEC STATS 7 JOURS");
  Serial.println("🇫🇷 Fuseau horaire français automatique");

  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  digitalWrite(RELAY_1_PIN, HIGH); // Relais inactif
  digitalWrite(RELAY_2_PIN, HIGH); // Relais inactif

  loadSettings();
  loadWeekStats(); // Charger les statistiques de la semaine
  
  if (timeOffset > 0) {
    bootTime = millis();
    currentDayOfWeek = getDayOfWeek(timeOffset + ((millis() - bootTime) / 1000));
    Serial.println("⏰ Heure française restaurée");
  }

  // Initialisation BLE
  Serial.println("🔵 Initialisation BLE...");
  BLEDevice::init("ESP32-Relay-Controller");
  delay(500);
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  delay(200);

  pService = pServer->createService(SERVICE_UUID);
  delay(200);

  // Création des caractéristiques
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  delay(100);

  pTimeCharacteristic = pService->createCharacteristic(
    TIME_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pTimeCharacteristic->addDescriptor(new BLE2902());
  delay(100);

  pTimeSyncCharacteristic = pService->createCharacteristic(
    TIME_SYNC_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pTimeSyncCharacteristic->setCallbacks(new MyCallbacks());
  delay(100);

  pRelay1TimeCharacteristic = pService->createCharacteristic(
    RELAY1_TIME_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pRelay1TimeCharacteristic->addDescriptor(new BLE2902());
  delay(100);

  pRelay2TimeCharacteristic = pService->createCharacteristic(
    RELAY2_TIME_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pRelay2TimeCharacteristic->addDescriptor(new BLE2902());
  delay(100);

  // Caractéristique pour les statistiques journalières
  pStatsCharacteristic = pService->createCharacteristic(
    STATS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatsCharacteristic->addDescriptor(new BLE2902());
  delay(100);

  // Nouvelle caractéristique pour les statistiques de la semaine
  pWeekStatsCharacteristic = pService->createCharacteristic(
    WEEK_STATS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pWeekStatsCharacteristic->addDescriptor(new BLE2902());
  delay(200);

  pService->start();
  delay(500);

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  delay(200);
  
  BLEDevice::startAdvertising();
  delay(500);
  
  Serial.println("🔵 Serveur BLE v4.0 prêt avec statistiques 7 jours!");
  Serial.println("📡 En attente de connexion...\n");
  
  printCurrentSettings();
}

void loop() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastDebug = 0;
  static unsigned long lastWeekStatsUpdate = 0;
  unsigned long currentMillis = millis();
  
  // Vérifier la réinitialisation journalière
  checkDailyReset();
  
  // CORRECTION: Mise à jour forcée des notifications si nécessaire
  if (deviceConnected && servicesReady) {
    if (forceNotifyRelay1) {
      updateRelayTimesCharacteristic();
      forceNotifyRelay1 = false;
    }
    if (forceNotifyRelay2) {
      updateRelayTimesCharacteristic();
      forceNotifyRelay2 = false;
    }
    if (forceNotifyStats) {
      updateStatsCharacteristic();
      forceNotifyStats = false;
    }
    if (forceNotifyWeekStats) {
      updateWeekStatsCharacteristic();
      forceNotifyWeekStats = false;
    }
  }
  
  // Mise à jour des notifications BLE régulières
  if (deviceConnected && servicesReady && currentMillis - lastUpdate >= 5000) {
    updateTimeCharacteristic();
    updateRelayTimesCharacteristic();
    updateStatsCharacteristic();
    lastUpdate = currentMillis;
  }
  
  // Mise à jour des statistiques de la semaine (moins fréquent)
  if (deviceConnected && servicesReady && currentMillis - lastWeekStatsUpdate >= 10000) {
    updateWeekStatsCharacteristic();
    lastWeekStatsUpdate = currentMillis;
  }
  
  // Debug périodique
  if (currentMillis - lastDebug >= 120000) { // Toutes les 2 minutes
    printCurrentSettings();
    lastDebug = currentMillis;
  }

  // Logique automatique des relais avec heure française
  if (isTimeSynchronized()) {
    int currentHour, currentMinute, currentSecond;
    getCurrentTime(currentHour, currentMinute, currentSecond);
    unsigned long currentTimeInSeconds = timeToSeconds(currentHour, currentMinute, currentSecond);

    // Distribution de grain automatique (durée fixe)
    if (!manualOverride1) {
      unsigned long startTimeInSeconds = timeToSeconds(startHour1, startMinute1, startSecond1);
      bool shouldBeOn = isTimeInRange(currentTimeInSeconds, startTimeInSeconds, durationSeconds1);
      
      if (shouldBeOn != relay1State) {
        controlRelay(1, shouldBeOn, true);
        Serial.printf("🔄 Distribution automatique: %s à %02d:%02d:%02d\n", 
          shouldBeOn ? "DÉMARRÉE" : "TERMINÉE", currentHour, currentMinute, currentSecond);
      }
    }

    // Ouverture porte automatique (plage horaire)
    if (!manualOverride2) {
      unsigned long startTimeInSeconds = timeToSeconds(startHour2, startMinute2, startSecond2);
      unsigned long stopTimeInSeconds = timeToSeconds(stopHour2, stopMinute2, stopSecond2);
      bool shouldBeOn = isTimeInRangeStartStop(currentTimeInSeconds, startTimeInSeconds, stopTimeInSeconds);
      
      if (shouldBeOn != relay2State) {
        controlRelay(2, shouldBeOn, true);
        Serial.printf("🔄 Porte automatique: %s à %02d:%02d:%02d\n", 
          shouldBeOn ? "OUVERTE" : "FERMÉE", currentHour, currentMinute, currentSecond);
      }
    }
  }

  delay(1000);
}