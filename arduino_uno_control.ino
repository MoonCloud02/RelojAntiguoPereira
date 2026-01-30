/*
 * Arduino UNO - Control de Motor Paso a Paso para Reloj de Torre con RTC DS3231
 * 
 * Este firmware controla el driver BH86 del motor paso a paso y gestiona
 * la sincronización con el módulo RTC DS3231 de alta precisión.
 * 
 * Conexiones:
 * - Pin 8  -> PUL+ (Driver BH86)
 * - Pin 9  -> DIR+ (Driver BH86)
 * - Pin 10 -> EN+ (Driver BH86)
 * - A4 (SDA) -> SDA del DS3231
 * - A5 (SCL) -> SCL del DS3231
 * - 5V -> VCC del DS3231
 * - GND -> GND del DS3231
 * 
 * Características:
 * - Sincronización automática con RTC DS3231
 * - Detección de cortes de energía
 * - Actualización automática cada minuto
 * - Compensación de posición tras reinicio
 * 
 * Autor: Miguel Angel Luna Garcia
 * Proyecto: Automatización Reloj Antiguo de Pereira
 * Fecha: Enero 2026
 */

#include <Wire.h>
#include <RTClib.h>

// Definición de pines
#define PIN_PUL 8    // Señal de pulsos al driver
#define PIN_DIR 9    // Señal de dirección
#define PIN_EN  10   // Señal de habilitación

// Constantes del motor
#define STEPS_PER_REV 200           // Pasos por revolución del motor (1.8° por paso)
#define MICROSTEPS 1                // Micropasos configurados en el driver
#define GEAR_RATIO 20               // Relación de reducción de la caja reductora
#define TOTAL_STEPS (STEPS_PER_REV * MICROSTEPS * GEAR_RATIO)  // 4000 pasos totales por revolución

// Constantes del reloj
#define STEPS_PER_HOUR (TOTAL_STEPS / 12)      // Pasos para una hora (333.33 pasos/hora)
#define STEPS_PER_MINUTE (STEPS_PER_HOUR / 60) // Pasos por minuto (5.55 pasos/min)

// Velocidad del motor
#define PULSE_DELAY_US 1000         // Microsegundos entre pulsos
#define MIN_PULSE_WIDTH_US 5        // Ancho mínimo del pulso

// Variables globales
RTC_DS3231 rtc;                     // Objeto RTC
long currentPosition = 0;           // Posición actual en pasos
int lastMinute = -1;                // Último minuto procesado
bool firstSync = true;              // Indica si es la primera sincronización
bool motorEnabled = false;          // Estado del motor

// Variables para cálculo preciso de posición
float accumulatedSteps = 0.0;       // Acumulador de pasos fraccionarios

void setup() {
  // Configurar pines como salidas
  pinMode(PIN_PUL, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_EN, OUTPUT);
  
  // Estado inicial
  digitalWrite(PIN_PUL, LOW);
  digitalWrite(PIN_DIR, LOW);
  digitalWrite(PIN_EN, HIGH);  // EN activo en BAJO, así que HIGH = deshabilitado
  
  // Inicializar comunicación serial
  Serial.begin(9600);
  Serial.println(F("Arduino UNO - Control Reloj de Torre con DS3231"));
  
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
}

void loop() {
  // Leer hora actual del RTC
  DateTime now = rtc.now();
  
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
  
  // Realizar movimiento
  moveSteps(diff);
  
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
  }
}

/*
 * Mover el motor N pasos
 * steps puede ser positivo (adelante) o negativo (atrás)
 */
void moveSteps(long steps) {
  if (steps == 0) return;
  
  // Determinar dirección
  bool forward = (steps > 0);
  digitalWrite(PIN_DIR, forward ? HIGH : LOW);
  
  // Habilitar motor
  enableMotor(true);
  
  // Realizar pasos
  long absSteps = abs(steps);
  for (long i = 0; i < absSteps; i++) {
    // Generar pulso
    digitalWrite(PIN_PUL, HIGH);
    delayMicroseconds(MIN_PULSE_WIDTH_US);
    digitalWrite(PIN_PUL, LOW);
    delayMicroseconds(PULSE_DELAY_US);
    
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
    
  } else {
    Serial.print(F("Comando desconocido: "));
    Serial.println(command);
    Serial.println(F("Comandos: SYNC, STATUS, ENABLE, DISABLE, RESET"));
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
  
  Serial.print(F("Motor: "));
  Serial.println(motorEnabled ? F("HABILITADO") : F("DESHABILITADO"));
  
  Serial.print(F("Primera sincronización: "));
  Serial.println(firstSync ? F("PENDIENTE") : F("COMPLETADA"));
  
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
