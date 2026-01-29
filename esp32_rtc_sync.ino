/*
 * ESP32 - Sistema RTC y Sincronización para Reloj de Torre
 * 
 * Este firmware gestiona el tiempo real usando el RTC interno del ESP32
 * y la memoria NVS para almacenamiento persistente. Detecta cortes de
 * energía y sincroniza el reloj físico al restaurarse la alimentación.
 * 
 * Características:
 * - RTC interno del ESP32 para mantener la hora
 * - Almacenamiento en NVS (Non-Volatile Storage)
 * - Detección de cortes de energía
 * - Cálculo automático de desfase temporal
 * - Comunicación con Arduino UNO para control del motor
 * 
 * Conexiones:
 * - GPIO 17 (TX2) -> RX del Arduino UNO
 * - GPIO 16 (RX2) -> TX del Arduino UNO
 * - GND -> GND común
 * 
 * Autor: Miguel Angel Luna Garcia
 * Proyecto: Automatización Reloj Antiguo de Pereira
 * Fecha: Enero 2026
 */

#include <Preferences.h>
#include <time.h>

// Comunicación serial con Arduino
#define RXD2 16
#define TXD2 17

// Objeto para almacenamiento persistente
Preferences preferences;

// Variables de tiempo
struct tm timeinfo;
time_t lastSavedTime = 0;
time_t currentTime = 0;
bool timeSyncNeeded = false;

// Constantes
#define NVS_NAMESPACE "clock"
#define NVS_KEY_TIMESTAMP "last_time"
#define NVS_KEY_INITIALIZED "initialized"
#define SAVE_INTERVAL 60000  // Guardar cada 60 segundos

// Variables de control
unsigned long lastSaveMillis = 0;
String inputString = "";

void setup() {
  // Inicializar Serial para debug
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nESP32 - Sistema RTC para Reloj de Torre");
  
  // Inicializar Serial2 para comunicación con Arduino
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial.println("Comunicación serial con Arduino inicializada");
  
  // Inicializar NVS
  preferences.begin(NVS_NAMESPACE, false);
  
  // Configurar RTC
  configTime(0, 0, "pool.ntp.org");  // UTC, se puede cambiar a servidor local
  
  // Verificar si es el primer arranque o hay corte de energía
  checkPowerLoss();
  
  Serial.println("Sistema ESP32 inicializado");
  Serial.println("Comandos disponibles:");
  Serial.println("  SETTIME <hh:mm:ss> - Establecer hora actual");
  Serial.println("  GETTIME            - Obtener hora actual");
  Serial.println("  SYNCNOW            - Forzar sincronización con reloj físico");
  Serial.println("  STATUS             - Ver estado del sistema");
}

void loop() {
  // Actualizar tiempo actual
  updateCurrentTime();
  
  // Guardar tiempo en NVS periódicamente
  if (millis() - lastSaveMillis >= SAVE_INTERVAL) {
    saveTimeToNVS();
    lastSaveMillis = millis();
  }
  
  // Procesar comandos desde Serial (debug/configuración)
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processDebugCommand(inputString);
      inputString = "";
    } else {
      inputString += c;
    }
  }
  
  // Procesar respuestas del Arduino
  if (Serial2.available()) {
    String response = Serial2.readStringUntil('\n');
    processArduinoResponse(response);
  }
  
  // Si hay sincronización pendiente, ejecutarla
  if (timeSyncNeeded) {
    performTimeSync();
    timeSyncNeeded = false;
  }
  
  delay(100);
}

/*
 * Verificar si hubo pérdida de energía y calcular desfase
 */
void checkPowerLoss() {
  bool initialized = preferences.getBool(NVS_KEY_INITIALIZED, false);
  
  if (!initialized) {
    Serial.println("Primera inicialización del sistema");
    preferences.putBool(NVS_KEY_INITIALIZED, true);
    
    // Solicitar configuración inicial de hora
    Serial.println("Por favor, configure la hora usando: SETTIME <hh:mm:ss>");
    return;
  }
  
  // Obtener última hora guardada
  lastSavedTime = preferences.getULong64(NVS_KEY_TIMESTAMP, 0);
  
  if (lastSavedTime == 0) {
    Serial.println("No hay hora guardada previamente");
    return;
  }
  
  // Obtener hora actual del RTC
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Error: No se pudo obtener hora del RTC");
    // Restaurar hora desde NVS
    struct tm temp_timeinfo;
    temp_timeinfo.tm_sec = 0;
    temp_timeinfo.tm_min = 0;
    temp_timeinfo.tm_hour = 0;
    temp_timeinfo.tm_mday = 1;
    temp_timeinfo.tm_mon = 0;
    temp_timeinfo.tm_year = 70;
    
    time_t restored_time = lastSavedTime;
    temp_timeinfo = *localtime(&restored_time);
    
    struct timeval tv;
    tv.tv_sec = restored_time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    
    Serial.println("Hora restaurada desde NVS");
    return;
  }
  
  currentTime = mktime(&timeinfo);
  
  // Calcular diferencia de tiempo
  long timeDiff = currentTime - lastSavedTime;
  
  Serial.print("Última hora guardada: ");
  Serial.println(ctime(&lastSavedTime));
  
  Serial.print("Hora actual del RTC: ");
  Serial.println(ctime(&currentTime));
  
  Serial.print("Diferencia de tiempo: ");
  Serial.print(timeDiff);
  Serial.println(" segundos");
  
  if (timeDiff > 5) {  // Si hay más de 5 segundos de diferencia
    Serial.println("¡Corte de energía detectado!");
    Serial.println("Programando sincronización del reloj físico...");
    timeSyncNeeded = true;
  } else {
    Serial.println("No se detectó corte de energía significativo");
  }
}

/*
 * Actualizar tiempo actual
 */
void updateCurrentTime() {
  if (getLocalTime(&timeinfo)) {
    currentTime = mktime(&timeinfo);
  }
}

/*
 * Guardar tiempo actual en NVS
 */
void saveTimeToNVS() {
  updateCurrentTime();
  preferences.putULong64(NVS_KEY_TIMESTAMP, currentTime);
  
  // Debug: mostrar hora guardada cada 5 minutos
  static int saveCount = 0;
  if (++saveCount % 5 == 0) {
    Serial.print("Hora guardada en NVS: ");
    Serial.println(&timeinfo, "%H:%M:%S");
  }
}

/*
 * Realizar sincronización del reloj físico
 */
void performTimeSync() {
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Error: No se pudo obtener hora para sincronización");
    return;
  }
  
  // Obtener hora y minuto actuales
  int hours = timeinfo.tm_hour;
  int minutes = timeinfo.tm_min;
  
  Serial.print("Sincronizando reloj físico a: ");
  Serial.print(hours);
  Serial.print(":");
  Serial.println(minutes);
  
  // Enviar comando de sincronización al Arduino
  Serial2.print("SYNC:");
  if (hours < 10) Serial2.print("0");
  Serial2.print(hours);
  Serial2.print(":");
  if (minutes < 10) Serial2.print("0");
  Serial2.println(minutes);
  
  Serial.println("Comando de sincronización enviado al Arduino");
}

/*
 * Procesar comandos de debug desde Serial
 */
void processDebugCommand(String command) {
  command.trim();
  
  if (command.startsWith("SETTIME ")) {
    // Formato: SETTIME hh:mm:ss
    String timeStr = command.substring(8);
    setTime(timeStr);
    
  } else if (command == "GETTIME") {
    if (getLocalTime(&timeinfo)) {
      Serial.print("Hora actual: ");
      Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
    } else {
      Serial.println("Error al obtener hora");
    }
    
  } else if (command == "SYNCNOW") {
    Serial.println("Forzando sincronización...");
    performTimeSync();
    
  } else if (command == "STATUS") {
    showStatus();
    
  } else if (command.startsWith("ARDUINO:")) {
    // Reenviar comando directamente al Arduino
    String arduinoCmd = command.substring(8);
    Serial2.println(arduinoCmd);
    Serial.print("Enviado a Arduino: ");
    Serial.println(arduinoCmd);
    
  } else {
    Serial.println("Comando desconocido");
    Serial.println("Use: SETTIME, GETTIME, SYNCNOW, STATUS, ARDUINO:<cmd>");
  }
}

/*
 * Establecer hora manualmente
 * Formato: "hh:mm:ss"
 */
void setTime(String timeStr) {
  int firstColon = timeStr.indexOf(':');
  int secondColon = timeStr.indexOf(':', firstColon + 1);
  
  if (firstColon == -1 || secondColon == -1) {
    Serial.println("Error: Formato inválido. Use hh:mm:ss");
    return;
  }
  
  int hours = timeStr.substring(0, firstColon).toInt();
  int minutes = timeStr.substring(firstColon + 1, secondColon).toInt();
  int seconds = timeStr.substring(secondColon + 1).toInt();
  
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
    Serial.println("Error: Valores fuera de rango");
    return;
  }
  
  // Configurar estructura de tiempo
  struct tm newTime;
  newTime.tm_year = 2026 - 1900;  // Año actual
  newTime.tm_mon = 0;             // Enero (0-11)
  newTime.tm_mday = 29;           // Día
  newTime.tm_hour = hours;
  newTime.tm_min = minutes;
  newTime.tm_sec = seconds;
  
  time_t t = mktime(&newTime);
  struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
  settimeofday(&tv, NULL);
  
  Serial.print("Hora establecida: ");
  Serial.print(hours);
  Serial.print(":");
  if (minutes < 10) Serial.print("0");
  Serial.print(minutes);
  Serial.print(":");
  if (seconds < 10) Serial.print("0");
  Serial.println(seconds);
  
  // Guardar inmediatamente
  saveTimeToNVS();
  
  // Sincronizar reloj físico
  Serial.println("¿Desea sincronizar el reloj físico? (envíe SYNCNOW)");
}

/*
 * Procesar respuestas del Arduino
 */
void processArduinoResponse(String response) {
  response.trim();
  Serial.print("Arduino: ");
  Serial.println(response);
  
  // Analizar respuestas específicas si es necesario
  if (response.startsWith("ERROR:")) {
    Serial.println("¡Atención! El Arduino reportó un error");
  } else if (response.startsWith("OK:")) {
    // Comando ejecutado correctamente
  }
}

/*
 * Mostrar estado del sistema
 */
void showStatus() {
  Serial.println("\n===== ESTADO DEL SISTEMA =====");
  
  if (getLocalTime(&timeinfo)) {
    Serial.print("Hora actual RTC: ");
    Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
  }
  
  time_t savedTime = preferences.getULong64(NVS_KEY_TIMESTAMP, 0);
  if (savedTime > 0) {
    Serial.print("Última hora guardada: ");
    Serial.println(ctime(&savedTime));
  }
  
  Serial.print("Tiempo desde último guardado: ");
  Serial.print((millis() - lastSaveMillis) / 1000);
  Serial.println(" segundos");
  
  Serial.print("Memoria NVS usada: ");
  Serial.print(preferences.freeEntries());
  Serial.println(" entradas libres");
  
  // Solicitar estado del Arduino
  Serial2.println("STATUS?");
  Serial.println("Estado solicitado al Arduino...");
  
  Serial.println("==============================\n");
}
