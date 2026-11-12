const int trigger = 9;
const int echoPin = 10;
const int fanPin  = 6;   // pino PWM

// Faixa de distância mapeada para a velocidade (cm)
const float DIST_MIN = 5.0;    // abaixo disso: ventoinha desligada
const float DIST_MAX = 100.0;  // acima disso: velocidade máxima

// PWM mínimo e maximo
const int PWM_MIN = 80;
const int PWM_MAX = 255;

long duracao;
float distanciaCm;

float medirDistancia() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duracao = pulseIn(echoPin, HIGH, 30000);

  if (duracao == 0) return -1;          // sem eco
  return duracao * 0.0343 / 2;
}

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(fanPin, OUTPUT);
  Serial.begin(9600);
}

void loop() {
  distanciaCm = medirDistancia();

  // Sem eco = nada por perto dentro do alcance, trata como distância máxima
  if (distanciaCm < 0) distanciaCm = DIST_MAX;

  int pwm;
  if (distanciaCm <= DIST_MIN) {
    pwm = 0;  // desliga quando a o nivel esta alto
  } else {
    float d = constrain(distanciaCm, DIST_MIN, DIST_MAX);
    pwm = map((long)(d * 10), (long)(DIST_MIN * 10), (long)(DIST_MAX * 10),
              PWM_MIN, PWM_MAX);
  }

  analogWrite(fanPin, pwm);

  Serial.print("Distancia: ");
  Serial.print(distanciaCm, 1);
  Serial.print(" cm | PWM: ");
  Serial.print(pwm);
  Serial.print(" | Duty: ");
  Serial.print(pwm * 100.0 / 255.0, 0);
  Serial.println(" %");

  delay(100);
}