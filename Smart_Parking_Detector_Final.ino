#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Servo
Servo servoIn;
Servo servoOut;
const int servoInPin  = 4;
const int servoOutPin = 7;

// Sensor gate
const int IR1 = 2; // depan gate masuk
const int IR2 = 3; // belakang gate masuk
const int IR3 = 5; // depan gate keluar
const int IR4 = 6; // belakang gate keluar

// Sensor slot SL1-SL5
const int slotPins[5] = {8, 9, 10, 11, 12};
bool slotStatus[5];

// Debounce
const unsigned long DEBOUNCE_MS = 50;
unsigned long lastIR1Change = 0, lastIR2Change = 0, lastIR3Change = 0, lastIR4Change = 0;

// Previous states (untuk edge detection)
int prevIR1 = HIGH, prevIR2 = HIGH, prevIR3 = HIGH, prevIR4 = HIGH;

// hitung slot
int totalSlots = 0; // jumlah slot tersisa
const int maxSlots = 5; //jumlah maks slot

// menghindari penulisan ulang lcd
String lastLine[2] = {"", ""};

// Sequence/State untuk IR1 (masuk)
bool seqActive = false;
unsigned long seqStart = 0;
const unsigned long SEQ_STEP_MS = 1500; // tiap langkah 1,5 sec
int lastSeqStep = -1; // -1 = tidak ada, 0 = default, 1 = show empty

// Status gate
bool gateInOpen = false;
bool gateOutOpen = false;

// menulis baris LCD hanya kalau berubah
void writeLineIfChanged(uint8_t row, const String &text) {
  if (text != lastLine[row]) {
    lcd.setCursor(0, row);
    lcd.print(text);
    // pad sisa kolom supaya tidak terlihat sisa karakter lama
    int pad = 16 - text.length();
    for (int i = 0; i < pad; i++) lcd.print(' ');
    lastLine[row] = text;
  }
}

// tampilan default lcd atau pesan penuh
void displayDefault() {
  if (totalSlots == 0) {
    writeLineIfChanged(0, "Maaf!");
    writeLineIfChanged(1, "Parkir Penuh:(");
  } else {
    writeLineIfChanged(0, "SELAMAT DATANG!");
    writeLineIfChanged(1, "Sisa Slot: " + String(totalSlots));
  }
}

// Menampilkan nomor slot kosong (SL1..SL5)
void displayEmptySlotsOnce() {
  writeLineIfChanged(0, "Slot Kosong:");
  String s = "";
  for (int i = 0; i < 5; i++) {
    if (slotStatus[i]) {
      if (s.length() > 0) s += " ";
      s += String(i + 1);
    }
  }
  if (s == "") s = "-"; // tidak ada yang kosong
  if (s.length() > 16) s = s.substring(0, 16);
  writeLineIfChanged(1, s);
}

// Fungsi buka/tutup gate masuk
void openGateIn() {
  servoIn.write(0); // buka
  gateInOpen = true;
  Serial.println("[GATE IN] OPEN");
}
void closeGateIn() {
  servoIn.write(100); // tutup
  gateInOpen = false;
  Serial.println("[GATE IN] CLOSE");
}

// Fungsi buka/tutup gate keluar
void openGateOut() {
  servoOut.write(0); // buka
  gateOutOpen = true;
  Serial.println("[GATE OUT] OPEN");
}
void closeGateOut() {
  servoOut.write(100); // tutup
  gateOutOpen = false;
  Serial.println("[GATE OUT] CLOSE");
}

// Baca sensor slot
void readSlotSensors() {
  for (int i = 0; i < 5; i++) {
    int v = digitalRead(slotPins[i]);
    // HIGH = kosong, LOW = ada mobil
    slotStatus[i] = (v == HIGH); 
  }
}

void setup() {
  Serial.begin(9600);

  // LCD
  lcd.init();
  lcd.backlight();

  // pin mode
  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  pinMode(IR3, INPUT);
  pinMode(IR4, INPUT);
  for (int i = 0; i < 5; i++) pinMode(slotPins[i], INPUT_PULLUP);

  // attach servo
  servoIn.attach(servoInPin);
  servoOut.attach(servoOutPin);
  closeGateIn();
  closeGateOut();

  // jumlah slot penuh
  totalSlots = maxSlots;

  // tampil pesan awal
  lcd.clear();
  displayDefault();
  delay(1000);
}

void loop() {
  unsigned long now = millis();

  // baca sensor IR
  int ir1 = digitalRead(IR1);
  int ir2 = digitalRead(IR2);
  int ir3 = digitalRead(IR3);
  int ir4 = digitalRead(IR4);

  // baca sensor slot (untuk info LCD)
  readSlotSensors();

  // --- EDGE DETECTION IR1 (masuk) ---
  if (ir1 != prevIR1 && (now - lastIR1Change) > DEBOUNCE_MS) {
    if (prevIR1 == HIGH && ir1 == LOW) {
      if (!seqActive && !gateInOpen && totalSlots > 0) {
        seqActive = true;
        seqStart = now;
        lastSeqStep = -1;
        Serial.println("[IR1] trigger - start sequence");
      }
    }
    lastIR1Change = now;
    prevIR1 = ir1;
  }

  // --- Sequence handling ---
  if (seqActive) {
    unsigned long elapsed = now - seqStart;
    int step = 0;
    if (elapsed < SEQ_STEP_MS) step = 0;           // 0..1,5s -> default
    else if (elapsed < 2 * SEQ_STEP_MS) step = 1;  // 1,5..3s -> show empty
    else step = 2;                                 // selesai

    if (step != lastSeqStep) {
      if (step == 0) {
        displayDefault();
      } else if (step == 1) {
        displayEmptySlotsOnce();
      } else if (step == 2) {
        seqActive = false;
        displayDefault();
        openGateIn();
      }
      lastSeqStep = step;
    }
  } else {
    displayDefault();
  }

  // --- EDGE DETECTION IR2 (masuk selesai) ---
  if (ir2 != prevIR2 && (now - lastIR2Change) > DEBOUNCE_MS) {
    if (prevIR2 == HIGH && ir2 == LOW) {
      if (gateInOpen) {
        closeGateIn();
        if (totalSlots > 0) totalSlots--;  // kurangi slot
        Serial.print("[IR2] vehicle entered. totalSlots = ");
        Serial.println(totalSlots);
      }
    }
    lastIR2Change = now;
    prevIR2 = ir2;
  }

  // --- GATE KELUAR (IR3 / IR4) ---
  if (ir3 != prevIR3 && (now - lastIR3Change) > DEBOUNCE_MS) {
    if (prevIR3 == HIGH && ir3 == LOW) {
      if (!gateOutOpen) openGateOut();
    }
    lastIR3Change = now;
    prevIR3 = ir3;
  }

  if (ir4 != prevIR4 && (now - lastIR4Change) > DEBOUNCE_MS) {
    if (prevIR4 == HIGH && ir4 == LOW) {
      if (gateOutOpen) {
        closeGateOut();
        if (totalSlots < maxSlots) totalSlots++;  // tambah slot
        Serial.print("[IR4] vehicle exit. totalSlots = ");
        Serial.println(totalSlots);
      }
    }
    lastIR4Change = now;
    prevIR4 = ir4;
  }

  delay(10);
}
