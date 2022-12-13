#include <Adafruit_PCF8574.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <avr/eeprom.h>

#define stpPin1 1 // Контакт управляющего сигнала STEP для драйвера ШД1
#define dirPin1 1 // Контакт управляющего сигнала DIR для драйвера ШД1
#define stpPin2 1 // Контакт управляющего сигнала STEP для драйвера ШД2
#define dirPin2 1 // Контакт управляющего сигнала DIR для драйвера ШД2

#define xEnd 1 // Контакт сигнала стоп по направлению X->0
#define yEnd 1 // Контакт сигнала стоп по направлению Y->0

#define mg 1 // Контакт управляющих импульсов для сервопривода подъёмного механизма


Adafruit_PCF8574 brdX; // I2C расширитель портов PCF8574 для подключения столбцов отслеживающей матрицы
Adafruit_PCF8574 brdY; // I2C расширитель портов PCF8574 для подключения строк отслеживающей матрицы

Servo mgServ; // Сервопривод подъёмного механизма

const EEMEM char ssidAddr[33]; // Адрес в энергонезависимой памяти строки с SSID точки доступа WiFi
const EEMEM paswdAddr[33]; // Адрес в энергонезависимой памяти строки с паролем для точки доступа WiFi
const EEMEM char hostAddr[33]; // Адрес в энергонезависимой памяти строки с ip сервера
const EEMEM char portAddr[33]; // Адрес в энергонезависимой памяти строки с портом сервера

char ssid[33]; // Строка с SSID точки доступа WiFi
char paswd[33]; // Строка с паролем от точки доступа WiFi
char host[33]; // Строка с ip сервера
char port[33]; // Строка с портом сервера

TaskHandle_t serListenerTask; // Объект задачи отслушивания последовательного порта
TaskHandle_t tcpListenerTask; // Объект задачи отслушивания TCP подключения
TaskHandle_t brdListenerTask; // Объект задачи отслушивания матрицы датчиков

WiFiClient _tcp;

void setup() {
  // Инициализация последовательного порта
  Serial.begin(115200);
  Serial.println("[setup()] Log: Serial initialization completed!");

  // Инициализация шаговых двигателей
  Serial.println("[setup()] Log: Stepper motor initialization...");
  pinMode(stpPin1, OUTPUT); // Установка контакта управляющего сигнала STEP ШД1 в режим вывода
  pinMode(dirPin1, OUTPUT); // Установка контакта управляющего сигнала DIR ШД1 в режим вывода
  
  pinMode(stpPin2, OUTPUT); // Установка контакта управляющего сигнала STEP ШД2 в режим вывода
  pinMode(dirPin2, OUTPUT); // Установка контакта управляющего сигнала DIR ШД2 в режим вывода

  pinMode(xEnd, INPUT_PULLUP); // Установка контакта сигнала стоп по направлению X->0 в режим ввода с использованием встроенного подтягивающего резистора
  pinMode(yEnd, INPUT_PULLUP); // Установка контакта сигнала стоп по направлению Y->0 в режим ввода с использованием встроенного подтягивающего резистора
  Serial.println("[setup()] Log: Stepper motor initialization completed!");


  // Инициализация матрицы датчиков (расширителей портов)
  Serial.println("[setup()] Log: PCF8574 initialization...");
  Wire.begin(SDA, SCL);

  brdX.begin(0x22, &Wire); // Инициализация столбцового (A-H) PCF8574 с I2C адресом 0x22
  brdY.begin(0x27, &Wire); // Инициализация строчного (1-8) PCF8574 с I2C адресом 0x27
  Serial.println("[setup()] Log: PCF8574 initialization completed!");


  // Инициализация подъёмного механизма
  Serial.println("[setup()] Log: Magnet lift initialization...");
  pinMode(mg, OUTPUT); // Установка контакта управляющих импульсов для сервопривода подъёмного механизма в режим вывода

  mgServ.attach(mg); // Прикрепление к объекту сервопривода соответсвующего контакта
  Serial.println("[setup()] Log: Magnet lift initialization completed!");


  // Чтение данных из энергонезависимой памяти
  Serial.println("[setup()] Log: Reading EEPROM");
  eeprom_read_block((void*)&ssid, (const void*)&ssidAddr, sizeof ssid); // Чтение SSID точки доступа
  eeprom_read_block((void*)&paswd, (const void*)&paswdAddr, sizeof paswd); // Чтение пароля от точки доступа
  
  eeprom_read_block((void*)&host, (const void*)&hostAddr, sizeof host); // Чтение ip сервера
  eeprom_read_block((void*)&port, (const void*)&portAddr, sizeof port); // Чтение порта сервера
  Serial.println("[setup()] Log: Reading EEPROM completed!");
  

  // Инициализация мультизадачности
  Serial.println("[setup()] Log: Multitasking initialization...");
  xTaskCreatePinnedToCore(serListener, "Serial listener", 10000, NULL, 0, &serListenerTask, 0); // Выставление параметров для задачи отслушивающей последовательный порт
  xTaskCreatePinnedToCore(tcpListener, "TCP listener", 10000, NULL, 0, &tcpListenerTask, 0); // Выставление параметров для задачи отслушивающей TCP подключение
  
  xTaskCreatePinnedToCore(brdListener, "Board listener", 10000, NULL, 0, &brdListenerTask, 1); // Выставление параметров для задачи отслушивающей матрицу датчиков
  Serial.println("[setup()] Log: Multitasking initialization completed!");
}

void serListener() { 
serListenerStart:;

goto serListenerStart;
}

void tcpListener() {
tcpListenerStart:;

  // Проверка и инициализация беспроводного подключения Wi-Fi
  if(WiFi.status() != WL_CONNECTED) { // Проверка подключения к точке доступа
    Serial.println("[tcpListener()] Error: Not connected to WiFi hotspot");
    Serial.print("[tcpListener()] Log: Trying to connect to WiFi hotspot.");
    WiFi.hostname("tcpListener()"); // Установка имени в сети
    WiFi.begin(ssid, paswd); // Старт попытки подключения
    for(char i = 0; i < 100 && WiFi.status() != WL_CONNECTED; ++i) { // Ждать 10 секунд или до успешного подключения
      Serial.print(".");
      delay(100);
    }
    Serial.println();
    if(WiFi.status() != WL_CONNECTED) { // Попытка таки не успешна
      Serial.println("[tcpListener()] Failure: Still can't connect to hotspot, try restarting the system and resetting the SSID and password!");
      goto tcpListenerStart;
    } else { // Подключение есть
      Serial.println("[tcpListener()] Log: Successfully connected to the WiFi network!");
    }
  }

  // Проверка и инициализация подключения к серверу
  if(!_tcp.connected()) {
    Serial.println("[tcpListener()] Error: Not connected to server");
    Serial.print("[tcpListener()] Log: Trying to connect to server.");
    for(char i = 0; i < 100 && !_tcp.connect(host, port); ++i) { // Ждать 10 секунд или до успешного подключения
      Serial.print(".");
      delay(100);
    }
    if(!_tcp.connected()) { // Попытка таки не успешна
      Serial.println("[tcpListener()] Failure: Still can't connect to the server, try restarting the system and resetting the server's IP and port!");
      goto tcpListenerStart;
    } else { // Подключение есть
      Serial.println("[tcpListener()] Log: Successfully connected to the server!");
    }
  }


  // Отправка сообщений из очереди
  
    
goto tcpListenerStart;
}

void brdListener() {
brdListenerStart:;

goto brdListenerStart;
}

void loop() {
  

}