/*
 * Arduino UNO - Control de Motor Paso a Paso para Reloj de Torre con RTC DS3231
 * 
 * Este firmware controla el driver BH86 del motor paso a paso y gestiona
 * la sincronización con el módulo RTC DS3231 de alta precisión.
 * 
 * Conexiones:
 * - Pin 4  -> CS (MicroSD Card Adapter)
 * - Pin 7  -> Control Relé (Reflector LED)
 * - Pin 8  -> PUL+ (Driver BH86)
 * - Pin 9  -> DIR+ (Driver BH86)
 * - Pin 10 -> EN+ (Driver BH86)
 * - Pin 11 -> MOSI (MicroSD Card Adapter)
 * - Pin 12 -> MISO (MicroSD Card Adapter)
 * - Pin 13 -> SCK (MicroSD Card Adapter)
 * - A4 (SDA) -> SDA del DS3231
 * - A5 (SCL) -> SCL del DS3231
 * - 5V -> VCC del DS3231 y MicroSD
 * - GND -> GND del DS3231 y MicroSD
 * 
 * Características:
 * - Sincronización automática con RTC DS3231
 * - Detección de cortes de energía
 * - Actualización automática cada minuto
 * - Compensación de posición tras reinicio
 * - Almacenamiento persistente en tarjeta SD
 * - Control automático de iluminación (6pm-5am)
 * 
 * Autor: Miguel Angel Luna Garcia
 * Proyecto: Automatización Reloj Antiguo de Pereira
 * Fecha: Enero 2026
 */

#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>

// Definición de pines
#define PIN_CS  4    // Chip Select para módulo SD
#define PIN_RELAY 7  // Control de relé para reflector LED (Low Level Trigger: LOW=ON, HIGH=OFF)
#define PIN_PUL 8    // Señal de pulsos al driver
#define PIN_DIR 9    // Señal de dirección
#define PIN_EN  10   // Señal de habilitación

// Constantes del motor
#define STEPS_PER_HOUR 800000L      // Pasos que da el motor por hora (1 revolución completa)
#define STEPS_PER_MINUTE (STEPS_PER_HOUR / 60)  // Pasos por minuto (13,333.33 pasos/min)
#define TOTAL_STEPS (STEPS_PER_HOUR * 12)  // Pasos totales en 12 horas (9,600,000 pasos)

// Velocidad del motor
#define PULSE_DELAY_US 1000         // Microsegundos entre pulsos (calibrado para 1 rev/hora)
#define SYNC_PULSE_DELAY_US 10     // Microsegundos para sincronización rápida
#define MIN_PULSE_WIDTH_US 5        // Ancho mínimo del pulso

// Configuración de almacenamiento SD con Wear Leveling
#define POSITION_FILE_PREFIX "pos_"    // Prefijo para archivos de posición
#define POSITION_FILE_EXT ".txt"       // Extensión de archivos
#define NUM_SLOTS 100                   // Número de slots para wear leveling (0-99)

// Configuración de iluminación
#define LIGHT_ON_HOUR 18   // Hora de encendido (6pm)
#define LIGHT_OFF_HOUR 5   // Hora de apagado (5am)

// Variables globales
RTC_DS3231 rtc;                     // Objeto RTC
long currentPosition = 0;           // Posición actual en pasos
int lastMinute = -1;                // Último minuto procesado
bool firstSync = true;              // Indica si es la primera sincronización
bool motorEnabled = false;          // Estado del motor
bool relayState = false;            // Estado del relé del reflector
unsigned long lastSavedTimestamp = 0; // Timestamp de la última posición guardada
int currentSlot = 0;                // Slot actual para wear leveling (0-99)

// Variables para cálculo preciso de posición
float accumulatedSteps = 0.0;       // Acumulador de pasos fraccionarios

// Declaraciones de funciones
void moveSteps(long steps, bool useFastSpeed = false);
void performFullSync(DateTime now);
void moveOneMinute();
void enableMotor(bool enable);
void savePositionToSD();
bool loadPositionFromSD();
bool initializeSD();
void controlReflector(DateTime now);

void setup() {
  // Configurar pines como salidas
  pinMode(PIN_PUL, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_EN, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  
  // Estado inicial
  digitalWrite(PIN_PUL, LOW);
  digitalWrite(PIN_DIR, LOW);
  digitalWrite(PIN_EN, HIGH);  // EN activo en BAJO, así que HIGH = deshabilitado
  digitalWrite(PIN_RELAY, HIGH);  // Relé Low Level Trigger: HIGH = apagado
  
  // Inicializar comunicación serial
  Serial.begin(9600);
  Serial.println(F("Arduino UNO - Control Reloj de Torre con DS3231"));
  
  // Inicializar tarjeta SD
  if (!initializeSD()) {
    Serial.println(F("ERROR: No se pudo inicializar la tarjeta SD"));
    Serial.println(F("Verifique que la tarjeta esté insertada y las conexiones"));
    Serial.println(F("El sistema continuará sin almacenamiento persistente"));
  }
  
  // Inicializar I2C y RTC
  Wire.begin();
  
  if (!rtc.begin()) {
    Serial.println(F("ERROR: No se encontró el RTC DS3231"));
    Serial.println(F("Verifique las conexiones I2C (SDA=A4, SCL=A5)"));
    while (1) delay(1000);  // Detener ejecución
  }
  
  Serial.println(F("RTC DS3231 detectado correctamente"));
  
  // Verificar si el RTC perdió energía
  if (rtc.lostPower()) {
    Serial.println(F("ADVERTENCIA: El RTC perdió energía"));
    Serial.println(F("Configure la hora usando el sketch 'set_rtc_time.ino'"));
  }
  
  // Mostrar hora actual del RTC
  DateTime now = rtc.now();
  printDateTime(now);
  
  // Intentar recuperar posición desde SD
  if (loadPositionFromSD()) {
    Serial.println(F("Sistema recuperado desde última posición guardada en SD"));
    
    // Verificar coherencia con RTC
    if (lastSavedTimestamp > 0) {
      unsigned long currentTimestamp = now.unixtime();
      unsigned long elapsedSeconds = currentTimestamp - lastSavedTimestamp;
      
      // Si pasó mucho tiempo (más de 1 hora), advertir y resincronizar
      if (elapsedSeconds > 3600) {
        Serial.print(F("ADVERTENCIA: Pasaron "));
        Serial.print(elapsedSeconds / 60);
        Serial.println(F(" minutos desde el último guardado"));
        Serial.println(F("Se recomienda verificar la sincronización"));
        firstSync = true;  // Forzar resincronización completa
      } else {
        Serial.print(F("Tiempo transcurrido: "));
        Serial.print(elapsedSeconds);
        Serial.println(F(" segundos"));
      }
    }
  } else {
    Serial.println(F("No hay posición guardada, iniciando desde 0"));
  }
  
  // Leer temperatura del DS3231
  float temp = rtc.getTemperature();
  Serial.print(F("Temperatura: "));
  Serial.print(temp);
  Serial.println(F(" °C"));
  
  Serial.println(F("\nSistema inicializado"));
  Serial.println(F("El reloj se sincronizará automáticamente cada minuto"));
  Serial.println(F("\nComandos disponibles:"));
  Serial.println(F("  SYNC       - Forzar sincronización inmediata"));
  Serial.println(F("  STATUS     - Mostrar estado actual"));
  Serial.println(F("  ENABLE     - Habilitar motor"));
  Serial.println(F("  DISABLE    - Deshabilitar motor"));
  Serial.println(F("  RESET      - Restablecer posición a 12:00"));
  Serial.println(F("  LIGHT_ON   - Encender reflector manualmente"));
  Serial.println(F("  LIGHT_OFF  - Apagar reflector manualmente"));
}

void loop() {
  // Leer hora actual del RTC
  DateTime now = rtc.now();
  
  // Controlar reflector LED según horario
  controlReflector(now);
  
  // Verificar si cambió el minuto
  if (now.minute() != lastMinute) {
    lastMinute = now.minute();
    
    if (firstSync) {
      // Primera sincronización: mover a la hora actual
      performFullSync(now);
      firstSync = false;
    } else {
      // Sincronización normal: avanzar un minuto
      moveOneMinute();
    }
    
    // Mostrar información cada 5 minutos
    if (lastMinute % 5 == 0) {
      Serial.print(F("Hora RTC: "));
      printTime(now);
      Serial.print(F(" | Posición: "));
      Serial.print(currentPosition);
      Serial.print(F(" pasos | Temp: "));
      Serial.print(rtc.getTemperature());
      Serial.println(F(" °C"));
    }
  }
  
  // Procesar comandos desde Serial
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    processCommand(command, now);
  }
  
  delay(500);  // Verificar cada medio segundo
}

/*
 * Realizar sincronización completa del reloj
 */
void performFullSync(DateTime now) {
  Serial.println(F("\n=== SINCRONIZACIÓN COMPLETA ==="));
  Serial.print(F("Sincronizando a: "));
  printTime(now);
  Serial.println();
  
  // Calcular posición objetivo
  int hours12 = now.hour() % 12;  // Convertir a formato 12 horas
  if (hours12 == 0) hours12 = 12;
  
  long targetSteps = calculateStepsForTime(hours12, now.minute());
  
  Serial.print(F("Posición actual: "));
  Serial.print(currentPosition);
  Serial.print(F(" pasos | Objetivo: "));
  Serial.print(targetSteps);
  Serial.println(F(" pasos"));
  
  // Calcular diferencia (camino más corto)
  long diff = targetSteps - currentPosition;
  
  // Ajustar para tomar el camino más corto en el reloj de 12 horas
  if (diff > TOTAL_STEPS / 2) {
    diff -= TOTAL_STEPS;
  } else if (diff < -TOTAL_STEPS / 2) {
    diff += TOTAL_STEPS;
  }
  
  Serial.print(F("Moviendo "));
  Serial.print(abs(diff));
  Serial.print(F(" pasos "));
  Serial.println(diff > 0 ? F("hacia adelante") : F("hacia atrás"));
  Serial.println(F("Usando velocidad rápida para sincronización..."));
  
  // Realizar movimiento a velocidad rápida
  moveSteps(diff, true);
  
  // Guardar posición en SD
  savePositionToSD();
  
  Serial.println(F("Sincronización completa finalizada\n"));
}

/*
 * Mover el reloj un minuto
 */
void moveOneMinute() {
  // Acumular pasos fraccionarios para mayor precisión
  accumulatedSteps += STEPS_PER_MINUTE;
  
  // Extraer parte entera
  int stepsToMove = (int)accumulatedSteps;
  accumulatedSteps -= stepsToMove;
  
  if (stepsToMove > 0) {
    moveSteps(stepsToMove);
    
    // Guardar posición en SD cada minuto
    savePositionToSD();
  }
}

/*
 * Mover el motor N pasos
 * steps puede ser positivo (adelante) o negativo (atrás)
 * useFastSpeed: usar velocidad rápida para sincronización
 */
void moveSteps(long steps, bool useFastSpeed = false) {
  if (steps == 0) return;
  
  // Determinar dirección
  bool forward = (steps > 0);
  digitalWrite(PIN_DIR, forward ? HIGH : LOW);
  
  // Habilitar motor
  enableMotor(true);
  
  // Seleccionar velocidad
  int pulseDelay = useFastSpeed ? SYNC_PULSE_DELAY_US : PULSE_DELAY_US;
  
  // Realizar pasos
  long absSteps = abs(steps);
  for (long i = 0; i < absSteps; i++) {
    // Generar pulso
    digitalWrite(PIN_PUL, HIGH);
    delayMicroseconds(MIN_PULSE_WIDTH_US);
    digitalWrite(PIN_PUL, LOW);
    delayMicroseconds(pulseDelay);
    
    // Actualizar posición
    if (forward) {
      currentPosition++;
    } else {
      currentPosition--;
    }
    
    // Mantener posición dentro del rango 0 - TOTAL_STEPS
    if (currentPosition >= TOTAL_STEPS) {
      currentPosition -= TOTAL_STEPS;
    } else if (currentPosition < 0) {
      currentPosition += TOTAL_STEPS;
    }
    
    // Pequeña pausa cada 100 pasos para no saturar
    if (i % 100 == 0 && i > 0) {
      delay(10);
    }
  }
  
  // Deshabilitar motor tras movimiento
  delay(100);
  enableMotor(false);
}

/*
 * Calcular pasos necesarios para una hora específica
 */
long calculateStepsForTime(int hours, int minutes) {
  // Asegurar que hours está en formato 12 horas
  if (hours > 12) hours = hours % 12;
  if (hours == 0) hours = 12;
  
  long totalMinutes = (hours - 12) * 60 + minutes;  // Minutos desde las 12:00
  if (totalMinutes < 0) totalMinutes += 12 * 60;
  
  float steps = totalMinutes * STEPS_PER_MINUTE;
  return (long)steps;
}

/*
 * Procesar comandos desde Serial
 */
void processCommand(String command, DateTime now) {
  command.toUpperCase();
  
  if (command == "SYNC") {
    Serial.println(F("Forzando sincronización..."));
    performFullSync(now);
    
  } else if (command == "STATUS") {
    showStatus(now);
    
  } else if (command == "ENABLE") {
    enableMotor(true);
    Serial.println(F("Motor habilitado"));
    
  } else if (command == "DISABLE") {
    enableMotor(false);
    Serial.println(F("Motor deshabilitado"));
    
  } else if (command == "RESET") {
    Serial.println(F("Restableciendo posición a 12:00..."));
    currentPosition = 0;
    accumulatedSteps = 0;
    Serial.println(F("Posición restablecida. Use SYNC para sincronizar con el RTC"));
    
  } else if (command == "LIGHT_ON") {
    digitalWrite(PIN_RELAY, LOW);  // Low Level Trigger: LOW = ON
    relayState = true;
    Serial.println(F("Reflector encendido manualmente"));
    
  } else if (command == "LIGHT_OFF") {
    digitalWrite(PIN_RELAY, HIGH);  // Low Level Trigger: HIGH = OFF
    relayState = false;
    Serial.println(F("Reflector apagado manualmente"));
    
  } else {
    Serial.print(F("Comando desconocido: "));
    Serial.println(command);
    Serial.println(F("Comandos: SYNC, STATUS, ENABLE, DISABLE, RESET, LIGHT_ON, LIGHT_OFF"));
  }
}

/*
 * Mostrar estado del sistema
 */
void showStatus(DateTime now) {
  Serial.println(F("\n===== ESTADO DEL SISTEMA ====="));
  
  Serial.print(F("Hora RTC: "));
  printDateTime(now);
  
  Serial.print(F("Temperatura: "));
  Serial.print(rtc.getTemperature());
  Serial.println(F(" °C"));
  
  Serial.print(F("Posición actual: "));
  Serial.print(currentPosition);
  Serial.print(F(" pasos ("));
  int h, m;
  stepsToTime(currentPosition, h, m);
  Serial.print(h);
  Serial.print(F(":"));
  if (m < 10) Serial.print(F("0"));
  Serial.print(m);
  Serial.println(F(")"));
  
  Serial.print(F("Pasos acumulados: "));
  Serial.println(accumulatedSteps, 2);
  
  Serial.print(F("Motor EN: "));
  Serial.println(motorEnabled ? F("HABILITADO") : F("DESHABILITADO"));
  
  Serial.print(F("Reflector LED: "));
  Serial.println(relayState ? F("ENCENDIDO") : F("APAGADO"));
  
  Serial.print(F("Primera sincronización: "));
  Serial.println(firstSync ? F("PENDIENTE") : F("COMPLETADA"));
  
  Serial.print(F("Último guardado: "));
  if (lastSavedTimestamp > 0) {
    DateTime savedTime = DateTime(lastSavedTimestamp);
    printDateTime(savedTime);
    Serial.println();
  } else {
    Serial.println(F("N/A"));
  }
  
  Serial.print(F("Wear Leveling: Slot actual "));
  Serial.print(currentSlot);
  Serial.print(F("/"));
  Serial.println(NUM_SLOTS - 1);
  
  Serial.println(F("==============================\n"));
}

/*
 * Habilitar o deshabilitar motor
 */
void enableMotor(bool enable) {
  motorEnabled = enable;
  digitalWrite(PIN_EN, enable ? LOW : HIGH);  // EN activo en BAJO
}

/*
 * Inicializar tarjeta SD
 * Retorna true si se inicializó correctamente
 */
bool initializeSD() {
  Serial.print(F("Inicializando tarjeta SD..."));
  
  if (!SD.begin(PIN_CS)) {
    Serial.println(F(" FALLO"));
    return false;
  }
  
  Serial.println(F(" OK"));
  
  // Sistema de wear leveling con 100 slots
  Serial.println(F("Sistema de wear leveling inicializado (100 slots)"));
  
  return true;
}

/*
 * Guardar posición actual en tarjeta SD con Wear Leveling
 * Formato: posición,timestamp
 * Usa rotación de 100 slots para extender vida útil de la SD
 */
void savePositionToSD() {
  // Obtener timestamp actual
  DateTime now = rtc.now();
  unsigned long timestamp = now.unixtime();
  
  // Construir nombre de archivo para el slot actual
  char filename[16];
  sprintf(filename, "%s%03d%s", POSITION_FILE_PREFIX, currentSlot, POSITION_FILE_EXT);
  
  // Eliminar archivo del slot actual si existe
  if (SD.exists(filename)) {
    SD.remove(filename);
  }
  
  // Crear y escribir archivo en el slot actual
  File dataFile = SD.open(filename, FILE_WRITE);
  
  if (dataFile) {
    // Guardar posición y timestamp separados por coma
    dataFile.print(currentPosition);
    dataFile.print(",");
    dataFile.println(timestamp);
    dataFile.close();
    
    // Actualizar timestamp en memoria
    lastSavedTimestamp = timestamp;
    
    // Avanzar al siguiente slot (rotación circular 0-99)
    currentSlot = (currentSlot + 1) % NUM_SLOTS;
    
    // Mensaje de depuración cada 30 minutos
    if (lastMinute % 30 == 0) {
      Serial.print(F("Posición guardada en SD: "));
      Serial.print(currentPosition);
      Serial.print(F(" pasos @ "));
      printTime(now);
      Serial.print(F(" (slot "));
      Serial.print((currentSlot - 1 + NUM_SLOTS) % NUM_SLOTS); // Mostrar slot usado
      Serial.println(F(")"));
    }
  } else {
    Serial.println(F("ERROR: No se pudo escribir en la tarjeta SD"));
  }
}

/*
 * Cargar posición desde tarjeta SD con Wear Leveling
 * Busca el archivo más reciente entre todos los slots
 * Calcula automáticamente el siguiente slot a usar
 * Formato: posición,timestamp
 * Retorna true si se cargó correctamente
 */
bool loadPositionFromSD() {
  // Buscar el archivo más reciente entre todos los slots
  unsigned long newestTimestamp = 0;
  long newestPosition = 0;
  int newestSlot = -1;
  bool foundAny = false;
  
  Serial.println(F("Buscando última posición guardada..."));
  
  for (int slot = 0; slot < NUM_SLOTS; slot++) {
    char filename[16];
    sprintf(filename, "%s%03d%s", POSITION_FILE_PREFIX, slot, POSITION_FILE_EXT);
    
    if (SD.exists(filename)) {
      File dataFile = SD.open(filename, FILE_READ);
      
      if (dataFile) {
        String line = dataFile.readStringUntil('\n');
        dataFile.close();
        
        // Buscar la coma que separa posición y timestamp
        int commaIndex = line.indexOf(',');
        
        if (commaIndex > 0) {
          long position = line.substring(0, commaIndex).toInt();
          unsigned long timestamp = line.substring(commaIndex + 1).toInt();
          
          // Verificar si este es el más reciente
          if (timestamp > newestTimestamp) {
            newestTimestamp = timestamp;
            newestPosition = position;
            newestSlot = slot;
            foundAny = true;
          }
        }
      }
    }
  }
  
  if (foundAny) {
    currentPosition = newestPosition;
    lastSavedTimestamp = newestTimestamp;
    
    // Calcular el siguiente slot a usar (siguiente al más reciente encontrado)
    currentSlot = (newestSlot + 1) % NUM_SLOTS;
    
    Serial.print(F("Posición recuperada de SD: "));
    Serial.print(currentPosition);
    Serial.print(F(" pasos (desde slot "));
    Serial.print(newestSlot);
    Serial.print(F(", próximo slot: "));
    Serial.print(currentSlot);
    Serial.print(F(", timestamp: "));
    Serial.print(lastSavedTimestamp);
    Serial.println(F(")"));
    
    return true;
  } else {
    // No se encontró ningún archivo, empezar desde slot 0
    currentSlot = 0;
    Serial.println(F("No se encontró archivo de posición en SD"));
    Serial.println(F("Iniciando wear leveling desde slot 0"));
    return false;
  }
}

/*
 * Convertir pasos a hora
 */
void stepsToTime(long steps, int &hours, int &minutes) {
  long normalizedSteps = steps % TOTAL_STEPS;
  if (normalizedSteps < 0) normalizedSteps += TOTAL_STEPS;
  
  float totalMinutes = normalizedSteps / STEPS_PER_MINUTE;
  hours = (int)(totalMinutes / 60);
  minutes = (int)totalMinutes % 60;
  
  if (hours == 0) hours = 12;
}

/*
 * Imprimir fecha y hora completa
 */
void printDateTime(DateTime dt) {
  Serial.print(dt.year(), DEC);
  Serial.print('/');
  if (dt.month() < 10) Serial.print('0');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  if (dt.day() < 10) Serial.print('0');
  Serial.print(dt.day(), DEC);
  Serial.print(" ");
  printTime(dt);
}

/*
 * Imprimir solo la hora
 */
void printTime(DateTime dt) {
  if (dt.hour() < 10) Serial.print('0');
  Serial.print(dt.hour(), DEC);
  Serial.print(':');
  if (dt.minute() < 10) Serial.print('0');
  Serial.print(dt.minute(), DEC);
  Serial.print(':');
  if (dt.second() < 10) Serial.print('0');
  Serial.print(dt.second(), DEC);
}

/*
 * Controlar el reflector LED según horario
 * Encendido: 6pm (18:00) - Apagado: 5am (05:00)
 * Nota: Relé Low Level Trigger - LOW=ON, HIGH=OFF
 */
void controlReflector(DateTime now) {
  int currentHour = now.hour();
  bool shouldBeOn = false;
  
  // Determinar si el reflector debe estar encendido
  // Encendido de 18:00 a 23:59 y de 00:00 a 04:59
  if (currentHour >= LIGHT_ON_HOUR || currentHour < LIGHT_OFF_HOUR) {
    shouldBeOn = true;
  }
  
  // Cambiar estado solo si es necesario
  // Low Level Trigger: LOW = ON, HIGH = OFF
  if (shouldBeOn && !relayState) {
    digitalWrite(PIN_RELAY, LOW);  // Encender reflector
    relayState = true;
    Serial.print(F("\nReflector encendido automáticamente a las "));
    if (now.hour() < 10) Serial.print('0');
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    if (now.minute() < 10) Serial.print('0');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    if (now.second() < 10) Serial.print('0');
    Serial.print(now.second(), DEC);
    Serial.println();
  } else if (!shouldBeOn && relayState) {
    digitalWrite(PIN_RELAY, HIGH);  // Apagar reflector
    relayState = false;
    Serial.print(F("\nReflector apagado automáticamente a las "));
    if (now.hour() < 10) Serial.print('0');
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    if (now.minute() < 10) Serial.print('0');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    if (now.second() < 10) Serial.print('0');
    Serial.print(now.second(), DEC);
    Serial.println();
  }
}
