// Poulailler ESP32 BLE v5 - version allégée + validation des distributions
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <time.h>

#define DEBUG 0   // 1 = logs série (coûte de la flash)
#if DEBUG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

#define RELAY_1_PIN 2   // grain
#define RELAY_2_PIN 4   // porte

#define SERVICE_UUID  "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CMD_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define TIME_UUID     "123e4567-e89b-12d3-a456-426614174000"
#define RELAY1_UUID   "5a786b5e-1234-5678-9abc-def012345678"
#define RELAY2_UUID   "6b897c6f-2345-6789-abcd-ef0123456789"
#define VALID_UUID    "9a8f7e6d-5c4b-4a39-8271-605f4e3d2c1b"  // NOUVEAU

// Statuts d'une distribution
enum : uint8_t { ST_NONE = 0, ST_PENDING, ST_OK, ST_INTERRUPTED, ST_MISSED };

struct Cfg {                       // 1 seul blob NVS
  uint8_t h1, m1, s1; uint16_t dur1;          // grain : début + durée (minutes)
  uint8_t h2, m2, s2, H2, M2, S2;             // porte : ouverture / fermeture
} cfg = {8, 0, 0, 30, 7, 0, 0, 21, 0, 0};

struct Rec { uint16_t day; uint8_t st; uint8_t pad; uint16_t planned; uint16_t done; };  // 8 octets
Rec logRec[7];                     // journal 7 jours (slot = jour % 7)

Preferences prefs;
BLEServer* srv;
BLECharacteristic *chTime, *chR1, *chR2, *chValid;

bool connected = false, pushAll = false, dirty = false;
unsigned long connectedAt = 0;
time_t utcBase = 0;                // heure UTC de référence
unsigned long bootMs = 0;
bool timeTrusted = false;          // true une fois l'heure resynchronisée depuis le boot
bool manual1 = false, manual2 = false, r1On = false, r2On = false;
bool recPending = false;
unsigned long runStartMs = 0;
uint16_t noMissDay = 0xFFFF;       // évite un faux "manquée" juste après (re)programmation

// ---------- Temps (fuseau France géré par la lib, DST automatique) ----------
bool isSynced() { return utcBase != 0; }

uint32_t localNow() {
  time_t t = utcBase + (millis() - bootMs) / 1000;
  struct tm lt; localtime_r(&t, &lt);
  return (uint32_t)t + (lt.tm_isdst > 0 ? 7200 : 3600);
}

// ---------- Persistance ----------
void saveCfg()  { prefs.putBytes("cfg", &cfg, sizeof(cfg)); }
void saveLog()  { prefs.putBytes("log", logRec, sizeof(logRec)); }
void saveTime() { prefs.putULong("utc", (uint32_t)(utcBase + (millis() - bootMs) / 1000)); }

// ---------- Journal des distributions ----------
#define SLOT(d) logRec[(d) % 7]   // macro (et non fonction) : évite un bug des prototypes auto de l'IDE Arduino

void startRec() {
  uint16_t day = localNow() / 86400;
  Rec& r = SLOT(day);
  r = {day, ST_PENDING, 0, cfg.dur1, 0};
  runStartMs = millis();
  recPending = true;
  saveLog();
}

void finishRec() {
  if (!recPending) return;
  recPending = false;
  uint32_t sec = (millis() - runStartMs) / 1000;
  uint16_t day = localNow() / 86400;
  Rec& r = SLOT(day);
  if (r.st != ST_PENDING) {          // passage minuit pendant la distribution
    for (auto& x : logRec) if (x.st == ST_PENDING) { r = x; break; }
  }
  r.done = (sec + 30) / 60;
  r.st = (sec + 5 >= (uint32_t)r.planned * 60) ? ST_OK : ST_INTERRUPTED;
  for (auto& x : logRec) if (&x != &r && x.st == ST_PENDING) x.st = ST_INTERRUPTED;
  saveLog();
  saveTime();
}

void checkMissed() {                 // fenêtre passée sans distribution
  if (!isSynced() || manual1) return;
  uint32_t now = localNow(), sod = now % 86400;
  uint16_t day = now / 86400;
  uint32_t end = (uint32_t)cfg.h1 * 3600 + cfg.m1 * 60 + cfg.s1 + (uint32_t)cfg.dur1 * 60;
  if (end < 86400 && sod >= end && day != noMissDay && SLOT(day).day != day) {
    SLOT(day) = {day, ST_MISSED, 0, cfg.dur1, 0};
    saveLog(); dirty = true;
  }
}

// ---------- Relais ----------
void setRelay(uint8_t n, bool on, bool autoMode) {
  if (n == 1) {
    if (on == r1On) return;
    digitalWrite(RELAY_1_PIN, on ? LOW : HIGH);
    r1On = on;
    if (on && autoMode) startRec();
    else if (!on) finishRec();
  } else {
    if (on == r2On) return;
    digitalWrite(RELAY_2_PIN, on ? LOW : HIGH);
    r2On = on;
  }
  dirty = true;
  LOG("relay %u -> %u (%s)\n", n, on, autoMode ? "auto" : "manuel");
}

bool inRange(uint32_t cur, uint32_t start, uint32_t end) {
  return start < end ? (cur >= start && cur < end) : (cur >= start || cur < end);
}

// ---------- Notifications ----------
void notifyStr(BLECharacteristic* c, const char* s) {
  c->setValue((uint8_t*)s, strlen(s));
  c->notify();
}

void pushTime() {
  char b[16];
  if (!isSynced()) strcpy(b, "Non sync");
  else { uint32_t s = localNow() % 86400; snprintf(b, sizeof(b), "%02lu:%02lu:%02lu FR", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60)); }
  notifyStr(chTime, b);
}

void pushRelays() {
  char b[64];
  snprintf(b, sizeof(b), "%02u:%02u:%02u,%lu,%s,FR,GRAIN,%u", cfg.h1, cfg.m1, cfg.s1,
           (unsigned long)cfg.dur1 * 60, r1On ? "ON" : "OFF", 0);
  notifyStr(chR1, b);
  snprintf(b, sizeof(b), "%02u:%02u:%02u,%02u:%02u:%02u,%s,FR,PORTE,%u", cfg.h2, cfg.m2, cfg.s2,
           cfg.H2, cfg.M2, cfg.S2, r2On ? "ON" : "OFF", 0);
  notifyStr(chR2, b);
}

// Format : "T<0|1>|joursAvant,statut,prévu_min,fait_min;..."   ex: T1|0,2,20,20;1,3,20,12
void pushValid() {
  char b[112];
  int n = snprintf(b, sizeof(b), "T%u|", timeTrusted ? 1 : 0);
  if (isSynced()) {
    uint16_t today = localNow() / 86400;
    for (auto& r : logRec)
      if (r.st != ST_NONE && r.day <= today && today - r.day < 7 && n < (int)sizeof(b) - 16)
        n += snprintf(b + n, sizeof(b) - n, "%u,%u,%u,%u;", today - r.day, r.st, r.planned, r.done);
  }
  notifyStr(chValid, b);
}

void pushAllNow() { pushTime(); pushRelays(); pushValid(); }

// ---------- Commandes de l'app ----------
void applyCommand(const uint8_t* v, size_t len) {
  if (len == 0) return;
  switch (v[0]) {
    case 1: manual1 = true; setRelay(1, v[1] == 1, false); break;
    case 2: manual2 = true; setRelay(2, v[1] == 1, false); break;
    case 3:
      if (len < 6) return;
      cfg.h1 = min<int>(v[1], 23); cfg.m1 = min<int>(v[2], 59); cfg.s1 = 0;
      cfg.dur1 = constrain((v[4] << 8) | v[5], 1, 1440);
      manual1 = false; noMissDay = localNow() / 86400; saveCfg(); break;
    case 4:
      if (len < 7) return;
      cfg.h2 = min<int>(v[1], 23); cfg.m2 = min<int>(v[2], 59); cfg.s2 = 0;
      cfg.H2 = min<int>(v[4], 23); cfg.M2 = min<int>(v[5], 59); cfg.S2 = 0;
      manual2 = false; saveCfg(); break;
    case 5:
      if (len < 2) return;
      if (v[1] == 1) { manual1 = false; noMissDay = localNow() / 86400; }
      if (v[1] == 2) manual2 = false;
      break;
    case 6:
      if (len < 5) return;
      utcBase = ((uint32_t)v[1] << 24) | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 8) | v[4];
      bootMs = millis(); timeTrusted = true; saveTime();
      break;
  }
  dirty = true;
}

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) { connected = true; connectedAt = millis(); pushAll = true; }
  void onDisconnect(BLEServer*) { connected = false; pushAll = false; BLEDevice::startAdvertising(); }
};

class CmdCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) { applyCommand(c->getData(), c->getLength()); }
};

BLECharacteristic* mk(BLEService* s, const char* uuid, uint32_t props) {
  BLECharacteristic* c = s->createCharacteristic(uuid, props);
  if (props & BLECharacteristic::PROPERTY_NOTIFY) c->addDescriptor(new BLE2902());
  return c;
}

void setup() {
#if DEBUG
  Serial.begin(115200);
#endif
  pinMode(RELAY_1_PIN, OUTPUT); digitalWrite(RELAY_1_PIN, HIGH);
  pinMode(RELAY_2_PIN, OUTPUT); digitalWrite(RELAY_2_PIN, HIGH);
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();

  prefs.begin("p", false);
  Cfg c; if (prefs.getBytes("cfg", &c, sizeof(c)) == sizeof(c)) cfg = c;
  if (prefs.getBytes("log", logRec, sizeof(logRec)) != sizeof(logRec)) memset(logRec, 0, sizeof(logRec));
  for (auto& r : logRec) if (r.st == ST_PENDING) r.st = ST_INTERRUPTED;  // coupure en cours de distribution
  saveLog();
  utcBase = prefs.getULong("utc", 0); bootMs = millis();   // heure approximative, à resynchroniser

  BLEDevice::init("ESP32-Relay-Controller");
  BLEDevice::setMTU(185);
  srv = BLEDevice::createServer();
  srv->setCallbacks(new ServerCb());
  BLEService* s = srv->createService(SERVICE_UUID);
  BLECharacteristic* cmd = mk(s, CMD_UUID, BLECharacteristic::PROPERTY_WRITE);
  cmd->setCallbacks(new CmdCb());
  const uint32_t RN = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY;
  chTime = mk(s, TIME_UUID, RN);
  chR1 = mk(s, RELAY1_UUID, RN);
  chR2 = mk(s, RELAY2_UUID, RN);
  chValid = mk(s, VALID_UUID, RN);
  s->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void loop() {
  static unsigned long lastPush = 0;
  unsigned long ms = millis();

  if (isSynced()) {
    uint32_t sod = localNow() % 86400;
    if (!manual1) {
      uint32_t st = (uint32_t)cfg.h1 * 3600 + cfg.m1 * 60 + cfg.s1;
      setRelay(1, inRange(sod, st, (st + (uint32_t)cfg.dur1 * 60) % 86400), true);
    }
    if (!manual2)
      setRelay(2, inRange(sod, (uint32_t)cfg.h2 * 3600 + cfg.m2 * 60 + cfg.s2,
                          (uint32_t)cfg.H2 * 3600 + cfg.M2 * 60 + cfg.S2), true);
    checkMissed();
  }

  if (connected && pushAll && ms - connectedAt > 2000) { pushAllNow(); pushAll = false; lastPush = ms; dirty = false; }
  if (connected && !pushAll) {
    if (dirty) { pushRelays(); pushValid(); dirty = false; }
    if (ms - lastPush >= 5000) { pushTime(); lastPush = ms; }
  }
  delay(500);
}
