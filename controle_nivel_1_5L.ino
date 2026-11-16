#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);   //     endereco 0x27 ou 0x3F

const int ledVerde    = 2;   // enchendo a plena vazao
const int ledAmarelo  = 3;   // enchendo com vazao reduzida (zona da rampa)
const int ledAzul     = 4;   // cheio, bomba desligada
const int ledVermelho = 5;   // fixo = nivel critico
const int trigPin     = 7;
const int echoPin     = 8;
const int fanPin      = 9;   // pino do pwm

// ---------- Geometria do reservatorio (cilindro) ----------
const float DIAMETRO_CM      = 10.0;                                   // diametro interno
const float VOLUME_MAX_L     = 1.5;                                    // capacidade
const float AREA_BASE_CM2    = PI * (DIAMETRO_CM / 2.0) * (DIAMETRO_CM / 2.0);  // ~78,54 cm2
const float ALTURA_MAX_CM    = VOLUME_MAX_L * 1000.0 / AREA_BASE_CM2;  // ~19,10 cm de coluna de agua
const float ALTURA_SENSOR_CM = 25.0;    // do sensor ate o fundo (MEDIR na montagem; manter folga > 4 cm acima do nivel cheio)

const float NIVEL_CRITICO = 10.0;   // abaixo disso: LED vermelho
const float NIVEL_PLENA   = 85.0;   // ate aqui: 100%
const float NIVEL_DESLIGA = 97.0;   // acima daqui: 0%

const float DIST_CHEIO = ALTURA_SENSOR_CM - ALTURA_MAX_CM;  // reservatorio cheio (~5,9 cm)
const float DIST_VAZIO = ALTURA_SENSOR_CM;                  // reservatorio vazio
const float MARGEM_CM  = 2.0;                               // tolerancia

// PWM minimo e maximo
const int PWM_MIN = 80;
const int PWM_MAX = 255;
const unsigned long PER_SENSOR = 100;    // leitura e controle
const unsigned long PER_LCD    = 250;    // atualizacao do display
const unsigned long PER_SERIAL = 1000;   // telemetria (RF6)
const unsigned long RAMPA_MS = 2000;     // partida de 0 a 100% em 2 s
float amostras[5];        // janela do filtro de mediana
int   idxAmostra = 0;

long duracao;
float distanciaCm = DIST_VAZIO;
float volumeL     = 0.0;
float percentual  = 0.0;
int   pwmAtual    = 0;

bool  leituraValida     = false;
int   invalidasSeguidas = 0;

bool          emRampa = false;
unsigned long tRampa  = 0;
unsigned long tSensor = 0, tLcd = 0, tSerial = 0;

float medirDistancia() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duracao = pulseIn(echoPin, HIGH, 30000);

  if (duracao == 0) return -1;          // sem eco
  return duracao * 0.0343 / 2;          // 343 m/s, ida e volta
}

float medianaAmostras() {
  float c[5];
  for (int i = 0; i < 5; i++) c[i] = amostras[i];

  for (int i = 1; i < 5; i++) {         // ordenacao por insercao
    float v = c[i];
    int j = i - 1;
    while (j >= 0 && c[j] > v) { c[j + 1] = c[j]; j--; }
    c[j + 1] = v;
  }
  return c[2];
}

void atualizaNivel() {
  float d = medirDistancia();

  bool valida = (d > 0) &&
                (d >= DIST_CHEIO - MARGEM_CM) &&
                (d <= DIST_VAZIO + MARGEM_CM);

  if (!valida) {
    invalidasSeguidas++;
    if (invalidasSeguidas >= 10) leituraValida = false;   // ~1 s
    return;
  }

  invalidasSeguidas = 0;
  leituraValida = true;

  amostras[idxAmostra] = d;
  idxAmostra = (idxAmostra + 1) % 5;
  distanciaCm = medianaAmostras();

  float alturaAgua = ALTURA_SENSOR_CM - distanciaCm;
  if (alturaAgua < 0) alturaAgua = 0;

  volumeL    = alturaAgua * AREA_BASE_CM2 / 1000.0;
  if (volumeL > VOLUME_MAX_L) volumeL = VOLUME_MAX_L;
  percentual = volumeL / VOLUME_MAX_L * 100.0;
}

int calculaPWM() {
  if (!leituraValida)              return 0;
  if (percentual >= NIVEL_DESLIGA) return 0;
  if (percentual <= NIVEL_PLENA)   return PWM_MAX;

  // Zona de reducao: 85% -> 97% mapeia PWM_MAX -> PWM_MIN.
  // O fator 10 preserva uma casa decimal, ja que map() trabalha com inteiros.
  return (int) map((long)(percentual * 10),
                   (long)(NIVEL_PLENA * 10), (long)(NIVEL_DESLIGA * 10),
                   PWM_MAX, PWM_MIN);
}

int aplicaRampa(int alvo) {
  if (alvo == 0) { emRampa = false; return 0; }

  if (pwmAtual == 0 && !emRampa) {      // saindo do repouso: inicia a rampa
    emRampa = true;
    tRampa  = millis();
  }

  if (emRampa) {
    unsigned long dt = millis() - tRampa;
    if (dt >= RAMPA_MS) { emRampa = false; return alvo; }

    int limite = (int)((long)PWM_MAX * dt / RAMPA_MS);
    return (limite < alvo) ? limite : alvo;
  }
  return alvo;
}

void atualizaLeds() {
  digitalWrite(ledVerde,    LOW);
  digitalWrite(ledAmarelo,  LOW);
  digitalWrite(ledAzul,     LOW);
  digitalWrite(ledVermelho, LOW);

  if (!leituraValida) {                              // vermelho piscando a 2 Hz
    digitalWrite(ledVermelho, (millis() / 250) % 2);
    return;
  }

  if      (percentual <  NIVEL_CRITICO) digitalWrite(ledVermelho, HIGH);
  else if (percentual <  NIVEL_PLENA)   digitalWrite(ledVerde,    HIGH);
  else if (percentual <  NIVEL_DESLIGA) digitalWrite(ledAmarelo,  HIGH);
  else                                  digitalWrite(ledAzul,     HIGH);
}

void atualizaLcd() {
  if (!leituraValida) {
    lcd.setCursor(0, 0); lcd.print("SENSOR INVALIDO ");
    lcd.setCursor(0, 1); lcd.print("Bomba desligada ");
    return;
  }

  char l1[17], l2[17], barra[6];

  int volMl = (int)(volumeL * 1000.0 + 0.5);          // volume em mL (0 a 1500)
  int pct   = (int)(percentual + 0.5);
  int duty  = (int)((long)pwmAtual * 100 / 255);

  sprintf(l1, "V:%4dmL   %3d%% ", volMl, pct);

  int cheios = duty * 5 / 100;
  for (int i = 0; i < 5; i++) barra[i] = (i < cheios) ? '#' : '-';
  barra[5] = '\0';
  sprintf(l2, "Bomba:%3d%% %s", duty, barra);

  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
}

void enviaTelemetria() {
  Serial.print("Dist: ");        Serial.print(distanciaCm, 1);
  Serial.print(" cm | Vol: ");   Serial.print(volumeL, 3);
  Serial.print(" L | Nivel: ");  Serial.print(percentual, 0);
  Serial.print(" % | PWM: ");    Serial.print(pwmAtual);
  Serial.print(" | Duty: ");     Serial.print(pwmAtual * 100.0 / 255.0, 0);
  Serial.print(" % | ");         Serial.println(leituraValida ? "OK" : "SENSOR INVALIDO");
}

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(fanPin,  OUTPUT);

  pinMode(ledVerde,    OUTPUT);
  pinMode(ledAmarelo,  OUTPUT);
  pinMode(ledAzul,     OUTPUT);
  pinMode(ledVermelho, OUTPUT);

  lcd.begin(16, 2);
  // lcd.backlight();            // descomente apenas com LiquidCrystal_I2C

  Serial.begin(9600);

  for (int i = 0; i < 5; i++) amostras[i] = DIST_VAZIO;
  analogWrite(fanPin, 0);        // garante a bomba parada na energizacao
}

void loop() {
  unsigned long agora = millis();

  if (agora - tSensor >= PER_SENSOR) {
    tSensor = agora;
    atualizaNivel();                          // le, valida, filtra, converte
    pwmAtual = aplicaRampa(calculaPWM());     // lei de controle + rampa
    analogWrite(fanPin, pwmAtual);
  }

  atualizaLeds();                             // todo laco, para o pisca-pisca

  if (agora - tLcd >= PER_LCD) {
    tLcd = agora;
    atualizaLcd();
  }

  if (agora - tSerial >= PER_SERIAL) {
    tSerial = agora;
    enviaTelemetria();
  }
}
