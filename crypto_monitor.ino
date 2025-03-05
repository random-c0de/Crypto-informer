/*
Проект: Crypto Informer для ESP8266 0,96" 128х64 Oled module
Описание:
- Устройство подключается к Wi-Fi и получает данные о выбранных криптовалютах.
- Веб-интерфейс для настройки доступен всегда по IP-адресу устройства.
- IP-адрес устройства отображается на OLED-дисплее после подключения к Wi-Fi.
- При первоначальной настройке или при невозможности подключиться к сохраненной точке доступа с 2х попыток, создается точка доступа для конфигурации.

Подключение дисплея:
- Адрес I2C: 0x3C
- Драйвер: SSD1306
- SDA: D5 (GPIO14)
- SCL: D6 (GPIO12)
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>     // Для создания веб-сервера
#include <WiFiClientSecure.h>
#include <WiFiManager.h>          // Для управления Wi-Fi и порталом конфигурации
#include <ArduinoJson.h>          // Для работы с JSON
#include <LittleFS.h>             // Для хранения настроек

// Библиотеки для дисплея
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Определение пинов для подключения дисплея
#define OLED_SDA D5 // GPIO14
#define OLED_SCL D6 // GPIO12
#define OLED_RESET -1

// Создание объекта дисплея
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

// Создание объекта веб-сервера
ESP8266WebServer server(80);

// Буферы для пользовательских настроек
char updateIntervalStr[6] = "60";    // Интервал обновления (секунды)
char graphRangeStr[4] = "24";        // Диапазон графика (часы)
char graphIntervalStr[4] = "60";     // Интервал графика (минуты)

// Массивы для хранения выбранных криптовалют
bool selectedCryptos[3] = { true, true, true }; // По умолчанию все выбраны

// Имена криптовалют и их тикеры
String symbols[] = { "BTCUSDT", "ETHUSDT", "BNBUSDT" };
String tickers[] = { "BTC", "ETH", "BNB" };
int numSymbols = sizeof(symbols) / sizeof(symbols[0]);
int currentSymbol = -1; // Инициализируем -1, чтобы начать с первой выбранной

// Переменные для настроек
unsigned long updateInterval = 60000; // В миллисекундах
int graphRange = 24;                  // В часах
int graphInterval = 60;               // В минутах

unsigned long previousMillis = 0;

// Флаг для сохранения настроек
bool shouldSaveConfig = false;

// Прототипы функций
void loadConfig();
void saveConfig();
void saveConfigCallback();
void configModeCallback(WiFiManager *myWiFiManager);
void displayMessage(String message);
void getCryptoDetails(String symbol);
void displayCryptoData(String ticker, float prices[], int numBars);
void drawPriceGraph(float prices[], int numBars);
String formatNumber(long num);
String formatPrice(float price); // Новая функция для форматирования цен

// Обработчики веб-сервера
void handleRoot();
void handleSave();
void handleNotFound();

void setup() {
  // Инициализация последовательного порта для отладки
  Serial.begin(115200);
  Serial.println();
  Serial.println("Запуск...");

  // Настройка пинов I2C для дисплея
  Wire.begin(OLED_SDA, OLED_SCL);

  // Инициализация дисплея
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Адрес 0x3C для 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Остановка, если дисплей не найден
  }

  display.clearDisplay();
  display.display();

  // Инициализация LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Ошибка монтирования LittleFS");
    displayMessage("FS Mount Failed");
    delay(2000);
    ESP.restart();
  } else {
    Serial.println("LittleFS смонтирован");
    loadConfig();
  }

  // Настройка WiFiManager
  WiFiManager wifiManager;

  // Настройка точки доступа
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConnectRetries(2);
  wifiManager.setConfigPortalTimeout(180); // 3 минуты

  // Попытка подключения
  if (!wifiManager.autoConnect("ESP-CRYPTO-INFORMER", "12345678")) {
    Serial.println("Не удалось подключиться к Wi-Fi.");
    displayMessage("Wi-Fi Unavailable");
    delay(2000);
    // Продолжаем работу без перезагрузки
  }

  // Отображение статуса подключения и IP-адреса
  String ipAddress = WiFi.localIP().toString();
  Serial.println("Wi-Fi подключен");
  Serial.print("IP Address: ");
  Serial.println(ipAddress);
  displayMessage("Wi-Fi Connected\nIP: " + ipAddress);
  delay(2000);

  // Инициализация веб-сервера
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started");

  // Применение настроек
  updateInterval = atol(updateIntervalStr) * 1000;
  graphRange = atoi(graphRangeStr);
  graphInterval = atoi(graphIntervalStr);
}

void loop() {
  unsigned long currentMillis = millis();

  // Обработка клиентских запросов
  server.handleClient();

  if (currentMillis - previousMillis >= updateInterval) {
    previousMillis = currentMillis;

    // Поиск следующей выбранной криптовалюты
    int attempts = 0;
    do {
      currentSymbol = (currentSymbol + 1) % numSymbols;
      attempts++;
      if (attempts > numSymbols) {
        Serial.println("Нет выбранных криптовалют");
        displayMessage("No Cryptos Selected");
        return;
      }
    } while (!selectedCryptos[currentSymbol]);

    // Получение данных
    getCryptoDetails(symbols[currentSymbol]);
  }
}

// Функция, вызываемая при запуске в режиме точки доступа
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Запущен режим настройки");
  Serial.print("Точка доступа: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.print("IP адрес: ");
  Serial.println(WiFi.softAPIP());

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("AP Mode");
  display.print("SSID: ");
  display.println(myWiFiManager->getConfigPortalSSID());
  display.print("IP: ");
  display.println(WiFi.softAPIP());
  display.print("Pass: 12345678");
  display.display();
}

// Обработчик главной страницы
void handleRoot() {
  String html = "<html><head><title>Crypto Informer Settings</title></head><body>";
  html += "<h1>Crypto Informer Settings</h1>";
  html += "<form action='/save' method='POST'>";
  html += "Update Interval (seconds):<br/>";
  html += "<input type='number' name='updateInterval' value='" + String(updateInterval / 1000) + "'><br/><br/>";
  html += "Graph Range (hours):<br/>";
  html += "<input type='number' name='graphRange' value='" + String(graphRange) + "'><br/><br/>";
  html += "Graph Interval (minutes):<br/>";
  html += "<input type='number' name='graphInterval' value='" + String(graphInterval) + "'><br/><br/>";
  html += "Cryptocurrencies:<br/>";
  html += "<input type='checkbox' name='crypto_btc' value='1'" + String(selectedCryptos[0] ? " checked" : "") + "> BTC<br/>";
  html += "<input type='checkbox' name='crypto_eth' value='1'" + String(selectedCryptos[1] ? " checked" : "") + "> ETH<br/>";
  html += "<input type='checkbox' name='crypto_bnb' value='1'" + String(selectedCryptos[2] ? " checked" : "") + "> BNB<br/><br/>";
  html += "<input type='submit' value='Save'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

// Обработчик сохранения настроек
void handleSave() {
  if (server.method() == HTTP_POST) {
    // Получаем данные из формы
    String updateIntervalInput = server.arg("updateInterval");
    String graphRangeInput = server.arg("graphRange");
    String graphIntervalInput = server.arg("graphInterval");

    // Сохраняем настройки
    strcpy(updateIntervalStr, updateIntervalInput.c_str());
    strcpy(graphRangeStr, graphRangeInput.c_str());
    strcpy(graphIntervalStr, graphIntervalInput.c_str());

    updateInterval = atol(updateIntervalStr) * 1000;
    graphRange = atoi(graphRangeStr);
    graphInterval = atoi(graphIntervalStr);

    selectedCryptos[0] = server.hasArg("crypto_btc");
    selectedCryptos[1] = server.hasArg("crypto_eth");
    selectedCryptos[2] = server.hasArg("crypto_bnb");

    saveConfig();

    server.sendHeader("Location", "/");
    server.send(303);
    
    // Отображаем сообщение об успешном сохранении
    displayMessage("Settings Saved");
    delay(2000);
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

// Обработчик для несуществующих страниц
void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

// Колбэк для сохранения настроек
void saveConfigCallback() {
  Serial.println("Надо сохранить настройки");
  shouldSaveConfig = true;
}

// Загрузка настроек из файловой системы
void loadConfig() {
  if (LittleFS.exists("/config.json")) {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size + 1]);
      configFile.readBytes(buf.get(), size);
      buf[size] = '\0';
      configFile.close();

      DynamicJsonDocument json(512);
      DeserializationError error = deserializeJson(json, buf.get());

      if (!error) {
        strcpy(updateIntervalStr, json["updateInterval"]);
        strcpy(graphRangeStr, json["graphRange"]);
        strcpy(graphIntervalStr, json["graphInterval"]);

        selectedCryptos[0] = json["crypto_btc"];
        selectedCryptos[1] = json["crypto_eth"];
        selectedCryptos[2] = json["crypto_bnb"];
      } else {
        Serial.println("Ошибка чтения настроек");
      }
    }
  } else {
    Serial.println("Файл настроек не найден");
  }
}

// Сохранение настроек в файловую систему
void saveConfig() {
  DynamicJsonDocument json(512);
  json["updateInterval"] = updateIntervalStr;
  json["graphRange"] = graphRangeStr;
  json["graphInterval"] = graphIntervalStr;
  json["crypto_btc"] = selectedCryptos[0];
  json["crypto_eth"] = selectedCryptos[1];
  json["crypto_bnb"] = selectedCryptos[2];

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Ошибка открытия файла для записи");
  } else {
    serializeJson(json, configFile);
    configFile.close();
    Serial.println("Настройки сохранены");
  }
}

// Функция отображения сообщения на дисплее
void displayMessage(String message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  // Разбиваем сообщение по символу новой строки
  int16_t y = 0;
  int16_t lineHeight = 8; // Высота строки для шрифта размера 1
  int from = 0;
  int16_t len = message.length();
  while (from < len) {
    int to = message.indexOf('\n', from);
    if (to == -1) {
      to = len;
    }
    String line = message.substring(from, to);
    display.setCursor(0, y);
    display.println(line);
    y += lineHeight;
    from = to + 1;
  }

  display.display();
}

// Функция получения данных о криптовалюте
void getCryptoDetails(String symbol) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Нет подключения к Wi-Fi");
    displayMessage("No Wi-Fi Connection");
    return;
  }

  // Расчет параметров для запроса
  int limit = (graphRange * 60) / graphInterval;
  if (limit > 1000) limit = 1000; // Ограничение Binance API

  String intervalStr;
  if (graphInterval % 60 == 0) {
    int hours = graphInterval / 60;
    intervalStr = String(hours) + "h";
  } else {
    intervalStr = String(graphInterval) + "m";
  }

  String url = "https://data-api.binance.vision/api/v3/klines?symbol=" + symbol +
               "&interval=" + intervalStr + "&limit=" + String(limit);
  Serial.print("Запрос: ");
  Serial.println(url);

  // Объект для безопасного Wi-Fi соединения
  WiFiClientSecure client;
  client.setInsecure(); // Отключаем проверку сертификата для упрощения

  // Создание объекта HTTPClient
  HTTPClient https;

  // Использование клиентского соединения
  https.begin(client, url);

  // Установка заголовка User-Agent
  https.setUserAgent("Mozilla/5.0 (compatible; ESP8266)");

  // Выполнение GET-запроса
  int httpCode = https.GET();

  if (httpCode > 0) {
    // HTTP-код ответа
    Serial.printf("HTTP-код ответа: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
      // Потоковый парсер JSON
      const size_t capacity = JSON_ARRAY_SIZE(limit) + limit * JSON_ARRAY_SIZE(12) + 1000;
      DynamicJsonDocument doc(capacity);

      DeserializationError error = deserializeJson(doc, https.getStream());

      if (error) {
        Serial.print("Ошибка парсинга JSON: ");
        Serial.println(error.c_str());
        displayMessage("JSON Error: " + String(error.c_str()));
      } else {
        // Массив для хранения цен закрытия
        int numBars = doc.size();
        float closePrices[numBars];

        // Извлечение данных
        for (int i = 0; i < numBars; i++) {
          JsonArray kline = doc[i];
          const char* closePriceStr = kline[4];
          if (closePriceStr) {
            closePrices[i] = atof(closePriceStr); // Цена закрытия
          } else {
            closePrices[i] = 0;
          }
        }

        // Отображение данных на дисплее
        displayCryptoData(tickers[currentSymbol], closePrices, numBars);
      }
    } else if (httpCode == 429) {
      Serial.println("Слишком много запросов, повтор через 10 секунд");
      displayMessage("Rate Limit, Retry...");
      delay(10000);
      getCryptoDetails(symbol); // Повторный вызов
    } else {
      Serial.printf("Непредвиденный HTTP-код: %d\n", httpCode);
      displayMessage("HTTP Error: " + String(httpCode));
    }
  } else {
    Serial.print("Ошибка запроса: ");
    Serial.println(https.errorToString(httpCode));
    displayMessage("Request Error");
  }

  // Завершение работы с HTTPClient
  https.end();
}

// Функция отображения данных на OLED-дисплее
void displayCryptoData(String ticker, float prices[], int numBars) {
  display.clearDisplay();

  // Отображение графика цен в верхней части экрана
  drawPriceGraph(prices, numBars);

  // Округление цены до целого числа
  long priceInt = round(prices[numBars - 1]);

  // Форматирование цены с разделителем тысяч
  String priceStr = formatNumber(priceInt);

  // Устанавливаем размер шрифта для цены
  display.setTextSize(3);
  display.setTextColor(WHITE);

  // Вычисляем ширину и высоту цены для позиционирования
  int16_t x1, y1;
  uint16_t priceWidth, priceHeight;
  display.getTextBounds(priceStr, 0, 0, &x1, &y1, &priceWidth, &priceHeight);

  // Ширина области для тикера
  int tickerAreaWidth = 16; // Настраиваемая ширина области для тикера

  // Позиционирование цены с учётом области для тикера
  int priceX = tickerAreaWidth + 4; // Отступ от тикера
  int priceY = 40; // Позиция по Y для цены

  // Отображаем цену
  display.setCursor(priceX, priceY);
  display.println(priceStr);

  // Отображение тикера вертикально перед ценой
  display.setTextSize(1); // Размер шрифта для тикера
  int letterSpacing = 0; // Расстояние между буквами
  int tickerX = 2; // Позиция по X для тикера
  int tickerY = priceY; // Начальная позиция по Y для тикера

  // Отображаем каждую букву тикера
  for (int i = 0; i < ticker.length(); i++) {
    char c = ticker.charAt(i);
    display.setCursor(tickerX, tickerY + i * (8 + letterSpacing));
    display.println(c);
  }

  display.display();
}

// Функция отрисовки графика цен
void drawPriceGraph(float prices[], int numBars) {
  // Находим минимальное и максимальное значения
  float minPrice = prices[0], maxPrice = prices[0];
  for (int i = 1; i < numBars; i++) {
    if (prices[i] < minPrice) minPrice = prices[i];
    if (prices[i] > maxPrice) maxPrice = prices[i];
  }
  if (maxPrice - minPrice == 0) { // Избегаем деления на ноль
    maxPrice += 1;
    minPrice -= 1;
  }

  // Форматируем значения для отображения
  String minStr = formatPrice(minPrice);
  String maxStr = formatPrice(maxPrice);

  // Отображаем min и max слева
  display.setTextSize(1);
  display.setCursor(0, 0);  // Верхний левый угол для max
  display.println(maxStr);
  display.setCursor(0, 24); // Нижний левый угол для min
  display.println(minStr);

  // Настраиваем координаты графика
  int graphX = 20;      // Сдвиг вправо для меток
  int graphY = 0;       // Верхняя граница графика
  int graphWidth = 108; // Ширина графика (128 - 20)
  int graphHeight = 32; // Высота графика

  // Рассчитываем ширину баров
  int barWidth = (graphWidth - (numBars - 1)) / numBars;
  if (barWidth < 1) barWidth = 1; // Минимальная ширина бара

  // Начальная позиция для самого правого бара (последний бар — самый новый)
  int startX = graphX + graphWidth - barWidth;

  // Отрисовка баров справа налево, начиная с последнего элемента
  for (int i = 0; i < numBars; i++) {
    int index = numBars - 1 - i;  // Индекс от последнего к первому
    int barHeight = map(prices[index], minPrice, maxPrice, 0, graphHeight);
    int x = startX - i * (barWidth + 1); // Сдвигаемся влево
    int y = graphY + (graphHeight - barHeight);
    display.fillRect(x, y, barWidth, barHeight, WHITE);
  }
}

// Функция форматирования цены
String formatPrice(float price) {
  if (price < 1000) {
    return String(price, 0);           // Числа < 1000 без изменений
  } else if (price < 10000) {
    return String(price / 1000, 2);    // Например, 2856 -> "2,85"
  } else {
    return String(price / 1000, 1);    // Например, 91631 -> "91,6"
  }
}

// Функция форматирования числа с разделителем тысяч
String formatNumber(long num) {
  String numStr = String(num);
  String formattedStr = "";

  int len = numStr.length();
  int count = 0;

  for (int i = len - 1; i >= 0; i--) {
    formattedStr = numStr.charAt(i) + formattedStr;
    count++;
    if (count == 3 && i != 0) {
      // Добавляем точку как разделитель, только если число >= 1000
      if (len > 3) {
        formattedStr = "." + formattedStr;
      }
      count = 0;
    }
  }

  return formattedStr;
}