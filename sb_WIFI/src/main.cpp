#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <EEPROM.h>

/*
 ______     ______     __         ______         __         ______     __    __     ______  
/\  ___\   /\  __ \   /\ \       /\__  _\       /\ \       /\  __ \   /\ "-./  \   /\  == \ 
\ \___  \  \ \  __ \  \ \ \____  \/_/\ \/       \ \ \____  \ \  __ \  \ \ \-./\ \  \ \  _-/ 
 \/\_____\  \ \_\ \_\  \ \_____\    \ \_\        \ \_____\  \ \_\ \_\  \ \_\ \ \_\  \ \_\   
  \/_____/   \/_/\/_/   \/_____/     \/_/         \/_____/   \/_/\/_/   \/_/  \/_/   \/_/   
*/

/* 
 ___      ___       _____           ________      
|\  \    /  /|     / __  \         |\   ____\     
\ \  \  /  / /    |\/_|\  \        \ \  \___|_    
 \ \  \/  / /     \|/ \ \  \        \ \_____  \   
  \ \    / /           \ \  \  ___   \|____|\  \  
   \ \__/ /             \ \__\|\__\    ____\_\  \ 
    \|__|/               \|__|\|__|   |\_________\
.                                     \|_________|
*/                                                     
                                              
#define PIN_PUMP_TOMATO_1 14
#define PIN_PUMP_TOMATO_2 13
#define PIN_PUMP_CUCUMBER 12

#define sensor_pin_1 34
#define sensor_pin_1 35
#define sensor_pin_1 36

#define smart 1
#define automatic 0

// Инициализация порогов влажности
int HUMIDITY_THRESHOLD_TOMATO_1 = 2500;
int HUMIDITY_THRESHOLD_TOMATO_2 = 2500;
int HUMIDITY_THRESHOLD_CUCUMBER = 2500;

// Инициализация объемов воды
int WATER_VOLUME_TOMATO_1 = 100;
int WATER_VOLUME_TOMATO_2 = 100;
int WATER_VOLUME_CUCUMBER = 100;

const char* ssid = "***********";
const char* password = "********";
const char* botToken = "*****************************";  // добавьте сюда свой токен
const char* CHAT_ID = "5386616268";
const int ledPin = 2;

// Глобальные переменные
int dump_T1, dump_T2, dump_C;
bool t1, t2, c; 
bool totalERROR;
uint8_t error;
bool mode = automatic;
bool wateredToday[7] = {false};

// Структура времени
typedef struct {
  uint8_t wday;
  uint8_t hours;
  uint8_t minutes;
} SystemTime;

// Структура расписания
struct PlantSchedule {
  int sun = -1;
  int mon = -1;
  int tue = -1;
  int wed = -1;
  int thu = -1;
  int fri = -1;
  int sat = -1;
};

// Объявление структур расписания
PlantSchedule scheduleCucumber;
PlantSchedule scheduleTomato1;
PlantSchedule scheduleTomato2;

SystemTime baseTime = {0, 0, 0};
unsigned long baseMillis, lastMillis = 0;
SystemTime currentTime = {0, 0, 0};
const unsigned long msPerMinute = 60000;
const unsigned long msPerDay = 86400000;

const char* DAY_NAMES[7] = {
  "Воскресенье", "Понедельник", "Вторник", "Среда", 
  "Четверг", "Пятница", "Суббота"
};

const char* DAY_SHORT[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

unsigned long timing = 0;
const long serialInterval = 1000;
const long wifiTimeout = 10000;

enum WateringState { 
  WATERING_IDLE, 
  WATERING_TOMATO1_ON, 
  WATERING_TOMATO2_ON, 
  WATERING_CUCUMBER_ON 
};

struct {
  bool active = false;
  bool wateringCucumber = false;
  bool wateringTomato1 = false;
  bool wateringTomato2 = false;
  unsigned long startTimeCucumber = 0;
  unsigned long startTimeTomato1 = 0;
  unsigned long startTimeTomato2 = 0;
} scheduleWatering;

WateringState wateringState = WATERING_IDLE;
unsigned long wateringStartTime = 0;

WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

// Прототипы функций
void printSchedule();
void sensorChec();
void startWatering();
void handleWatering();
void checkWateringSchedule();
void handleSetSchedule(String chat_id, String command);
void handleSetTimeCommand(String chat_id, String command);
void handleSetHumidity(String chat_id, String text);
void handleSetVolume(String chat_id, String text);
void handleScheduleWatering();
void sendWateringNotification(const String& plant, int volume, int humidityBefore, int humidityAfter);
void resetWateringFlags();

String formatTime(uint8_t hours, uint8_t minutes) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", hours, minutes);
  return String(buf);
}

String getUptime() {
  unsigned long totalMinutes = (millis() / msPerMinute);
  unsigned long hours = totalMinutes / 60;
  unsigned long minutes = totalMinutes % 60;
  return String(hours) + " ч. " + String(minutes) + " мин.";
}

void updateTime() {
  unsigned long elapsedMillis = millis() - baseMillis;
  unsigned long elapsedMinutes = elapsedMillis / msPerMinute;
  
  uint8_t minutes = (baseTime.minutes + elapsedMinutes) % 60;
  uint8_t hours = (baseTime.hours + (baseTime.minutes + elapsedMinutes) / 60) % 24;
  unsigned long elapsedDays = elapsedMillis / msPerDay;
  uint8_t wday = (baseTime.wday + elapsedDays) % 7;
  
  currentTime.wday = wday;
  currentTime.hours = hours;
  currentTime.minutes = minutes;

  if (hours == 0 && minutes == 0) {
    resetWateringFlags();
  }
}

void handleSetTimeCommand(String chat_id, String command) {
  int spacePos = command.indexOf(' ');
  if (spacePos == -1) {
    bot.sendMessage(chat_id, "Формат: /settime [день_недели] HH:MM\nПример: /settime mon 15:30\nДни: sun,mon,tue,wed,thu,fri,sat", "");
    return;
  }
  
  String args = command.substring(spacePos + 1);
  args.trim();
  
  int spacePos2 = args.indexOf(' ');
  String dayStr = "";
  String timeStr = "";
  
  if (spacePos2 != -1) {
    dayStr = args.substring(0, spacePos2);
    timeStr = args.substring(spacePos2 + 1);
  } else {
    timeStr = args;
  }
  
  int colonPos = timeStr.indexOf(':');
  if (colonPos == -1) {
    bot.sendMessage(chat_id, "Ошибка формата времени! Используйте HH:MM", "");
    return;
  }
  
  uint8_t hours = timeStr.substring(0, colonPos).toInt();
  uint8_t minutes = timeStr.substring(colonPos + 1).toInt();
  
  if (hours >= 24 || minutes >= 60) {
    bot.sendMessage(chat_id, "Некорректное время! Часы: 0-23, Минуты: 0-59", "");
    return;
  }
  
  uint8_t wday = currentTime.wday;
  if (!dayStr.isEmpty()) {
    dayStr.toLowerCase();
    for (int i = 0; i < 7; i++) {
      if (dayStr.equals(DAY_SHORT[i])) {
        wday = i;
        break;
      }
    }
  }
  
  baseTime.wday = wday;
  baseTime.hours = hours;
  baseTime.minutes = minutes;
  baseMillis = millis();
  resetWateringFlags();
  
  String message = "⏱ Время установлено!\n";
  message += "День: " + String(DAY_NAMES[wday]) + "\n";
  message += "Время: " + formatTime(hours, minutes);
  bot.sendMessage(chat_id, message, "");
}

void handleSetSchedule(String chat_id, String command) {
  int firstSpace = command.indexOf(' ');
  if (firstSpace == -1) {
    bot.sendMessage(chat_id, "❌ Формат: /setschedule [растение] [день] [час]\n"
                    "Растения: cucumber, tomato1, tomato2\n"
                    "Дни: sun,mon,tue,wed,thu,fri,sat\n"
                    "Час: 0-23 или -1 для отключения\n"
                    "Пример: /setschedule cucumber mon 8", "");
    return;
  }
  
  String args = command.substring(firstSpace + 1);
  args.trim();
  
  int spacePos1 = args.indexOf(' ');
  int spacePos2 = args.lastIndexOf(' ');
  
  if (spacePos1 == -1 || spacePos2 == spacePos1) {
    bot.sendMessage(chat_id, "❌ Ошибка формата! Укажите растение, день и час", "");
    return;
  }
  
  String plantStr = args.substring(0, spacePos1);
  plantStr.toLowerCase();
  String dayStr = args.substring(spacePos1 + 1, spacePos2);
  dayStr.toLowerCase();
  String hourStr = args.substring(spacePos2 + 1);
  hourStr.trim();
  
  int dayIndex = -1;
  for (int i = 0; i < 7; i++) {
    if (dayStr.equals(DAY_SHORT[i])) {
      dayIndex = i;
      break;
    }
  }
  
  if (dayIndex == -1) {
    bot.sendMessage(chat_id, "❌ Неверный день! Допустимые значения: sun,mon,tue,wed,thu,fri,sat", "");
    return;
  }
  
  int hourValue = hourStr.toInt();
  if ((hourValue < -1) || (hourValue > 23)) {
    bot.sendMessage(chat_id, "❌ Неверное значение! Допустимые значения: -1 или 0-23", "");
    return;
  }
  
  String plantName = "";
  int eepromOffset = 0;
  
  if (plantStr == "cucumber") {
    switch(dayIndex) {
      case 0: scheduleCucumber.sun = hourValue; break;
      case 1: scheduleCucumber.mon = hourValue; break;
      case 2: scheduleCucumber.tue = hourValue; break;
      case 3: scheduleCucumber.wed = hourValue; break;
      case 4: scheduleCucumber.thu = hourValue; break;
      case 5: scheduleCucumber.fri = hourValue; break;
      case 6: scheduleCucumber.sat = hourValue; break;
    }
    plantName = "огурцы 🥒";
    eepromOffset = 0;
  } 
  else if (plantStr == "tomato1") {
    switch(dayIndex) {
      case 0: scheduleTomato1.sun = hourValue; break;
      case 1: scheduleTomato1.mon = hourValue; break;
      case 2: scheduleTomato1.tue = hourValue; break;
      case 3: scheduleTomato1.wed = hourValue; break;
      case 4: scheduleTomato1.thu = hourValue; break;
      case 5: scheduleTomato1.fri = hourValue; break;
      case 6: scheduleTomato1.sat = hourValue; break;
    }
    plantName = "помидоры 1 🍅";
    eepromOffset = 100;
  }
  else if (plantStr == "tomato2") {
    switch(dayIndex) {
      case 0: scheduleTomato2.sun = hourValue; break;
      case 1: scheduleTomato2.mon = hourValue; break;
      case 2: scheduleTomato2.tue = hourValue; break;
      case 3: scheduleTomato2.wed = hourValue; break;
      case 4: scheduleTomato2.thu = hourValue; break;
      case 5: scheduleTomato2.fri = hourValue; break;
      case 6: scheduleTomato2.sat = hourValue; break;
    }
    plantName = "помидоры 2 🍅";
    eepromOffset = 200;
  }
  else {
    bot.sendMessage(chat_id, "❌ Неверное растение! Допустимые значения: cucumber, tomato1, tomato2", "");
    return;
  }
  
  int addr = eepromOffset + dayIndex * sizeof(int);
  EEPROM.put(addr, hourValue);
  EEPROM.commit();
  
  String message = "✅ Расписание обновлено!\n";
  message += "Растение: " + plantName + "\n";
  message += "День: " + String(DAY_NAMES[dayIndex]) + "\n";
  message += "Час полива: ";
  
  if (hourValue >= 0) {
    message += formatTime(hourValue, 0);
  } else {
    message += "отключено";
  }
  
  bot.sendMessage(chat_id, message, "");
  printSchedule();
}

void checkWateringSchedule() {
  if (currentTime.minutes != 0 || scheduleWatering.active || wateredToday[currentTime.wday]) {
    return;
  }

  bool needWaterCucumber = false;
  bool needWaterTomato1 = false;
  bool needWaterTomato2 = false;
  String plantsToWater = "";

  // Проверка огурцов
  int wateringHourCucumber = -1;
  switch(currentTime.wday) {
    case 0: wateringHourCucumber = scheduleCucumber.sun; break;
    case 1: wateringHourCucumber = scheduleCucumber.mon; break;
    case 2: wateringHourCucumber = scheduleCucumber.tue; break;
    case 3: wateringHourCucumber = scheduleCucumber.wed; break;
    case 4: wateringHourCucumber = scheduleCucumber.thu; break;
    case 5: wateringHourCucumber = scheduleCucumber.fri; break;
    case 6: wateringHourCucumber = scheduleCucumber.sat; break;
  }
  
  if (wateringHourCucumber >= 0 && currentTime.hours == wateringHourCucumber) {
    if (mode == automatic) {
      needWaterCucumber = true;
      plantsToWater += "огурцы 🥒, ";
    } else if (mode == smart) {
      sensorChec();
      if (dump_C > HUMIDITY_THRESHOLD_CUCUMBER) {
        needWaterCucumber = true;
        plantsToWater += "огурцы 🥒, ";
      } else {
        String message = "💧 Автополив пропущен: огурцы 🥒\n";
        message += "Причина: достаточная влажность\n";
        message += "День: " + String(DAY_NAMES[currentTime.wday]) + "\n";
        message += "Время: " + formatTime(currentTime.hours, currentTime.minutes);
        bot.sendMessage(CHAT_ID, message, "");
      }
    }
  }

  // Проверка помидоров 1
  int wateringHourTomato1 = -1;
  switch(currentTime.wday) {
    case 0: wateringHourTomato1 = scheduleTomato1.sun; break;
    case 1: wateringHourTomato1 = scheduleTomato1.mon; break;
    case 2: wateringHourTomato1 = scheduleTomato1.tue; break;
    case 3: wateringHourTomato1 = scheduleTomato1.wed; break;
    case 4: wateringHourTomato1 = scheduleTomato1.thu; break;
    case 5: wateringHourTomato1 = scheduleTomato1.fri; break;
    case 6: wateringHourTomato1 = scheduleTomato1.sat; break;
  }
  
  if (wateringHourTomato1 >= 0 && currentTime.hours == wateringHourTomato1) {
    if (mode == automatic) {
      needWaterTomato1 = true;
      plantsToWater += "помидоры 1 🍅, ";
    } else if (mode == smart) {
      sensorChec();
      if (dump_T1 > HUMIDITY_THRESHOLD_TOMATO_1) {
        needWaterTomato1 = true;
        plantsToWater += "помидоры 1 🍅, ";
      } else {
        String message = "💧 Автополив пропущен: помидоры 1 🍅\n";
        message += "Причина: достаточная влажность\n";
        message += "День: " + String(DAY_NAMES[currentTime.wday]) + "\n";
        message += "Время: " + formatTime(currentTime.hours, currentTime.minutes);
        bot.sendMessage(CHAT_ID, message, "");
      }
    }
  }

  // Проверка помидоров 2
  int wateringHourTomato2 = -1;
  switch(currentTime.wday) {
    case 0: wateringHourTomato2 = scheduleTomato2.sun; break;
    case 1: wateringHourTomato2 = scheduleTomato2.mon; break;
    case 2: wateringHourTomato2 = scheduleTomato2.tue; break;
    case 3: wateringHourTomato2 = scheduleTomato2.wed; break;
    case 4: wateringHourTomato2 = scheduleTomato2.thu; break;
    case 5: wateringHourTomato2 = scheduleTomato2.fri; break;
    case 6: wateringHourTomato2 = scheduleTomato2.sat; break;
  }
  
  if (wateringHourTomato2 >= 0 && currentTime.hours == wateringHourTomato2) {
    if (mode == automatic) {
      needWaterTomato2 = true;
      plantsToWater += "помидоры 2 🍅, ";
    } else if (mode == smart) {
      sensorChec();
      if (dump_T2 > HUMIDITY_THRESHOLD_TOMATO_2) {
        needWaterTomato2 = true;
        plantsToWater += "помидоры 2 🍅, ";
      } else {
        String message = "💧 Автополив пропущен: помидоры 2 🍅\n";
        message += "Причина: достаточная влажность\n";
        message += "День: " + String(DAY_NAMES[currentTime.wday]) + "\n";
        message += "Время: " + formatTime(currentTime.hours, currentTime.minutes);
        bot.sendMessage(CHAT_ID, message, "");
      }
    }
  }

  if (needWaterCucumber || needWaterTomato1 || needWaterTomato2) {
    scheduleWatering.active = true;
    scheduleWatering.wateringCucumber = needWaterCucumber;
    scheduleWatering.wateringTomato1 = needWaterTomato1;
    scheduleWatering.wateringTomato2 = needWaterTomato2;
    scheduleWatering.startTimeCucumber = 0;
    scheduleWatering.startTimeTomato1 = 0;
    scheduleWatering.startTimeTomato2 = 0;
    
    if (!plantsToWater.isEmpty()) {
      plantsToWater.remove(plantsToWater.length() - 2);
      bot.sendMessage(CHAT_ID, "💧 Начался одновременный полив: " + plantsToWater, "");
    }
  }
}

void handleScheduleWatering() {
  if (!scheduleWatering.active) return;

  unsigned long currentMillis = millis();
  bool allCompleted = true;

  if (scheduleWatering.wateringCucumber) {
    if (scheduleWatering.startTimeCucumber == 0) {
      scheduleWatering.startTimeCucumber = currentMillis;
      digitalWrite(PIN_PUMP_CUCUMBER, HIGH);
    } 
    else if (currentMillis - scheduleWatering.startTimeCucumber >= (unsigned long)(WATER_VOLUME_CUCUMBER * 30)) {
      digitalWrite(PIN_PUMP_CUCUMBER, LOW);
      scheduleWatering.wateringCucumber = false;
      int humidityBefore = dump_C;
      sensorChec();
      sendWateringNotification("огурцов 🥒", WATER_VOLUME_CUCUMBER, humidityBefore, dump_C);
    } else {
      allCompleted = false;
    }
  }

  if (scheduleWatering.wateringTomato1) {
    if (scheduleWatering.startTimeTomato1 == 0) {
      scheduleWatering.startTimeTomato1 = currentMillis;
      digitalWrite(PIN_PUMP_TOMATO_1, HIGH);
    } 
    else if (currentMillis - scheduleWatering.startTimeTomato1 >= (unsigned long)(WATER_VOLUME_TOMATO_1 * 30)) {
      digitalWrite(PIN_PUMP_TOMATO_1, LOW);
      scheduleWatering.wateringTomato1 = false;
      int humidityBefore = dump_T1;
      sensorChec();
      sendWateringNotification("помидоров 1 🍅", WATER_VOLUME_TOMATO_1, humidityBefore, dump_T1);
    } else {
      allCompleted = false;
    }
  }

  if (scheduleWatering.wateringTomato2) {
    if (scheduleWatering.startTimeTomato2 == 0) {
      scheduleWatering.startTimeTomato2 = currentMillis;
      digitalWrite(PIN_PUMP_TOMATO_2, HIGH);
    } 
    else if (currentMillis - scheduleWatering.startTimeTomato2 >= (unsigned long)(WATER_VOLUME_TOMATO_2 * 30)) {
      digitalWrite(PIN_PUMP_TOMATO_2, LOW);
      scheduleWatering.wateringTomato2 = false;
      int humidityBefore = dump_T2;
      sensorChec();
      sendWateringNotification("помидоров 2 🍅", WATER_VOLUME_TOMATO_2, humidityBefore, dump_T2);
    } else {
      allCompleted = false;
    }
  }

  if (allCompleted && 
      !scheduleWatering.wateringCucumber && 
      !scheduleWatering.wateringTomato1 && 
      !scheduleWatering.wateringTomato2) {
    scheduleWatering.active = false;
    wateredToday[currentTime.wday] = true;
  }
}

void sendWateringNotification(const String& plant, int volume, int humidityBefore, int humidityAfter) {
  String message = "✅ Полив " + plant + " завершен!\n";
  message += "Вылито: " + String(volume) + " мл\n";
  message += "Влажность до: " + String(humidityBefore) + "\n";
  message += "Влажность после: " + String(humidityAfter);
  bot.sendMessage(CHAT_ID, message, "");
}

void startWatering() {
  if (wateringState == WATERING_IDLE) {
    Serial.println("Полив запущен!");
    wateringState = WATERING_TOMATO1_ON;
    digitalWrite(PIN_PUMP_TOMATO_1, HIGH);
    wateringStartTime = millis();
    bot.sendMessage(CHAT_ID, "💧 Начался полив помидоров 1 🍅", "");
  }
}

void handleWatering() {
  unsigned long currentMillis = millis();
  
  switch (wateringState) {
    case WATERING_TOMATO1_ON:
      if (currentMillis - wateringStartTime >= (WATER_VOLUME_TOMATO_1 * 30)) {
        digitalWrite(PIN_PUMP_TOMATO_1, LOW);
        wateringState = WATERING_TOMATO2_ON;
        digitalWrite(PIN_PUMP_TOMATO_2, HIGH);
        wateringStartTime = currentMillis;
        Serial.println("Полив помидоров 1 завершен");
        bot.sendMessage(CHAT_ID, "💧 Начался полив помидоров 2 🍅", "");
      }
      break;
      
    case WATERING_TOMATO2_ON:
      if (currentMillis - wateringStartTime >= (WATER_VOLUME_TOMATO_2 * 30)) {
        digitalWrite(PIN_PUMP_TOMATO_2, LOW);
        wateringState = WATERING_CUCUMBER_ON;
        digitalWrite(PIN_PUMP_CUCUMBER, HIGH);
        wateringStartTime = currentMillis;
        Serial.println("Полив помидоров 2 завершен");
        bot.sendMessage(CHAT_ID, "💧 Начался полив огурцов 🥒", "");
      }
      break;
      
    case WATERING_CUCUMBER_ON:
      if (currentMillis - wateringStartTime >= (WATER_VOLUME_CUCUMBER * 30)) {
        digitalWrite(PIN_PUMP_CUCUMBER, LOW);
        wateringState = WATERING_IDLE;
        Serial.println("Полив огурцов завершен");
        Serial.println("Весь полив завершен!");
        
        String message = "✅ Весь полив завершен!\n";
        message += "Объемы:\n";
        message += "- Помидоры 1: " + String(WATER_VOLUME_TOMATO_1) + " мл\n";
        message += "- Помидоры 2: " + String(WATER_VOLUME_TOMATO_2) + " мл\n";
        message += "- Огурцы: " + String(WATER_VOLUME_CUCUMBER) + " мл";
        bot.sendMessage(CHAT_ID, message, "");
      }
      break;
  }
}

void printSchedule() {
  Serial.println("\n--- Расписание полива ---");
  
  Serial.println("Огурцы 🥒:");
  for (int i = 0; i < 7; i++) {
    int hour = -1;
    switch(i) {
      case 0: hour = scheduleCucumber.sun; break;
      case 1: hour = scheduleCucumber.mon; break;
      case 2: hour = scheduleCucumber.tue; break;
      case 3: hour = scheduleCucumber.wed; break;
      case 4: hour = scheduleCucumber.thu; break;
      case 5: hour = scheduleCucumber.fri; break;
      case 6: hour = scheduleCucumber.sat; break;
    }
    
    Serial.printf("  %-12s: ", DAY_NAMES[i]);
    if (hour >= 0) {
      Serial.printf("%02d:00\n", hour);
    } else {
      Serial.println("отключено");
    }
  }
  
  Serial.println("\nПомидоры 1 🍅:");
  for (int i = 0; i < 7; i++) {
    int hour = -1;
    switch(i) {
      case 0: hour = scheduleTomato1.sun; break;
      case 1: hour = scheduleTomato1.mon; break;
      case 2: hour = scheduleTomato1.tue; break;
      case 3: hour = scheduleTomato1.wed; break;
      case 4: hour = scheduleTomato1.thu; break;
      case 5: hour = scheduleTomato1.fri; break;
      case 6: hour = scheduleTomato1.sat; break;
    }
    
    Serial.printf("  %-12s: ", DAY_NAMES[i]);
    if (hour >= 0) {
      Serial.printf("%02d:00\n", hour);
    } else {
      Serial.println("отключено");
    }
  }
  
  Serial.println("\nПомидоры 2 🍅:");
  for (int i = 0; i < 7; i++) {
    int hour = -1;
    switch(i) {
      case 0: hour = scheduleTomato2.sun; break;
      case 1: hour = scheduleTomato2.mon; break;
      case 2: hour = scheduleTomato2.tue; break;
      case 3: hour = scheduleTomato2.wed; break;
      case 4: hour = scheduleTomato2.thu; break;
      case 5: hour = scheduleTomato2.fri; break;
      case 6: hour = scheduleTomato2.sat; break;
    }
    
    Serial.printf("  %-12s: ", DAY_NAMES[i]);
    if (hour >= 0) {
      Serial.printf("%02d:00\n", hour);
    } else {
      Serial.println("отключено");
    }
  }
  Serial.println("-------------------------\n");
}

void sensorChec(){
  dump_T1 = analogRead(39);
  dump_T2 = analogRead(34);
  dump_C = analogRead(36);
}

void handleSetHumidity(String chat_id, String text) {
  int firstSpace = text.indexOf(' ');
  int secondSpace = text.indexOf(' ', firstSpace + 1);
  
  if (firstSpace == -1 || secondSpace == -1) {
    bot.sendMessage(chat_id, "❌ Формат: /sethumidity [растение] [порог]\n"
                    "Растения: cucumber, tomato1, tomato2\n"
                    "Порог: 0-4095\n"
                    "Пример: /sethumidity tomato1 2000", "");
    return;
  }
  
  String plant = text.substring(firstSpace + 1, secondSpace);
  plant.toLowerCase();
  String thresholdStr = text.substring(secondSpace + 1);
  int threshold = thresholdStr.toInt();
  
  if (threshold < 0 || threshold > 4095) {
    bot.sendMessage(chat_id, "❌ Недопустимое значение порога! Допустимый диапазон: 0-4095", "");
    return;
  }
  
  if (plant == "cucumber") {
    HUMIDITY_THRESHOLD_CUCUMBER = threshold;
    EEPROM.put(400, threshold);
    bot.sendMessage(chat_id, "✅ Порог для огурцов установлен: " + String(threshold), "");
  } 
  else if (plant == "tomato1") {
    HUMIDITY_THRESHOLD_TOMATO_1 = threshold;
    EEPROM.put(404, threshold);
    bot.sendMessage(chat_id, "✅ Порог для помидоров 1 установлен: " + String(threshold), "");
  }
  else if (plant == "tomato2") {
    HUMIDITY_THRESHOLD_TOMATO_2 = threshold;
    EEPROM.put(408, threshold);
    bot.sendMessage(chat_id, "✅ Порог для помидоров 2 установлен: " + String(threshold), "");
  }
  else {
    bot.sendMessage(chat_id, "❌ Неверное название растения! Допустимые значения: cucumber, tomato1, tomato2", "");
    return;
  }
  
  EEPROM.commit();
}

void handleSetVolume(String chat_id, String text) {
  int firstSpace = text.indexOf(' ');
  int secondSpace = text.indexOf(' ', firstSpace + 1);
  
  if (firstSpace == -1 || secondSpace == -1) {
    bot.sendMessage(chat_id, "❌ Формат: /setvolume [растение] [объем]\n"
                    "Растения: cucumber, tomato1, tomato2\n"
                    "Объем (мл): 1-10000\n"
                    "Пример: /setvolume tomato1 150", "");
    return;
  }
  
  String plant = text.substring(firstSpace + 1, secondSpace);
  plant.toLowerCase();
  String volumeStr = text.substring(secondSpace + 1);
  int volume = volumeStr.toInt();
  
  if (volume < 1 || volume > 10000) {
    bot.sendMessage(chat_id, "❌ Недопустимый объем! Допустимый диапазон: 1-10000 мл", "");
    return;
  }
  
  if (plant == "cucumber") {
    WATER_VOLUME_CUCUMBER = volume;
    EEPROM.put(412, volume);
    bot.sendMessage(chat_id, "✅ Объем для огурцов установлен: " + String(volume) + " мл", "");
  } 
  else if (plant == "tomato1") {
    WATER_VOLUME_TOMATO_1 = volume;
    EEPROM.put(416, volume);
    bot.sendMessage(chat_id, "✅ Объем для помидоров 1 установлен: " + String(volume) + " мл", "");
  }
  else if (plant == "tomato2") {
    WATER_VOLUME_TOMATO_2 = volume;
    EEPROM.put(420, volume);
    bot.sendMessage(chat_id, "✅ Объем для помидоров 2 установлен: " + String(volume) + " мл", "");
  }
  else {
    bot.sendMessage(chat_id, "❌ Неверное название растения! Допустимые значения: cucumber, tomato1, tomato2", "");
    return;
  }
  
  EEPROM.commit();
}

void resetWateringFlags() {
  for (int i = 0; i < 7; i++) {
    wateredToday[i] = false;
  }
}

void handleNewMessages(int numNewMessages) {
  for(int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    if(text == "/start") {
      String welcome = "🌱 *Привет! Я бот для управления системой автополива* 🍀\n";
      welcome += "Используйте команды:\n";
      welcome += "/settime - Установка времени ⏳\n";
      welcome += "/setschedule - Установка расписания 📅\n";
      welcome += "/commands - Все команды 📒";
      bot.sendMessage(chat_id, welcome, "Markdown");
    }
    else if(text == "/commands") {
      String commands = "🌿 *Доступные команды:*\n\n";
      commands += "/start - Начало работы 💡\n";
      commands += "/settime - Установка времени ⌛️\n";
      commands += "/time - Текущее время 🕰\n";
      commands += "/uptime - Время работы ⏱️\n";
      commands += "/sensors_chek - Опрос датчиков 🔄\n";
      commands += "/cucumbers - Вкл/выкл огурцы 🥒\n";
      commands += "/tomatos1 - Вкл/выкл помидоры 1 🍅\n";
      commands += "/tomatos2 - Вкл/выкл помидоры 2 🍅\n";
      commands += "/setmode - Смена режима 🔧\n";
      commands += "/setschedule - Установка расписания 📅\n";
      commands += "/getschedule - Показать расписание 📋\n";
      commands += "/sethumidity - Установка порога влажности 💧\n";
      commands += "/setvolume - Установка объема воды 💦";
      bot.sendMessage(chat_id, commands, "Markdown");
    }
    else if(text == "/sensors_chek") {
      digitalWrite(ledPin, HIGH);
      bot.sendMessage(chat_id, "🔄 *Проверка датчиков* 🔄", "Markdown");
      sensorChec();
      String sensorData = "Датчик T1: " + String(dump_T1) + "\n";
      sensorData += "Датчик T2: " + String(dump_T2) + "\n";
      sensorData += "Датчик C: " + String(dump_C);
      bot.sendMessage(chat_id, sensorData, "");
      digitalWrite(ledPin, LOW);
    }
    else if(text == "/cucumbers"){
      c = !c;
      digitalWrite(PIN_PUMP_CUCUMBER, c ? HIGH : LOW);
      bot.sendMessage(chat_id, c ? "🔥 *Огурцы: ВКЛ* 🥒" : "❄️ *Огурцы: ВЫКЛ* 🥒", "Markdown");
    }
    else if(text == "/tomatos1"){
      t1 = !t1;
      digitalWrite(PIN_PUMP_TOMATO_1, t1 ? HIGH : LOW);
      bot.sendMessage(chat_id, t1 ? "🔥 *Помидоры 1: ВКЛ* 🍅" : "❄️ *Помидоры 1: ВЫКЛ* 🍅", "Markdown");
    }
    else if(text == "/tomatos2"){
      t2 = !t2;
      digitalWrite(PIN_PUMP_TOMATO_2, t2 ? HIGH : LOW);
      bot.sendMessage(chat_id, t2 ? "🔥 *Помидоры 2: ВКЛ* 🍅" : "❄️ *Помидоры 2: ВЫКЛ* 🍅", "Markdown");
    }
    else if (text == "/time") {
      String message = "🕰 *Текущее время:* " + formatTime(currentTime.hours, currentTime.minutes);
      bot.sendMessage(chat_id, message, "Markdown");
    }
    else if (text == "/uptime") {
      bot.sendMessage(chat_id, "⏱️ *Время работы:* " + getUptime(), "Markdown");
    }
    else if (text.startsWith("/settime")) {
      handleSetTimeCommand(chat_id, text);
    }
    else if(text.startsWith("/setschedule")) {
      handleSetSchedule(chat_id, text);
    }
    else if(text == "/getschedule") {
      String schedule = "📅 *Расписание полива:*\n\n";
      
      schedule += "🥒 *Огурцы:*\n";
      for (int i = 0; i < 7; i++) {
        int hour = -1;
        switch(i) {
          case 0: hour = scheduleCucumber.sun; break;
          case 1: hour = scheduleCucumber.mon; break;
          case 2: hour = scheduleCucumber.tue; break;
          case 3: hour = scheduleCucumber.wed; break;
          case 4: hour = scheduleCucumber.thu; break;
          case 5: hour = scheduleCucumber.fri; break;
          case 6: hour = scheduleCucumber.sat; break;
        }
        
        schedule += "  " + String(DAY_NAMES[i]) + ": ";
        if (hour >= 0) {
          schedule += formatTime(hour, 0);
        } else {
          schedule += "отключено";
        }
        schedule += "\n";
      }
      
      schedule += "\n🍅 *Помидоры 1:*\n";
      for (int i = 0; i < 7; i++) {
        int hour = -1;
        switch(i) {
          case 0: hour = scheduleTomato1.sun; break;
          case 1: hour = scheduleTomato1.mon; break;
          case 2: hour = scheduleTomato1.tue; break;
          case 3: hour = scheduleTomato1.wed; break;
          case 4: hour = scheduleTomato1.thu; break;
          case 5: hour = scheduleTomato1.fri; break;
          case 6: hour = scheduleTomato1.sat; break;
        }
        
        schedule += "  " + String(DAY_NAMES[i]) + ": ";
        if (hour >= 0) {
          schedule += formatTime(hour, 0);
        } else {
          schedule += "отключено";
        }
        schedule += "\n";
      }
      
      schedule += "\n🍅 *Помидоры 2:*\n";
      for (int i = 0; i < 7; i++) {
        int hour = -1;
        switch(i) {
          case 0: hour = scheduleTomato2.sun; break;
          case 1: hour = scheduleTomato2.mon; break;
          case 2: hour = scheduleTomato2.tue; break;
          case 3: hour = scheduleTomato2.wed; break;
          case 4: hour = scheduleTomato2.thu; break;
          case 5: hour = scheduleTomato2.fri; break;
          case 6: hour = scheduleTomato2.sat; break;
        }
        
        schedule += "  " + String(DAY_NAMES[i]) + ": ";
        if (hour >= 0) {
          schedule += formatTime(hour, 0);
        } else {
          schedule += "отключено";
        }
        schedule += "\n";
      }
      bot.sendMessage(chat_id, schedule, "Markdown");
    }
    else if(text.startsWith("/setmode")) {
      int spacePos = text.indexOf(' ');
      if (spacePos == -1) {
        bot.sendMessage(chat_id, "🔧 *Формат:* /setmode [smart|auto]", "Markdown");
        return;
      }
      
      String modeStr = text.substring(spacePos + 1);
      modeStr.toLowerCase();
      
      if (modeStr == "smart") {
        mode = smart;
        bot.sendMessage(chat_id, "✅ *Режим изменен:* SMART 🧠", "Markdown");
      } else if (modeStr == "auto") {
        mode = automatic;
        bot.sendMessage(chat_id, "✅ *Режим изменен:* AUTOMATIC 🤖", "Markdown");
      } else {
        bot.sendMessage(chat_id, "❌ *Недопустимый режим!* Используйте smart или auto", "Markdown");
      }
    }
    else if(text.startsWith("/sethumidity")) {
      handleSetHumidity(chat_id, text);
    }
    else if(text.startsWith("/setvolume")) {
      handleSetVolume(chat_id, text);
    }
    else if(text == "/restart"){
      bot.sendMessage(chat_id, "🔄*Перезапуск not!*🔄", "Markdown");
    }else{
      bot.sendMessage(chat_id, "❌ *Неизвестная команда.* Используйте /commands для списка команд", "Markdown");
    }
  }
}

// Флаг для однократной перезагрузки
RTC_DATA_ATTR bool firstBoot = true;

void setup() {
  Serial.begin(115200);
  
  // Однократная перезагрузка при включении питания
  if (esp_reset_reason() == ESP_RST_POWERON) {
    firstBoot = false;
    Serial.println("Первая загрузка после подачи питания. Перезагрузка...");
    delay(100);
    ESP.restart();
    return;
  }
  firstBoot = false;

  Serial.println("Нормальный запуск");
  
  // Инициализация пинов
  pinMode(ledPin, OUTPUT);
  pinMode(PIN_PUMP_TOMATO_1, OUTPUT);
  digitalWrite(PIN_PUMP_TOMATO_1, LOW);
  pinMode(PIN_PUMP_TOMATO_2, OUTPUT);
  digitalWrite(PIN_PUMP_TOMATO_2, LOW);
  pinMode(PIN_PUMP_CUCUMBER, OUTPUT);
  digitalWrite(PIN_PUMP_CUCUMBER, LOW);
  pinMode(sensor_pin_1, INPUT);
  pinMode(sensor_pin_2, INPUT);
  pinMode(sensor_pin_3, INPUT);
  
  EEPROM.begin(512);
  
  int initFlagAddr = 300;
  if (EEPROM.read(initFlagAddr) != 0xAA) {
    Serial.println("Инициализация EEPROM...");
    
    for (int i = 0; i < 7; i++) {
      EEPROM.put(0 + i*sizeof(int), -1);
      EEPROM.put(100 + i*sizeof(int), -1);
      EEPROM.put(200 + i*sizeof(int), -1);
    }
    
    EEPROM.put(400, 2500);
    EEPROM.put(404, 2500);
    EEPROM.put(408, 2500);
    EEPROM.put(412, 100);
    EEPROM.put(416, 100);
    EEPROM.put(420, 100);
    
    EEPROM.write(initFlagAddr, 0xAA);
    EEPROM.commit();
  }

  EEPROM.get(400, HUMIDITY_THRESHOLD_CUCUMBER);
  EEPROM.get(404, HUMIDITY_THRESHOLD_TOMATO_1);
  EEPROM.get(408, HUMIDITY_THRESHOLD_TOMATO_2);
  EEPROM.get(412, WATER_VOLUME_CUCUMBER);
  EEPROM.get(416, WATER_VOLUME_TOMATO_1);
  EEPROM.get(420, WATER_VOLUME_TOMATO_2);
  
  for (int plant = 0; plant < 3; plant++) {
    int eepromOffset = plant * 100;
    for (int i = 0; i < 7; i++) {
      int addr = eepromOffset + i * sizeof(int);
      int value;
      EEPROM.get(addr, value);
      
      if (value < -1 || value > 23) {
        value = -1;
        EEPROM.put(addr, value);
        EEPROM.commit();
      }
      
      if (plant == 0) {
        switch(i) {
          case 0: scheduleCucumber.sun = value; break;
          case 1: scheduleCucumber.mon = value; break;
          case 2: scheduleCucumber.tue = value; break;
          case 3: scheduleCucumber.wed = value; break;
          case 4: scheduleCucumber.thu = value; break;
          case 5: scheduleCucumber.fri = value; break;
          case 6: scheduleCucumber.sat = value; break;
        }
      }
      else if (plant == 1) {
        switch(i) {
          case 0: scheduleTomato1.sun = value; break;
          case 1: scheduleTomato1.mon = value; break;
          case 2: scheduleTomato1.tue = value; break;
          case 3: scheduleTomato1.wed = value; break;
          case 4: scheduleTomato1.thu = value; break;
          case 5: scheduleTomato1.fri = value; break;
          case 6: scheduleTomato1.sat = value; break;
        }
      }
      else if (plant == 2) {
        switch(i) {
          case 0: scheduleTomato2.sun = value; break;
          case 1: scheduleTomato2.mon = value; break;
          case 2: scheduleTomato2.tue = value; break;
          case 3: scheduleTomato2.wed = value; break;
          case 4: scheduleTomato2.thu = value; break;
          case 5: scheduleTomato2.fri = value; break;
          case 6: scheduleTomato2.sat = value; break;
        }
      }
    }
  }
  
  baseTime.wday = 0;
  baseTime.hours = 0;
  baseTime.minutes = 0;
  baseMillis = millis();
  
  WiFi.begin(ssid, password);
  client.setInsecure();
  
  unsigned long wifiStart = millis();
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    if(millis() - wifiStart > wifiTimeout) {
      Serial.println("\nОшибка подключения к WiFi! Перезагрузка...");
      delay(1000);
      ESP.restart();
    }
  }
  
  Serial.println("\nWiFi подключен!");
  Serial.print("IP адрес: ");
  Serial.println(WiFi.localIP());
  
  bot.sendMessage(CHAT_ID, "🌱 *Система автополива запущена!*\n⌛ Установите время командой /settime", "Markdown");
  printSchedule();
}

void loop() {
  updateTime();
  checkWateringSchedule();
  handleWatering();
  handleScheduleWatering();

  unsigned long currentMillis = millis();

  if(currentMillis - timing >= serialInterval) {
    timing = currentMillis;
    Serial.printf("Время работы: %.1f сек.\n", currentMillis / 1000.0);
    
    if(WiFi.status() != WL_CONNECTED) {
      Serial.println("Потеряно соединение WiFi!");
    }

    sensorChec();
    Serial.print("T1: ");
    Serial.print(dump_T1);
    Serial.print(" | T2: ");
    Serial.print(dump_T2);
    Serial.print(" | C: ");
    Serial.println(dump_C);
  }
  
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  if(numNewMessages > 0) {
    handleNewMessages(numNewMessages);
  }
  
  delay(10);
}
