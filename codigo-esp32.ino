#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// ----- OLED -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- Pines -----
#define PIN_RGB 5
#define LEDS_RGB 6
#define PIN_IND 4
#define LEDS_IND 2
#define PIN_DHT 14
#define TIPO_DHT DHT22
#define PIN_RELE 13
#define PIN_BTN 12
#define PIN_LUZ 27

// ----- Variables de estado -----
bool hayLuz = false;
bool rgbEncendido = false;
bool permisoRGB = false;

// ----- WiFi & MQTT -----
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_CLAVE = "";
const char* MQTT_SERVIDOR = "broker.hivemq.com";
const int MQTT_PUERTO = 1883;

// ----- Objetos -----
WiFiClient wifi;
PubSubClient mqtt(wifi);
Adafruit_NeoPixel rgb(LEDS_RGB, PIN_RGB, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ind(LEDS_IND, PIN_IND, NEO_GRB + NEO_KHZ800);
DHT dht(PIN_DHT, TIPO_DHT);

// ----- Variables temporales -----
unsigned long ultLectura = 0;
const unsigned long intervalo = 15000;

const float HUM_BAJA = 40.0;
const float HUM_ALTA = 60.0;

bool releOn = false;
bool btnAnt = HIGH;
float hum = 0.0;
float temp = 0.0;

void conectarWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_CLAVE);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Conectado");
}

void actualizarDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.printf("Temp: %.1f C", temp);

  display.setCursor(0, 12);
  display.printf("Hum:  %.1f %%", hum);

  display.setCursor(0, 24);
  display.printf("Rele: %s", releOn ? "Encendido" : "Apagado");

  display.setCursor(0, 36);
  display.printf("Luz:  %s", hayLuz ? "Hay luz" : "Sin luz");

  display.display();
}

void controlarRele(bool encender) {
  if (encender != releOn) {
    digitalWrite(PIN_RELE, encender ? HIGH : LOW);
    releOn = encender;
    const char* estado = encender ? "encendido" : "apagado";
    Serial.printf("Rele: %s\n", estado);
    mqtt.publish("/juan/control/cuarto/humidificador", estado, true);
    actualizarDisplay();
  }
}

void controlarRGB(bool encender) {
  if (encender) {
    if (!hayLuz && !rgbEncendido) {
      for (int i = 0; i < LEDS_RGB; i++) {
        rgb.setPixelColor(i, rgb.Color(49, 240, 247));
      }
      rgb.show();
      rgbEncendido = true;
      permisoRGB = true;
      Serial.println("RGB ON (auto)");
    } else {
      Serial.println("RGB no encendido: hay luz o ya esta encendido");
    }
  } else {
    for (int i = 0; i < LEDS_RGB; i++) {
      rgb.setPixelColor(i, rgb.Color(0, 0, 0));
    }
    rgb.show();
    rgbEncendido = false;
    permisoRGB = false;
    Serial.println("RGB OFF");
  }
}

void actualizarIndicadores() {
  if (temp < 16.0) {
    ind.setPixelColor(0, ind.Color(49, 238, 247));
  } else if (temp <= 22.0) {
    ind.setPixelColor(0, ind.Color(49, 247, 49));
  } else {
    ind.setPixelColor(0, ind.Color(247, 168, 49));
  }

  if (hum < HUM_BAJA) {
    ind.setPixelColor(1, ind.Color(247, 164, 49));
  } else if (hum > HUM_ALTA) {
    ind.setPixelColor(1, ind.Color(49, 122, 247));
  } else {
    ind.setPixelColor(1, ind.Color(49, 247, 49));
  }

  ind.show();
}

void reconectarMQTT() {
  while (!mqtt.connected()) {
    if (mqtt.connect("ESP32ClientRGB")) {
      Serial.println("MQTT conectado");
      mqtt.subscribe("/juan/control/cuarto/rgb");
      mqtt.subscribe("/juan/control/cuarto/humidificador/control");
      mqtt.subscribe("/juan/control/cuarto/rgb/control");
    } else {
      Serial.print("Fallo MQTT: ");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

void recibirMQTT(char* topico, byte* datos, unsigned int len) {
  String msg = "";
  for (int i = 0; i < len; i++) msg += (char)datos[i];

  Serial.printf("Topic %s: %s\n", topico, msg.c_str());

  if (String(topico) == "/juan/control/cuarto/rgb") {
    int r = -1, g = -1, b = -1;

    if (msg.indexOf('{') != -1) {
      // Modo JSON
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, msg);
      if (!error) {
        r = doc["r"];
        g = doc["g"];
        b = doc["b"];
      } else {
        Serial.print("Error JSON: ");
        Serial.println(error.f_str());
        return;
      }
    } else {
      // Modo texto: "R,G,B"
      int primero = msg.indexOf(',');
      int segundo = msg.indexOf(',', primero + 1);
      if (primero != -1 && segundo != -1) {
        r = msg.substring(0, primero).toInt();
        g = msg.substring(primero + 1, segundo).toInt();
        b = msg.substring(segundo + 1).toInt();
      } else {
        Serial.println("Formato RGB invalido (esperado: R,G,B)");
        return;
      }
    }

    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
      if (!hayLuz && permisoRGB) {
        for (int i = 0; i < LEDS_RGB; i++) {
          rgb.setPixelColor(i, rgb.Color(r, g, b));
        }
        rgb.show();
        Serial.printf("RGB actualizado a: %d, %d, %d\n", r, g, b);
      } else {
        Serial.println("RGB ignorado: hay luz o RGB no activado");
      }
    } else {
      Serial.println("Valores RGB fuera de rango");
    }
  }

  if (String(topico) == "/juan/control/cuarto/rgb/control") {
    if (msg == "encender") controlarRGB(true);
    else if (msg == "apagar") controlarRGB(false);
  }
}

void setup() {
  Serial.begin(115200);
  rgb.begin(); rgb.show();
  ind.begin(); ind.show();
  dht.begin();

  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, LOW);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LUZ, INPUT_PULLUP);

  conectarWiFi();
  mqtt.setServer(MQTT_SERVIDOR, MQTT_PUERTO);
  mqtt.setCallback(recibirMQTT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Error OLED");
    while (true);
  }
  display.clearDisplay();
  display.display();
  actualizarDisplay();
}

void loop() {
  if (!mqtt.connected()) reconectarMQTT();
  mqtt.loop();

  if (millis() - ultLectura >= intervalo) {
    ultLectura = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("Error DHT");
      return;
    }

    temp = t;
    hum = h;

    Serial.printf("Temp: %.2f °C, Hum: %.2f %%\n", temp, hum);
    mqtt.publish("/juan/control/cuarto/sensor/dht22/temperatura", String(temp).c_str(), true);
    mqtt.publish("/juan/control/cuarto/sensor/dht22/humedad", String(hum).c_str(), true);

    hayLuz = digitalRead(PIN_LUZ) == LOW;
    Serial.println(hayLuz ? "Hay Luz" : "Sin Luz");
    mqtt.publish("/juan/control/cuarto/sensor/luz/estado", hayLuz ? "hay_luz" : "sin_luz", true);
    actualizarDisplay();

    if (hayLuz && rgbEncendido) controlarRGB(false);

    if (hum < HUM_BAJA) {
      controlarRele(true);
    } else if (hum >= HUM_ALTA) {
      controlarRele(false);
    }

    actualizarIndicadores();
  }

  bool btn = digitalRead(PIN_BTN);
  if (btnAnt == HIGH && btn == LOW) {
    delay(30);
    if (digitalRead(PIN_BTN) == LOW) {
      if (hum >= HUM_BAJA && hum < HUM_ALTA) {
        controlarRele(!releOn);
      } else {
        Serial.println("Ignorado: humedad fuera de rango manual.");
      }
    }
  }
  btnAnt = btn;
}
