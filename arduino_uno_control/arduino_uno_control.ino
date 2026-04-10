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
 * - Control automático de iluminación (6pm-7am)
 * 
 * Autor: Miguel Angel Luna Garcia
 * Proyecto: Automatización Reloj Antiguo de Pereira
 * Fecha: Enero 2026
 */

#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>
#include <avr/wdt.h>

// Definición de pines
#define PIN_CS  4    // Chip Select para módulo SD
#define PIN_RELAY 7  // Control de relé para reflector LED (Low Level Trigger: LOW=ON, HIGH=OFF)
#define PIN_PUL 8    // Señal de pulsos al driver
#define PIN_DIR 9    // Señal de dirección
#define PIN_EN  10   // Señal de habilitación

// Constantes del motor
#define STEPS_PER_HOUR 800000L      // Pasos que da el motor por hora (1 revolución completa)
#define STEPS_PER_MINUTE (STEPS_PER_HOUR / 60.0f)  // Pasos por minuto (13,333.33 pasos/min)
#define TOTAL_STEPS (STEPS_PER_HOUR * 12)  // Pasos totales en 12 horas (9,600,000 pasos)

// Velocidad del motor
#define PULSE_DELAY_US 500         // Microsegundos entre pulsos
#define SYNC_PULSE_DELAY_US 10     // Microsegundos para sincronización rápida
#define MIN_PULSE_WIDTH_US 5        // Ancho mínimo del pulso

// Configuración de almacenamiento SD con Wear Leveling
#define POSITION_FILE_PREFIX "pos_"    // Prefijo para archivos de posición
#define POSITION_FILE_EXT ".txt"       // Extensión de archivos
#define NUM_SLOTS 1440                  // Número de slots para wear leveling (1 por minuto del día)

// Configuración de iluminación
#define LIGHT_ON_HOUR 18   // Hora de encendido (6pm)
#define LIGHT_OFF_HOUR 7   // Hora de apagado (7am)

// Variables globales
RTC_DS3231 rtc;                     // Objeto RTC
long currentPosition = 0;           // Posición actual en pasos
int lastMinute = -1;                // Último minuto procesado
bool firstSync = true;              // Indica si es la primera sincronización
bool motorEnabled = false;          // Estado del motor
bool relayState = false;            // Estado del relé del reflector
unsigned long lastSavedTimestamp = 0; // Timestamp de la última posición guardada
int currentSlot = 0;                // Slot actual para wear leveling (0-1439)
bool sdAvailable = false;           // Estado de la tarjeta SD

// Variables para cálculo preciso de posición
float accumulatedSteps = 0.0;       // Acumulador de pasos fraccionarios

// Variables para monitoreo de minutos perdidos
unsigned long minuteUpdateCount = 0;    // Contador de actualizaciones por minuto
unsigned long lastProcessedTimestamp = 0; // Timestamp del último minuto procesado (para detectar saltos entre horas)

// Variables para movimiento no bloqueante
#define STEPS_PER_BATCH 200           // Pasos procesados por iteración del loop
long pendingSteps = 0;                // Pasos pendientes de ejecutar
bool pendingFastSpeed = false;        // Velocidad del movimiento pendiente
// pendingOnComplete: 0=nada, 1=guardar SD, 2=guardar SD + imprimir fin de sync
uint8_t pendingOnComplete = 0;

// Declaraciones de funciones
void processPendingSteps();
void moveSteps(long steps, bool useFastSpeed = false);
void performFullSync(DateTime now);
void moveOneMinute();
void enableMotor(bool enable);
void savePositionToSD();
bool loadPositionFromSD();
bool initializeSD();
void controlReflector(DateTime now);
void processCommand(const char* command, DateTime now);

// Convierte un buffer char[] a mayúsculas in-place (reemplaza String::toUpperCase)
static void strToUpper(char* s) {
  while (*s) { if (*s >= 'a' && *s <= 'z') *s -= 32; s++; }
}

// Lee una línea del archivo SD en un buffer char[] (reemplaza String readStringUntil)
// Retorna el número de bytes leídos (sin el '\n')
static int readLineFromFile(File &f, char* buf, int maxLen) {
  int i = 0;
  while (i < maxLen - 1 && f.available()) {
    char c = f.read();
    if (c == '\n' || c == '\r') break;
    buf[i++] = c;
  }
  buf[i] = '\0';
  return i;
}

void setup() {
  wdt_disable();  // Apagar WDT durante inicialización (por si venimos de un reset del watchdog)

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
    Serial.println(F("Reiniciando en 8 segundos..."));
    wdt_enable(WDTO_8S);
    while (1);  // Watchdog reinicia el sistema
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
      if (currentTimestamp < lastSavedTimestamp) {
        // El RTC fue reiniciado a una fecha anterior al último guardado — evitar underflow
        Serial.println(F("ADVERTENCIA: Hora del RTC anterior al último guardado en SD"));
        Serial.println(F("Verifique la hora del RTC. Continuando sin compensación."));
        firstSync = false;
      } else {
        unsigned long elapsedSeconds = currentTimestamp - lastSavedTimestamp;
        long elapsedMinutes = elapsedSeconds / 60;

        Serial.print(F("Tiempo transcurrido desde último guardado: "));
        Serial.print(elapsedMinutes);
        Serial.println(F(" minutos"));

          // Si pasó tiempo significativo (más de 2 minutos), compensar automáticamente
          if (elapsedSeconds > 120) {
            Serial.print(F("ADVERTENCIA: El timestamp indica "));
            Serial.print(elapsedMinutes);
            Serial.println(F(" minutos de diferencia"));
            Serial.println(F(""));
            Serial.println(F("OPCIONES:"));
            Serial.println(F("  SYNC - Mover manecillas a la hora actual del RTC"));
            Serial.println(F("  OK   - Conservar posicion actual sin cambios"));
            Serial.println(F(""));
            Serial.println(F("El sistema NO se moverá hasta que elija una opción."));
            Serial.println(F("Si no responde en 60 segundos, continuará con opción OK"));
            firstSync = false;  // NO sincronizar automáticamente

            // Esperar comando del usuario con timeout de 60 segundos
            unsigned long waitStart = millis();
            unsigned long timeout = 60000;  // 60 segundos
            bool commandReceived = false;

            while (millis() - waitStart < timeout) {
              wdt_reset();
              if (Serial.available()) {
                char cmd[16];
                int cmdLen = Serial.readBytesUntil('\n', cmd, sizeof(cmd) - 1);
                if (cmdLen > 0 && cmd[cmdLen - 1] == '\r') cmdLen--;
                cmd[cmdLen] = '\0';
                strToUpper(cmd);

                if (strcmp(cmd, "SYNC") == 0) {
                  firstSync = true;
                  Serial.println(F("Sincronizando a hora actual..."));
                  commandReceived = true;
                  break;
                } else if (strcmp(cmd, "OK") == 0) {
                  Serial.println(F("Continuando con posición actual"));
                  firstSync = false;
                  commandReceived = true;
                  break;
                } else {
                  Serial.println(F("Comando no reconocido. Use: SYNC u OK"));
                }
              }
              delay(100);
            }

            // Si no se recibió comando, continuar con OK (opción 3)
            if (!commandReceived) {
              Serial.println(F("\nTimeout: No se recibió comando en 60 segundos"));
              Serial.println(F("Continuando con posición actual (opción OK)"));
              firstSync = false;
            }
          } else {
            Serial.println(F("Diferencia de tiempo aceptable, continuando normalmente"));
            firstSync = false;
          }
        }  // else: currentTimestamp >= lastSavedTimestamp
    }
  } else {
    Serial.println(F("No hay posición guardada, iniciando desde 0"));
  }
  
  // Leer temperatura del DS3231
  float temp = rtc.getTemperature();
  Serial.print(F("Temperatura: "));
  Serial.print(temp);
  Serial.println(F(" °C"));
  
  // Habilitar motor permanentemente para mantener posición
  enableMotor(true);
  Serial.println(F("Motor habilitado permanentemente (holding torque)"));
  
  // Activar watchdog: reinicio automático si el loop no responde en 8 segundos
  wdt_enable(WDTO_8S);

  Serial.println(F("\nSistema inicializado"));
  Serial.println(F("El reloj se sincronizará automáticamente cada minuto"));
  Serial.println(F("\nComandos disponibles:"));
  Serial.println(F("  SYNC       - Forzar sincronización inmediata con el RTC"));
  Serial.println(F("  STATUS     - Mostrar estado actual del sistema"));
  Serial.println(F("  RESET      - Restablecer posición a 12:00"));
  Serial.println(F("  LIGHT_ON   - Encender reflector manualmente"));
  Serial.println(F("  LIGHT_OFF  - Apagar reflector manualmente"));
}

void loop() {
  wdt_reset();  // Alimentar watchdog — si esto deja de ejecutarse, el sistema reinicia

  // Procesar lote de pasos pendientes (movimiento no bloqueante)
  processPendingSteps();

  // Leer hora actual del RTC
  DateTime now = rtc.now();

  // Controlar reflector LED según horario
  controlReflector(now);

  // Verificar si cambió el minuto (solo si no hay movimiento en curso)
  if (now.minute() != lastMinute && pendingSteps == 0) {
    // Detectar minutos saltados comparando timestamps completos (cubre saltos entre horas)
    bool minutesSkipped = false;
    if (lastProcessedTimestamp != 0) {
      unsigned long currentTs = now.unixtime();
      // Se espera exactamente 60 segundos entre actualizaciones (±30s de tolerancia)
      long elapsed = (long)(currentTs - lastProcessedTimestamp);
      if (elapsed > 90) {  // Más de 1.5 minutos = se saltó al menos un minuto
        long skipped = (elapsed / 60) - 1;  // Minutos saltados (el actual no cuenta)
        Serial.print(F("\nADVERTENCIA: Se saltaron "));
        Serial.print(skipped);
        Serial.println(F(" minuto(s) — resincronizando a hora actual"));
        minutesSkipped = true;
      }
    }

    lastMinute = now.minute();
    lastProcessedTimestamp = now.unixtime();
    minuteUpdateCount++;

    if (firstSync || minutesSkipped) {
      // Primera sync o minutos perdidos: corregir a la hora actual
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
      Serial.print(F(" °C | Updates: "));
      Serial.println(minuteUpdateCount);
    }
  }
  
  // Procesar comandos desde Serial (char[] estático — sin heap dinámico)
  if (Serial.available()) {
    char command[16];
    int len = Serial.readBytesUntil('\n', command, sizeof(command) - 1);
    // Quitar \r si el terminal envía CRLF
    if (len > 0 && command[len - 1] == '\r') len--;
    command[len] = '\0';
    strToUpper(command);
    processCommand(command, now);
  }
  
  // Solo pausar si no hay movimiento pendiente
  if (pendingSteps == 0) delay(500);
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
  
  if (diff == 0) {
    Serial.println(F("Posición ya correcta, no se requiere movimiento"));
    savePositionToSD();
    Serial.println(F("Sincronización completa finalizada\n"));
    return;
  }

  Serial.print(F("Moviendo "));
  Serial.print(abs(diff));
  Serial.print(F(" pasos "));
  Serial.println(diff > 0 ? F("hacia adelante") : F("hacia atrás"));
  Serial.println(F("Usando velocidad rápida para sincronización..."));

  // Programar movimiento a velocidad rápida (no bloqueante)
  moveSteps(diff, true);
  pendingOnComplete = 2;  // Al terminar: guardar SD + imprimir mensaje
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
    pendingOnComplete = 1;  // Guardar en SD al completar el movimiento
  }
}

/*
 * Procesar un lote de pasos pendientes (llamar en cada iteración del loop)
 * Mueve STEPS_PER_BATCH pasos por llamada para no bloquear el loop.
 * Al terminar ejecuta la acción indicada por pendingOnComplete.
 */
void processPendingSteps() {
  if (pendingSteps == 0) return;

  bool forward = (pendingSteps > 0);
  digitalWrite(PIN_DIR, forward ? LOW : HIGH);

  if (!motorEnabled) enableMotor(true);

  int pulseDelay = pendingFastSpeed ? SYNC_PULSE_DELAY_US : PULSE_DELAY_US;
  long batch = min(abs(pendingSteps), (long)STEPS_PER_BATCH);

  for (long i = 0; i < batch; i++) {
    digitalWrite(PIN_PUL, HIGH);
    delayMicroseconds(MIN_PULSE_WIDTH_US);
    digitalWrite(PIN_PUL, LOW);
    delayMicroseconds(pulseDelay);

    if (forward) currentPosition++;
    else         currentPosition--;

    if (currentPosition >= TOTAL_STEPS) currentPosition -= TOTAL_STEPS;
    else if (currentPosition < 0)       currentPosition += TOTAL_STEPS;
  }

  pendingSteps -= forward ? batch : -batch;

  // Movimiento completado
  if (pendingSteps == 0) {
    delay(100);  // Estabilización mecánica
    if (pendingOnComplete >= 1) {
      savePositionToSD();
    }
    if (pendingOnComplete == 2) {
      Serial.println(F("Sincronización completa finalizada\n"));
    }
    pendingOnComplete = 0;
  }
}

/*
 * Programar movimiento de N pasos (no bloqueante)
 * steps positivo = adelante, negativo = atrás
 * useFastSpeed: velocidad rápida para sincronización
 */
void moveSteps(long steps, bool useFastSpeed) {
  if (steps == 0) return;
  pendingSteps = steps;
  pendingFastSpeed = useFastSpeed;
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
  return (long)(steps + 0.5f);  // Redondear en lugar de truncar
}

/*
 * Procesar comandos desde Serial
 */
void processCommand(const char* command, DateTime now) {
  if (strcmp(command, "SYNC") == 0) {
    if (pendingSteps != 0) {
      Serial.println(F("Movimiento en curso, espere antes de sincronizar"));
      return;
    }
    Serial.println(F("Forzando sincronización..."));
    performFullSync(now);

  } else if (strcmp(command, "STATUS") == 0) {
    showStatus(now);
    
  } else if (strcmp(command, "RESET") == 0) {
    pendingSteps = 0;
    pendingOnComplete = 0;
    currentPosition = 0;
    accumulatedSteps = 0;
    savePositionToSD();
    Serial.println(F("Posición restablecida a 12:00. Use SYNC para sincronizar con el RTC"));
    
  } else if (strcmp(command, "LIGHT_ON") == 0) {
    digitalWrite(PIN_RELAY, LOW);  // Low Level Trigger: LOW = ON
    relayState = true;
    Serial.println(F("Reflector encendido manualmente"));
    
  } else if (strcmp(command, "LIGHT_OFF") == 0) {
    digitalWrite(PIN_RELAY, HIGH);  // Low Level Trigger: HIGH = OFF
    relayState = false;
    Serial.println(F("Reflector apagado manualmente"));
    
  } else {
    Serial.print(F("Comando desconocido: "));
    Serial.println(command);  // const char* — funciona directo con println
    Serial.println(F("Comandos: SYNC, STATUS, RESET, LIGHT_ON, LIGHT_OFF"));
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
  
  Serial.print(F("Actualizaciones de minuto: "));
  Serial.println(minuteUpdateCount);

  Serial.print(F("Pasos pendientes: "));
  Serial.println(pendingSteps);
  
  Serial.print(F("Último guardado: "));
  if (lastSavedTimestamp > 0) {
    DateTime savedTime = DateTime(lastSavedTimestamp);
    printDateTime(savedTime);
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
    sdAvailable = false;
    return false;
  }

  Serial.println(F(" OK"));
  sdAvailable = true;

  // Sistema de wear leveling con 1440 slots (1 por minuto del día)
  Serial.println(F("Sistema de wear leveling inicializado (1440 slots)"));

  return true;
}

/*
 * Guardar posición actual en tarjeta SD con Wear Leveling
 * Formato: posición,timestamp
 * Usa rotación de 1440 slots (1 por minuto del día) para extender vida útil de la SD
 */
void savePositionToSD() {
  // Obtener timestamp actual
  DateTime now = rtc.now();
  unsigned long timestamp = now.unixtime();
  
  // Construir nombre de archivo para el slot actual
  char filename[16];
  sprintf(filename, "%s%04d%s", POSITION_FILE_PREFIX, currentSlot, POSITION_FILE_EXT);
  
  // Intentar hasta 3 veces en caso de fallo
  bool success = false;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    wdt_reset();  // Alimentar WDT — las operaciones SD pueden tomar varios segundos
    if (attempt > 0) {
      Serial.print(F("Reintentando escritura SD (intento "));
      Serial.print(attempt + 1);
      Serial.println(F("/3)..."));
      delay(100);
    }
    
    // Reinicializar SD solo si un intento previo falló (no en el primer intento)
    if (!sdAvailable) {
      Serial.println(F("ERROR: SD no disponible. Reintentando inicialización..."));
      delay(500);
      wdt_reset();
      if (!SD.begin(PIN_CS)) continue;  // Sigue sin SD — intentar de nuevo
      sdAvailable = true;
    }
    
    // Eliminar archivo del slot actual si existe
    if (SD.exists(filename)) {
      if (!SD.remove(filename)) {
        Serial.print(F("ADVERTENCIA: No se pudo eliminar archivo existente: "));
        Serial.println(filename);
        // Continuar de todas formas, intentar sobrescribir
      }
    }
    
    // Crear y escribir archivo en el slot actual
    File dataFile = SD.open(filename, FILE_WRITE);
    
    if (dataFile) {
      // Guardar posición y timestamp separados por coma
      size_t written = 0;
      written += dataFile.print(currentPosition);
      written += dataFile.print(",");
      written += dataFile.println(timestamp);
      
      // Forzar escritura en disco
      dataFile.flush();
      dataFile.close();
      
      // Verificar que el archivo se escribió correctamente
      if (written > 0 && SD.exists(filename)) {
        // Verificar integridad leyendo el archivo
        File verifyFile = SD.open(filename, FILE_READ);
        if (verifyFile) {
          char buf[32];
          readLineFromFile(verifyFile, buf, sizeof(buf));
          verifyFile.close();

          char* comma = strchr(buf, ',');
          if (comma != NULL) {
            *comma = '\0';
            long readPosition = atol(buf);
            unsigned long readTimestamp = strtoul(comma + 1, NULL, 10);
            
            if (readPosition == currentPosition && readTimestamp == timestamp) {
              // Escritura y verificación exitosa
              lastSavedTimestamp = timestamp;
              currentSlot = (currentSlot + 1) % NUM_SLOTS;
              success = true;
              
              // Mensaje de depuración cada 30 minutos o si hubo reintentos
              if (lastMinute % 30 == 0 || attempt > 0) {
                Serial.print(F("✓ Posición guardada en SD: "));
                Serial.print(currentPosition);
                Serial.print(F(" pasos @ "));
                printTime(now);
                Serial.print(F(" (slot "));
                Serial.print((currentSlot - 1 + NUM_SLOTS) % NUM_SLOTS);
                if (attempt > 0) {
                  Serial.print(F(", intentos: "));
                  Serial.print(attempt + 1);
                }
                Serial.println(F(")"));
              }
            } else {
              Serial.println(F("ERROR: Verificación de datos fallida"));
            }
          } else {
            Serial.println(F("ERROR: Formato de datos incorrecto"));
          }
        } else {
          Serial.println(F("ERROR: No se pudo verificar archivo escrito"));
        }
      } else {
        Serial.print(F("ERROR: Escritura fallida (bytes escritos: "));
        Serial.print(written);
        Serial.println(F(")"));
      }
    } else {
      Serial.print(F("ERROR: No se pudo abrir archivo para escritura: "));
      Serial.println(filename);
      sdAvailable = false;  // Forzar reinicialización en el próximo intento
    }
  }

  if (!success) {
    sdAvailable = false;  // SD en estado incierto — reinicializar en próxima escritura
    Serial.println(F("ERROR CRÍTICO: No se pudo guardar posición en SD después de 3 intentos"));
    Serial.println(F("ACCIÓN REQUERIDA: Verificar tarjeta SD y conexiones"));
  }
}

/*
 * Cargar posición desde tarjeta SD con Wear Leveling
 * Busca el archivo más reciente entre todos los 1440 slots
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
    wdt_reset();  // El scan de 1440 slots puede superar 8s sin esto
    char filename[16];
    sprintf(filename, "%s%04d%s", POSITION_FILE_PREFIX, slot, POSITION_FILE_EXT);
    
    if (SD.exists(filename)) {
      File dataFile = SD.open(filename, FILE_READ);
      
      if (dataFile) {
        char buf[32];
        readLineFromFile(dataFile, buf, sizeof(buf));
        dataFile.close();

        // Buscar la coma que separa posición y timestamp
        char* comma = strchr(buf, ',');

        if (comma != NULL) {
          *comma = '\0';
          long position = atol(buf);
          unsigned long timestamp = strtoul(comma + 1, NULL, 10);
          
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

  long totalMinutes = (long)(normalizedSteps / STEPS_PER_MINUTE + 0.5f);  // Redondear
  hours = (int)(totalMinutes / 60);
  minutes = (int)(totalMinutes % 60);

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
  Serial.println();
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
 * Encendido: 6pm (18:00) - Apagado: 7am (07:00)
 * Nota: Relé Low Level Trigger - LOW=ON, HIGH=OFF
 */
void controlReflector(DateTime now) {
  int currentHour = now.hour();
  bool shouldBeOn = false;
  
  // Determinar si el reflector debe estar encendido
  // Encendido de 18:00 a 23:59 y de 00:00 a 06:59
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

// testSDCard() y showSDInfo() eliminados para reducir uso de flash
