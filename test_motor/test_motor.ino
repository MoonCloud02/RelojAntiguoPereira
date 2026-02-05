/*
 * Test Motor - Pruebas y Control Manual del Driver BH86
 * 
 * Este sketch permite realizar pruebas del motor paso a paso y enviar
 * comandos manuales para verificar el funcionamiento del hardware.
 * 
 * Conexiones:
 * - Pin 8  -> PUL+ (Driver BH86)
 * - Pin 9  -> DIR+ (Driver BH86)
 * - Pin 10 -> EN+ (Driver BH86)
 * - A4 (SDA) -> SDA del DS3231
 * - A5 (SCL) -> SCL del DS3231
 * 
 * Comandos disponibles:
 * - FWD [pasos]    - Mover hacia adelante X pasos (ejemplo: FWD 100)
 * - BWD [pasos]    - Mover hacia atrás X pasos (ejemplo: BWD 50)
 * - ENABLE         - Habilitar motor
 * - DISABLE        - Deshabilitar motor
 * - SPEED [valor]  - Cambiar velocidad (100-2000 us) (ejemplo: SPEED 500)
 * - CONT FWD       - Movimiento continuo adelante
 * - CONT BWD       - Movimiento continuo atrás
 * - STOP           - Detener movimiento continuo
 * - REV [vueltas]  - Dar X revoluciones completas (ejemplo: REV 2)
 * - TEST           - Ejecutar secuencia de prueba
 * - STATUS         - Mostrar estado actual
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
#define STEPS_PER_REV 40000           // Pasos por revolución del motor (1.8° por paso)
#define MICROSTEPS 1                // Micropasos configurados en el driver
#define GEAR_RATIO 20               // Relación de reducción de la caja reductora
#define TOTAL_STEPS (STEPS_PER_REV * MICROSTEPS * GEAR_RATIO)  // 800000 pasos totales

// Configuración de velocidad
#define DEFAULT_PULSE_DELAY_US 4419  // Velocidad por defecto (1 rev/hora = 3600000 ms)
#define MIN_PULSE_WIDTH_US 5         // Ancho mínimo del pulso
#define MIN_SPEED 10                 // Velocidad máxima (menor delay = más rápido)
#define MAX_SPEED 5000               // Velocidad mínima

// Variables globales
RTC_DS3231 rtc;                      // Objeto RTC
long currentPosition = 0;            // Posición actual en pasos
int pulseDelay = DEFAULT_PULSE_DELAY_US; // Delay entre pulsos
bool motorEnabled = false;           // Estado del motor
bool continuousMode = false;         // Modo de movimiento continuo
bool continuousDirection = true;     // Dirección del movimiento continuo

void setup() {
  // Configurar pines como salidas
  pinMode(PIN_PUL, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_EN, OUTPUT);
  
  // Estado inicial
  digitalWrite(PIN_PUL, LOW);
  digitalWrite(PIN_DIR, LOW);
  digitalWrite(PIN_EN, HIGH);  // EN activo en BAJO, HIGH = deshabilitado
  
  // Inicializar comunicación serial
  Serial.begin(9600);
  Serial.println(F("\n========================================"));
  Serial.println(F("   TEST MOTOR - Control Manual BH86"));
  Serial.println(F("========================================"));
  Serial.println();
  
  // Inicializar I2C y RTC
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println(F("ADVERTENCIA: No se encontró el RTC DS3231"));
    Serial.println(F("El sistema funcionará sin mostrar hora"));
  } else {
    Serial.println(F("RTC DS3231 detectado"));
  }
  
  Serial.println(F("Sistema inicializado y listo para pruebas"));
  Serial.println();
  printHelp();
}

void loop() {
  // Modo continuo
  if (continuousMode) {
    digitalWrite(PIN_DIR, continuousDirection ? HIGH : LOW);
    
    // Generar pulso
    digitalWrite(PIN_PUL, HIGH);
    delayMicroseconds(MIN_PULSE_WIDTH_US);
    digitalWrite(PIN_PUL, LOW);
    delayMicroseconds(pulseDelay);
    
    // Actualizar posición
    if (continuousDirection) {
      currentPosition++;
    } else {
      currentPosition--;
    }
    
    // Mantener posición dentro del rango
    if (currentPosition >= TOTAL_STEPS) {
      currentPosition = 0;
    } else if (currentPosition < 0) {
      currentPosition = TOTAL_STEPS - 1;
    }
  }
  
  // Procesar comandos desde Serial
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      processCommand(input);
    }
  }
  
  // Pequeño delay si no está en modo continuo
  if (!continuousMode) {
    delay(10);
  }
}

/*
 * Procesar comandos recibidos
 */
void processCommand(String input) {
  // Convertir a mayúsculas para facilitar comparación
  input.toUpperCase();
  
  // Extraer comando y parámetro
  int spaceIndex = input.indexOf(' ');
  String command;
  String param;
  
  if (spaceIndex > 0) {
    command = input.substring(0, spaceIndex);
    param = input.substring(spaceIndex + 1);
    param.trim();
  } else {
    command = input;
  }
  
  // Procesar comando
  if (command == "FWD") {
    handleMoveCommand(param, true);
    
  } else if (command == "BWD") {
    handleMoveCommand(param, false);
    
  } else if (command == "ENABLE") {
    enableMotor(true);
    Serial.println(F("✓ Motor HABILITADO"));
    
  } else if (command == "DISABLE") {
    enableMotor(false);
    Serial.println(F("✓ Motor DESHABILITADO"));
    
  } else if (command == "SPEED") {
    handleSpeedCommand(param);
    
  } else if (command == "CONT") {
    handleContinuousCommand(param);
    
  } else if (command == "STOP") {
    continuousMode = false;
    enableMotor(false);
    Serial.println(F("✓ Movimiento DETENIDO"));
    
  } else if (command == "REV") {
    handleRevolutionCommand(param);
    
  } else if (command == "TEST") {
    runTestSequence();
    
  } else if (command == "STATUS") {
    showStatus();
    
  } else if (command == "HELP" || command == "?") {
    printHelp();
    
  } else if (command == "RESET") {
    currentPosition = 0;
    Serial.println(F("✓ Posición restablecida a 0"));
    
  } else {
    Serial.print(F("✗ Comando desconocido: "));
    Serial.println(command);
    Serial.println(F("Escribe HELP para ver comandos disponibles"));
  }
}

/*
 * Manejar comando de movimiento (FWD/BWD)
 */
void handleMoveCommand(String param, bool forward) {
  if (param.length() == 0) {
    Serial.println(F("✗ Especifique número de pasos"));
    Serial.println(forward ? F("Ejemplo: FWD 100") : F("Ejemplo: BWD 100"));
    return;
  }
  
  long steps = param.toInt();
  if (steps <= 0) {
    Serial.println(F("✗ Número de pasos inválido"));
    return;
  }
  
  Serial.print(F("→ Moviendo "));
  Serial.print(steps);
  Serial.print(F(" pasos hacia "));
  Serial.println(forward ? F("ADELANTE") : F("ATRÁS"));
  
  moveSteps(forward ? steps : -steps);
  
  Serial.print(F("✓ Movimiento completado. Posición: "));
  Serial.println(currentPosition);
}

/*
 * Manejar comando de velocidad
 */
void handleSpeedCommand(String param) {
  if (param.length() == 0) {
    Serial.print(F("Velocidad actual: "));
    Serial.print(pulseDelay);
    Serial.println(F(" μs"));
    Serial.println(F("Uso: SPEED [100-2000]"));
    return;
  }
  
  int newSpeed = param.toInt();
  if (newSpeed < MIN_SPEED || newSpeed > MAX_SPEED) {
    Serial.print(F("✗ Velocidad fuera de rango ("));
    Serial.print(MIN_SPEED);
    Serial.print(F("-"));
    Serial.print(MAX_SPEED);
    Serial.println(F(" μs)"));
    return;
  }
  
  pulseDelay = newSpeed;
  Serial.print(F("✓ Velocidad establecida: "));
  Serial.print(pulseDelay);
  Serial.print(F(" μs ("));
  Serial.print(newSpeed < 500 ? F("RÁPIDO") : newSpeed < 1000 ? F("MEDIO") : F("LENTO"));
  Serial.println(F(")"));
}

/*
 * Manejar comando continuo
 */
void handleContinuousCommand(String param) {
  if (param == "FWD") {
    continuousDirection = true;
    continuousMode = true;
    enableMotor(true);
    Serial.println(F("✓ Modo CONTINUO ADELANTE activado (STOP para detener)"));
    
  } else if (param == "BWD") {
    continuousDirection = false;
    continuousMode = true;
    enableMotor(true);
    Serial.println(F("✓ Modo CONTINUO ATRÁS activado (STOP para detener)"));
    
  } else {
    Serial.println(F("✗ Uso: CONT FWD o CONT BWD"));
  }
}

/*
 * Manejar comando de revoluciones
 */
void handleRevolutionCommand(String param) {
  if (param.length() == 0) {
    Serial.println(F("✗ Especifique número de revoluciones"));
    Serial.println(F("Ejemplo: REV 2"));
    return;
  }
  
  int revolutions = param.toInt();
  if (revolutions <= 0) {
    Serial.println(F("✗ Número de revoluciones inválido"));
    return;
  }
  
  long steps = TOTAL_STEPS * revolutions;
  
  Serial.print(F("→ Ejecutando "));
  Serial.print(revolutions);
  Serial.print(F(" revolución(es) completa(s) ("));
  Serial.print(steps);
  Serial.println(F(" pasos)"));
  
  moveSteps(steps);
  
  Serial.println(F("✓ Revoluciones completadas"));
}

/*
 * Mover el motor N pasos
 */
void moveSteps(long steps) {
  if (steps == 0) return;
  
  // Determinar dirección
  bool forward = (steps > 0);
  digitalWrite(PIN_DIR, forward ? HIGH : LOW);
  
  // Habilitar motor
  bool wasEnabled = motorEnabled;
  if (!motorEnabled) {
    enableMotor(true);
    delay(50);  // Esperar estabilización
  }
  
  // Leer hora de inicio
  DateTime startDateTime = rtc.now();
  Serial.print(F("  Inicio: "));
  if (startDateTime.hour() < 10) Serial.print('0');
  Serial.print(startDateTime.hour());
  Serial.print(':');
  if (startDateTime.minute() < 10) Serial.print('0');
  Serial.print(startDateTime.minute());
  Serial.print(':');
  if (startDateTime.second() < 10) Serial.print('0');
  Serial.print(startDateTime.second());
  Serial.println();
  
  // Realizar pasos
  long absSteps = abs(steps);
  unsigned long startTime = millis();
  
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
    
    // Mantener posición dentro del rango
    if (currentPosition >= TOTAL_STEPS) {
      currentPosition = 0;
    } else if (currentPosition < 0) {
      currentPosition = TOTAL_STEPS - 1;
    }
    
    // Mostrar progreso cada 10000 pasos
    if (absSteps > 10000 && i % 10000 == 0 && i > 0) {
      Serial.print(F("  Progreso: "));
      Serial.print((i * 100) / absSteps);
      Serial.println(F("%"));
    }
  }
  
  unsigned long duration = millis() - startTime;
  
  // Leer hora final
  DateTime endDateTime = rtc.now();
  Serial.print(F("  Fin: "));
  if (endDateTime.hour() < 10) Serial.print('0');
  Serial.print(endDateTime.hour());
  Serial.print(':');
  if (endDateTime.minute() < 10) Serial.print('0');
  Serial.print(endDateTime.minute());
  Serial.print(':');
  if (endDateTime.second() < 10) Serial.print('0');
  Serial.print(endDateTime.second());
  Serial.println();
  
  // Restaurar estado del motor
  if (!wasEnabled) {
    delay(50);
    enableMotor(false);
  }
  
  // Mostrar tiempo de ejecución
  if (absSteps > 100) {
    Serial.print(F("  Tiempo: "));
    Serial.print(duration);
    Serial.println(F(" ms"));
  }
}

/*
 * Ejecutar secuencia de prueba
 */
void runTestSequence() {
  Serial.println(F("\n========== SECUENCIA DE PRUEBA =========="));
  
  // Prueba 1: Habilitar motor
  Serial.println(F("\n1. Habilitando motor..."));
  enableMotor(true);
  delay(1000);
  
  // Prueba 2: Movimientos pequeños
  Serial.println(F("\n2. Movimiento pequeño adelante (50 pasos)..."));
  moveSteps(50);
  delay(500);
  
  Serial.println(F("\n3. Movimiento pequeño atrás (50 pasos)..."));
  moveSteps(-50);
  delay(500);
  
  // Prueba 3: Media revolución
  Serial.println(F("\n4. Media revolución adelante..."));
  moveSteps(TOTAL_STEPS / 2);
  delay(1000);
  
  // Prueba 4: Regresar
  Serial.println(F("\n5. Media revolución atrás..."));
  moveSteps(-TOTAL_STEPS / 2);
  delay(500);
  
  // Prueba 5: Revoluciones a diferentes velocidades
  Serial.println(F("\n6. Revolución completa (velocidad actual)..."));
  moveSteps(TOTAL_STEPS);
  delay(1000);
  
  // Finalizar
  enableMotor(false);
  Serial.println(F("\n========== PRUEBA COMPLETADA =========="));
  Serial.println(F("✓ Todas las pruebas finalizadas correctamente\n"));
}

/*
 * Mostrar estado del sistema
 */
void showStatus() {
  Serial.println(F("\n========== ESTADO DEL SISTEMA =========="));
  
  Serial.print(F("Posición actual: "));
  Serial.print(currentPosition);
  Serial.print(F(" pasos ("));
  Serial.print((currentPosition * 360.0) / TOTAL_STEPS, 1);
  Serial.println(F("°)"));
  
  Serial.print(F("Motor: "));
  Serial.println(motorEnabled ? F("HABILITADO") : F("DESHABILITADO"));
  
  Serial.print(F("Velocidad: "));
  Serial.print(pulseDelay);
  Serial.print(F(" μs ("));
  Serial.print(pulseDelay < 500 ? F("RÁPIDO") : pulseDelay < 1000 ? F("MEDIO") : F("LENTO"));
  Serial.println(F(")"));
  
  Serial.print(F("Modo continuo: "));
  if (continuousMode) {
    Serial.print(F("ACTIVO ("));
    Serial.print(continuousDirection ? F("ADELANTE") : F("ATRÁS"));
    Serial.println(F(")"));
  } else {
    Serial.println(F("INACTIVO"));
  }
  
  Serial.print(F("Pasos totales/rev: "));
  Serial.println(TOTAL_STEPS);
  
  Serial.println(F("========================================\n"));
}

/*
 * Habilitar o deshabilitar motor
 */
void enableMotor(bool enable) {
  motorEnabled = enable;
  digitalWrite(PIN_EN, enable ? LOW : HIGH);  // EN activo en BAJO
}

/*
 * Mostrar ayuda
 */
void printHelp() {
  Serial.println(F("COMANDOS DISPONIBLES:"));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("Movimiento:"));
  Serial.println(F("  FWD [pasos]    - Mover adelante"));
  Serial.println(F("  BWD [pasos]    - Mover atrás"));
  Serial.println(F("  REV [vueltas]  - Dar revoluciones completas"));
  Serial.println(F("  CONT FWD       - Continuo adelante"));
  Serial.println(F("  CONT BWD       - Continuo atrás"));
  Serial.println(F("  STOP           - Detener continuo"));
  Serial.println();
  Serial.println(F("Control:"));
  Serial.println(F("  ENABLE         - Habilitar motor"));
  Serial.println(F("  DISABLE        - Deshabilitar motor"));
  Serial.println(F("  SPEED [μs]     - Cambiar velocidad (100-2000)"));
  Serial.println();
  Serial.println(F("Información:"));
  Serial.println(F("  STATUS         - Ver estado actual"));
  Serial.println(F("  TEST           - Ejecutar prueba automática"));
  Serial.println(F("  RESET          - Restablecer posición a 0"));
  Serial.println(F("  HELP           - Mostrar esta ayuda"));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("\nEjemplos:"));
  Serial.println(F("  FWD 100        - Avanza 100 pasos"));
  Serial.println(F("  SPEED 500      - Velocidad media"));
  Serial.println(F("  REV 2          - Da 2 vueltas completas"));
  Serial.println();
}
