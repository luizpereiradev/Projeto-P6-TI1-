/*
  SMN-Q - Detector de Queda para Idosos (versao completa)
  Hardware: ESP32-C3 Super Mini + MPU6050 (I2C) + LCD 16x2 I2C
  Deteccao por fases (queda livre -> impacto) + Notificacao via Bluetooth BLE

  Ligacoes:
    MPU6050  VCC->3.3V GND->GND SDA->GPIO8 SCL->GPIO9
    LCD I2C  VCC->3.3V GND->GND SDA->GPIO8 SCL->GPIO9
    Potenciometro: meio->GPIO4 | pontas->3.3V e GND (seletor de modo)
    LED RGB: R->GPIO3 G->GPIO5 B->GPIO6 (com resistor) | comum->GND
    Buzzer: +->GPIO10 | -->GND
    Botao:  ->GPIO2 e GND

  Bluetooth: procure "SMN-Q Detector" no app nRF Connect e ative as
  notificacoes na caracteristica UART. As mensagens de alerta chegam ali.

  Serial (115200): q = simular queda | t = teste dos componentes
*/

#include <Wire.h>
#include <MPU6050_light.h>
#include <LiquidCrystal_I2C.h>

// --- Bluetooth BLE ---
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *pCharacteristic;
bool bleConectado = false;

// --- objetos ---
MPU6050 mpu(Wire);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- pinos ---
const int potPin=4, botaoPin=2, buzzerPin=10;
const int ledR=3, ledG=5, ledB=6;
const int PINO_SDA=8, PINO_SCL=9;

bool emAlerta=false;
const int tempoCancelar=15;
const unsigned long JANELA_IMPACTO=1200;   // tempo entre queda livre e impacto (ms)

// avisa quando um celular (cuidador) conecta ou desconecta
class MinhasCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* s){ bleConectado=true; Serial.println("Cuidador conectado via BLE!"); }
  void onDisconnect(BLEServer* s){ bleConectado=false; Serial.println("Cuidador desconectou"); s->getAdvertising()->start(); }
};

// envia uma mensagem para o celular do cuidador (se estiver conectado)
void enviarBLE(String msg){
  if(bleConectado){
    pCharacteristic->setValue(msg.c_str());
    pCharacteristic->notify();
    Serial.println("[BLE enviado] " + msg);
  } else {
    Serial.println("[BLE] Nenhum cuidador conectado - " + msg);
  }
}

void setup(){
  Serial.begin(115200); delay(300);
  pinMode(botaoPin, INPUT_PULLUP); pinMode(buzzerPin, OUTPUT);
  pinMode(ledR,OUTPUT); pinMode(ledG,OUTPUT); pinMode(ledB,OUTPUT);
  analogReadResolution(12);
  Wire.begin(PINO_SDA, PINO_SCL);

  lcd.init(); lcd.backlight(); lcd.clear();
  lcd.print("Sistema queda"); lcd.setCursor(0,1); lcd.print("ligando...");
  corLed(0,255,0);

  // --- inicia o Bluetooth BLE ---
  BLEDevice::init("SMN-Q Detector");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MinhasCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("BLE ativo: procure 'SMN-Q Detector' no celular.");

  // --- inicia o sensor ---
  byte status=mpu.begin();
  Serial.print("Status do MPU6050: "); Serial.println(status);

  lcd.clear(); lcd.print("Ajustando..."); lcd.setCursor(0,1); lcd.print("nao mexa");
  delay(1000);
  mpu.calcOffsets();

  Serial.println("Sistema iniciado. q=simular queda  t=teste");
  delay(500);

  lcd.clear(); lcd.print("Sistema OK"); lcd.setCursor(0,1); lcd.print("Monitorando");
}

void loop(){
  lerSerial();
  if(clicouBotao() && !emAlerta) emergencia("BOTAO PANICO");
  if(!emAlerta) detectarQueda();
  delay(50);
}

// magnitude CRUA (sem filtro), usada na deteccao do IMPACTO:
// o pico da colisao dura 1 ou 2 amostras e o filtro o achataria
float lerMag(){
  mpu.update();
  float x=mpu.getAccX(), y=mpu.getAccY(), z=mpu.getAccZ();
  float mag=sqrt(x*x+y*y+z*z);
  static float ultMag=999;
  if(fabs(mag-ultMag)>0.05){
    Serial.print("MAG:"); Serial.println(mag,2);
    ultMag=mag;
  }
  return mag;
}

// FILTRO DE MEDIA MOVEL (janela de 3 amostras = 150 ms a 50 ms por leitura)
// Suaviza o ruido do sensor. Usado na queda livre, que dura varias amostras,
// e na exibicao do LCD. Comeca preenchido com 1.0g (repouso) para que a media
// nao parta de zero e dispare uma falsa queda livre logo apos o boot.
// A janela e curta de proposito: com 5 amostras seriam necessarios 150 ms de
// queda livre (~11 cm) antes da media cruzar 0,60g; com 3 bastam 100 ms (~5 cm).
const int N_AMOSTRAS=3;
float janela[N_AMOSTRAS]={1.0,1.0,1.0};
int idxJanela=0;

float filtrarMedia(float mag){
  janela[idxJanela]=mag;
  idxJanela=(idxJanela+1)%N_AMOSTRAS;
  float soma=0;
  for(int i=0;i<N_AMOSTRAS;i++) soma+=janela[i];
  return soma/N_AMOSTRAS;
}

// POTENCIOMETRO = seletor de modo (REAL = literatura / DEMO = facil de testar)
String nivelSens(){ int p=analogRead(potPin); return (p<2048)?"REAL":"DEMO"; }
float limiteQuedaLivre(){ int p=analogRead(potPin); return (p<2048)?0.60:0.70; }
float limiteImpacto(){    int p=analogRead(potPin); return (p<2048)?2.50:1.15; }

// DETECCAO POR FASES: queda livre (mag baixa) seguida de impacto (mag alta)
void detectarQueda(){
  float magCrua=lerMag();                // valor instantaneo -> deteccao do impacto
  float magFiltrada=filtrarMedia(magCrua); // media movel -> queda livre e LCD
  float limQueda=limiteQuedaLivre();
  float limImpacto=limiteImpacto();
  String modo=nivelSens();

  lcd.setCursor(0,0); lcd.print("Modo:"); lcd.print(modo); lcd.print("     ");
  lcd.setCursor(0,1); lcd.print("MAG:"); lcd.print(magFiltrada,2); lcd.print("       ");

  static int fase=0;                     // 0=monitorando, 1=aguardando impacto
  static unsigned long tempoQuedaLivre=0;

  if(fase==0){
    if(magFiltrada < limQueda){          // Fase 1: queda livre (vale longo, filtrado)
      fase=1;
      tempoQuedaLivre=millis();
      corLed(0,0,255);
      Serial.print("[FASE 1] Queda livre! MAG="); Serial.println(magFiltrada,2);
    }
  } else {
    if(magCrua > limImpacto){            // Fase 2: impacto (pico curto, sem filtro)
      Serial.print("[FASE 2] Impacto! MAG="); Serial.println(magCrua,2);
      fase=0;
      possivelQueda();
    }
    else if(millis()-tempoQuedaLivre > JANELA_IMPACTO){
      fase=0;                            // sem impacto na janela: falso alarme
      corLed(0,255,0);
      Serial.println("[RESET] Sem impacto na janela.");
    }
  }
}

void possivelQueda(){
  emAlerta=true; corLed(255,180,0); tone(buzzerPin,1000,300);
  lcd.clear(); lcd.print("Possivel queda");
  Serial.println("Possivel queda detectada. Aperte o botao para cancelar.");

  // avisa o cuidador que ALGO foi detectado (ainda em periodo de cancelamento)
  enviarBLE("ALERTA: possivel queda detectada. Verificando...");

  for(int i=tempoCancelar;i>0;i--){
    lcd.setCursor(0,1); lcd.print("Cancelar: "); lcd.print(i); lcd.print("s   ");
    tone(buzzerPin,800,150);
    unsigned long inicio=millis();
    while(millis()-inicio<1000){ lerSerial(); if(clicouBotao()){cancelar();return;} }
  }
  emergencia("QUEDA DETECTADA");
}

void cancelar(){
  emAlerta=false; noTone(buzzerPin); corLed(0,255,0);
  lcd.clear(); lcd.print("Cancelado"); lcd.setCursor(0,1); lcd.print("Sistema OK");
  Serial.println("Alarme cancelado");
  enviarBLE("Alarme cancelado pelo usuario. Tudo bem.");
  delay(2000);
  lcd.clear(); lcd.print("Sistema OK"); lcd.setCursor(0,1); lcd.print("Monitorando");
}

void emergencia(String motivo){
  emAlerta=true; corLed(255,0,0);
  lcd.clear(); lcd.print("EMERGENCIA!"); lcd.setCursor(0,1); lcd.print(motivo);
  Serial.println("== ALERTA ENVIADO AO CUIDADOR =="); Serial.println(motivo);

  // NOTIFICACAO PRINCIPAL AO CUIDADOR VIA BLUETOOTH
  enviarBLE("EMERGENCIA! " + motivo + ". Socorro necessario!");

  for(int i=0;i<10;i++){ tone(buzzerPin,1200); delay(250); noTone(buzzerPin); delay(250); }
  lcd.clear(); lcd.print("Alerta enviado"); lcd.setCursor(0,1); lcd.print("Cuidador avisado");
  delay(3000);
  emAlerta=false; corLed(0,255,0);
  lcd.clear(); lcd.print("Sistema OK"); lcd.setCursor(0,1); lcd.print("Monitorando");
}

void teste(){
  emAlerta=true;
  lcd.clear(); lcd.print("LED verde");   corLed(0,255,0);   delay(800);
  lcd.clear(); lcd.print("LED amarelo"); corLed(255,180,0); delay(800);
  lcd.clear(); lcd.print("LED vermelho");corLed(255,0,0);   delay(800);
  lcd.clear(); lcd.print("LED azul");    corLed(0,0,255);   delay(800);
  lcd.clear(); lcd.print("Buzzer"); tone(buzzerPin,1000,500); delay(800);
  float mag=lerMag(); int pot=analogRead(potPin); bool botao=digitalRead(botaoPin)==LOW;
  lcd.clear(); lcd.print("Sensores"); lcd.setCursor(0,1); lcd.print("MAG:"); lcd.print(mag,2);
  Serial.print("MAG:");Serial.print(mag,2);
  Serial.print(" Pot:");Serial.print(pot);
  Serial.print(" Botao:");Serial.println(botao?"apertado":"solto");
  enviarBLE("Teste executado. Sistema funcionando.");
  delay(2000);
  emAlerta=false; corLed(0,255,0);
  lcd.clear(); lcd.print("Sistema OK"); lcd.setCursor(0,1); lcd.print("Monitorando");
}

void lerSerial(){
  if(Serial.available()){
    char t=Serial.read();
    if(t=='q'||t=='Q'){ Serial.println("Queda simulada"); possivelQueda(); }
    if(t=='t'||t=='T') teste();
  }
}

bool clicouBotao(){
  static bool ultimo=HIGH; static unsigned long tUlt=0;
  bool atual=digitalRead(botaoPin);
  if(ultimo==HIGH && atual==LOW){
    if(millis()-tUlt>300){ tUlt=millis(); ultimo=atual; return true; }
  }
  ultimo=atual; return false;
}

void corLed(int r,int g,int b){ analogWrite(ledR,r); analogWrite(ledG,g); analogWrite(ledB,b); }
