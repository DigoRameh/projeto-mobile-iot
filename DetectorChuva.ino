// ESP32 + Stepper (Arduino Stepper.h) + 28BYJ-48 (ULN2003) + Sensor de chuva (DO)
// Digo: DO->d23, IN1..IN4->d18,d19,d21,d22, (opcional) AO->d4 p/ debug analógico
// Comportamento: MOLHADO => +360° ; SECO estável => -360°

#include <Arduino.h>
#include <Stepper.h>

// ====== PINOS ======
#define PIN_RAIN_DO 23         // d23 - digital do sensor (LOW normalmente = molhado)
#define PIN_AO      4          // d4  - analógico (opcional, ADC2) — desative Wi-Fi

#define PIN_IN1 18             // d18
#define PIN_IN2 19             // d19
#define PIN_IN3 21             // d21
#define PIN_IN4 22             // d22

// ====== SENSOR ======
#define DO_INPUT_PULLUP 1      // 1=INPUT_PULLUP; 0=INPUT
#define DO_ACTIVE_LOW   1      // 1=molhado é LOW; 0=molhado é HIGH
const unsigned long DRY_STABLE_MS = 2000;

// ====== ANALÓGICO (opcional) ======
#define ENABLE_ANALOG_DEBUG 1  // 1=imprime ADC/EMA; 0=desliga
int LIMIAR_CHUVA = 2350;       // abaixo => molhado
int LIMIAR_SECO  = 2600;       // acima  => seco
const float ALFA_EMA = 0.15f;
float ema = 0.0f;

// ====== STEPPER ======
// 28BYJ-48: normalmente 2048 passos/volta em PASSO-CHEIO.
// Se o seu for 4096 (meia-etapa), troque para 4096.
const int STEPS_PER_REV = 2048;
Stepper myStepper(STEPS_PER_REV, PIN_IN1, PIN_IN3, PIN_IN2, PIN_IN4);
//                   ^^^^^^^^^^^  ordem recomendada p/ 28BYJ-48 (IN1,IN3,IN2,IN4)
int STEP_SPEED_RPM = 12;        // ajuste velocidade (8–15 é ok)

// ====== ESTADO ======
bool wetState = false;          // estado atual "oficial"
bool spunCW   = false;          // já girou +360° após molhar?
unsigned long drySince = 0;

int lerADCmedia(uint8_t n){
  uint32_t s=0; for(uint8_t i=0;i<n;i++){ s += analogRead(PIN_AO); delay(2); } return s/n;
}
bool DO_wet(){
  int v = digitalRead(PIN_RAIN_DO);
  return DO_ACTIVE_LOW ? (v==LOW) : (v==HIGH);
}
bool analogWet(bool last){
  if (!ENABLE_ANALOG_DEBUG) return last;
  if (ema <= LIMIAR_CHUVA) return true;
  if (ema >= LIMIAR_SECO)  return false;
  return last; // zona morta -> histerese
}

void setup(){
  Serial.begin(115200);

  if (DO_INPUT_PULLUP) pinMode(PIN_RAIN_DO, INPUT_PULLUP);
  else                 pinMode(PIN_RAIN_DO, INPUT);

  if (ENABLE_ANALOG_DEBUG) {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_AO, ADC_11db);
    ema = (float) lerADCmedia(8);
  }

  myStepper.setSpeed(STEP_SPEED_RPM);

  Serial.println("\n=== ESP32 + Stepper.h + Rain DO ===");
  Serial.printf("STEPS_PER_REV=%d, RPM=%d\n", STEPS_PER_REV, STEP_SPEED_RPM);
  Serial.printf("DO_ACTIVE_LOW=%d, PULLUP=%d\n", DO_ACTIVE_LOW, DO_INPUT_PULLUP);
  if (ENABLE_ANALOG_DEBUG)
    Serial.printf("ADC TH: chuva<=%d  seco>=%d\n", LIMIAR_CHUVA, LIMIAR_SECO);
}

void loop(){
  // ---- Leitura do sensor ----
  bool wetByDO = DO_wet();
  if (ENABLE_ANALOG_DEBUG) {
    int bruto = lerADCmedia(4);
    ema = ALFA_EMA * bruto + (1.0f - ALFA_EMA) * ema;

    Serial.print("DO="); Serial.print(wetByDO ? "WET" : "DRY");
    Serial.print(" | ADC="); Serial.print(bruto);
    Serial.print(" | EMA="); Serial.print((int)ema);
    Serial.print(" | TH(chuva<="); Serial.print(LIMIAR_CHUVA);
    Serial.print(", seco>="); Serial.print(LIMIAR_SECO);
    Serial.print(") | ");

    // decisão combinada: DO tem prioridade; se não molhado pelo DO, usa analógico
    bool wetByAnalog = analogWet(wetState);
    wetState = wetByDO ? true : wetByAnalog;

    Serial.print("ESTADO="); Serial.println(wetState ? "MOLHADO" : "SECO");
  } else {
    wetState = wetByDO;
  }

  // ---- Ações no motor ----
  // Transição SECO->MOLHADO: +360°
  static bool lastWet = false;
  if (!lastWet && wetState) {
    Serial.println("SECO->MOLHADO: +360°");
    myStepper.step(STEPS_PER_REV);   // sentido + (ajuste se girar ao contrário)
    spunCW = true;
    drySince = 0;
  }

  // SECO estável: −360° (se já girou)
  if (!wetState) {
    if (drySince == 0) drySince = millis();
    if ((millis() - drySince) >= DRY_STABLE_MS && spunCW) {
      Serial.println("SECO estável: -360° (retorno)");
      myStepper.step(-STEPS_PER_REV); // sentido contrário
      spunCW = false;
    }
  } else {
    drySince = 0;
  }

  lastWet = wetState;
  delay(150);
}
