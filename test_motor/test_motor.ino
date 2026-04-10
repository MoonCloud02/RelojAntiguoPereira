/*
 * Test Motor - Pruebas y Control Manual del Driver BH86
 *
 * Este sketch permite realizar pruebas del motor paso a paso y enviar
 * comandos manuales para verificar el funcionamiento del hardware.
 *
 * Conexiones:
 * - Pin 7  -> Control Relé (Reflector LED)
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
 * - SPEED [valor]  - Cambiar velocidad (100-5000 us) (ejemplo: SPEED 500)
 * - CONT FWD       - Movimiento continuo adelante
 * - CONT BWD       - Movimiento continuo atrás
 * - STOP           - Detener movimiento continuo o interrumpir movimiento
 * - REV [vueltas]  - Dar X revoluciones completas (ejemplo: REV 2)
 * - TEST           - Ejecutar secuencia de prueba
 * - STATUS         - Mostrar estado actual
 * - RESET          - Restablecer posición a 0
 * - LIGHT_ON       - Encender reflector/relé
 * - LIGHT_OFF      - Apagar reflector/relé
 * - HELP           - Mostrar ayuda
 *
 * Autor: Miguel Angel Luna Garcia
 * Proyecto: Automatización Reloj Antiguo de Pereira
 * Fecha: Enero 2026
 */

#include <Wire.h>
#include <RTClib.h>

// Definición de pines
#define PIN_RELAY 7  // Control de relé para reflector LED
#define PIN_PUL 8    // Señal de pulsos al driver
#define PIN_DIR 9    // Señal de dirección
#define PIN_EN  10   // Señal de habilitación

// Constantes del motor
// Dirección adelante = LOW en PIN_DIR (igual que arduino_uno_control)
#define STEPS_PER_REV 40000
#define MICROSTEPS 1
#define GEAR_RATIO 20
#define TOTAL_STEPS ((long)STEPS_PER_REV * MICROSTEPS * GEAR_RATIO)  // 800,000 pasos

// Máximo de revoluciones sin desbordar long (2,147,483,647 / 800,000)
#define MAX_REVOLUTIONS 2684

// Configuración de velocidad
#define DEFAULT_PULSE_DELAY_US 4419  // 1 rev/hora
#define MIN_PULSE_WIDTH_US 5
#define MIN_SPEED 10
#define MAX_SPEED 5000

// Variables globales
RTC_DS3231 rtc;
bool rtcAvailable = false;           // true si el RTC se inicializó correctamente
long currentPosition = 0;
int pulseDelay = DEFAULT_PULSE_DELAY_US;
bool motorEnabled = false;
bool relayState = false;
bool continuousMode = false;
bool continuousDirection = true;     // true = adelante
volatile bool stopRequested = false; // Solicitud de parada en movimiento bloqueante

// Convierte buffer char[] a mayúsculas in-place
static void strToUpper(char* s) {
  while (*s) { if (*s >= 'a' && *s <= 'z') *s -= 32; s++; }
}

// Declaraciones
void processCommand(char* input);
void handleMoveCommand(const char* param, bool forward);
void handleSpeedCommand(const char* param);
void handleContinuousCommand(const char* param);
void handleRevolutionCommand(const char* param);
void moveSteps(long steps);
void enableMotor(bool enable);
void runTestSequence();
void showStatus();
void printHelp();

void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_PUL, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_EN, OUTPUT);

  digitalWrite(PIN_RELAY, HIGH);  // Relé Low Level Trigger: HIGH = apagado
  digitalWrite(PIN_PUL, LOW);
  digitalWrite(PIN_DIR, LOW);
  digitalWrite(PIN_EN, HIGH);     // EN activo en BAJO, HIGH = deshabilitado

  Serial.begin(9600);
  Serial.println(F("\n========================================"));
  Serial.println(F("   TEST MOTOR - Control Manual BH86"));
  Serial.println(F("========================================"));
  Serial.println();

  Wire.begin();
  if (!rtc.begin()) {
    Serial.println(F("ADVERTENCIA: No se encontró el RTC DS3231"));
    Serial.println(F("Los timestamps de movimiento no estarán disponibles"));
  } else {
    rtcAvailable = true;
    Serial.println(F("RTC DS3231 detectado"));
  }

  Serial.println(F("Sistema inicializado y listo para pruebas"));
  Serial.println();
  printHelp();
}

void loop() {
  // Modo continuo
  if (continuousMode) {
    // Dirección: mismo convenio que arduino_uno_control (adelante = LOW)
    digitalWrite(PIN_DIR, continuousDirection ? LOW : HIGH);

    digitalWrite(PIN_PUL, HIGH);
    delayMicroseconds(MIN_PULSE_WIDTH_US);
    digitalWrite(PIN_PUL, LOW);
    delayMicroseconds(pulseDelay);

    if (continuousDirection) currentPosition++;
    else                     currentPosition--;

    if (currentPosition >= TOTAL_STEPS) currentPosition = 0;
    else if (currentPosition < 0)       currentPosition = TOTAL_STEPS - 1;
  }

  // Procesar comandos desde Serial (char[] estático — sin heap dinámico)
  if (Serial.available()) {
    char input[32];
    int len = Serial.readBytesUntil('\n', input, sizeof(input) - 1);
    if (len > 0 && input[len - 1] == '\r') len--;
    input[len] = '\0';
    if (len > 0) processCommand(input);
  }

  if (!continuousMode) delay(10);
}

/*
 * Procesar comandos recibidos
 */
void processCommand(char* input) {
  strToUpper(input);

  // Separar comando y parámetro en el primer espacio
  char* param = strchr(input, ' ');
  if (param != NULL) {
    *param = '\0';  // Terminar el string del comando
    param++;
    while (*param == ' ') param++;  // Saltar espacios extra
    if (*param == '\0') param = NULL;
  }

  char* command = input;

  if (strcmp(command, "FWD") == 0) {
    handleMoveCommand(param, true);

  } else if (strcmp(command, "BWD") == 0) {
    handleMoveCommand(param, false);

  } else if (strcmp(command, "ENABLE") == 0) {
    continuousMode = false;
    enableMotor(true);
    Serial.println(F("Motor HABILITADO"));

  } else if (strcmp(command, "DISABLE") == 0) {
    continuousMode = false;
    enableMotor(false);
    Serial.println(F("Motor DESHABILITADO"));

  } else if (strcmp(command, "SPEED") == 0) {
    handleSpeedCommand(param);

  } else if (strcmp(command, "CONT") == 0) {
    handleContinuousCommand(param);

  } else if (strcmp(command, "STOP") == 0) {
    stopRequested = true;
    continuousMode = false;
    enableMotor(false);
    Serial.println(F("DETENIDO"));

  } else if (strcmp(command, "REV") == 0) {
    handleRevolutionCommand(param);

  } else if (strcmp(command, "TEST") == 0) {
    runTestSequence();

  } else if (strcmp(command, "STATUS") == 0) {
    showStatus();

  } else if (strcmp(command, "HELP") == 0 || strcmp(command, "?") == 0) {
    printHelp();

  } else if (strcmp(command, "RESET") == 0) {
    currentPosition = 0;
    Serial.println(F("Posición restablecida a 0"));

  } else if (strcmp(command, "LIGHT_ON") == 0 || strcmp(command, "RELAY_ON") == 0) {
    digitalWrite(PIN_RELAY, LOW);
    relayState = true;
    Serial.println(F("Reflector ENCENDIDO"));

  } else if (strcmp(command, "LIGHT_OFF") == 0 || strcmp(command, "RELAY_OFF") == 0) {
    digitalWrite(PIN_RELAY, HIGH);
    relayState = false;
    Serial.println(F("Reflector APAGADO"));

  } else {
    Serial.print(F("Comando desconocido: "));
    Serial.println(command);
    Serial.println(F("Escribe HELP para ver comandos disponibles"));
  }
}

/*
 * Manejar comando de movimiento (FWD/BWD)
 */
void handleMoveCommand(const char* param, bool forward) {
  if (param == NULL || param[0] == '\0') {
    Serial.println(forward ? F("Uso: FWD [pasos]") : F("Uso: BWD [pasos]"));
    return;
  }

  long steps = atol(param);
  if (steps <= 0) {
    Serial.println(F("Numero de pasos invalido"));
    return;
  }

  Serial.print(F("Moviendo "));
  Serial.print(steps);
  Serial.print(F(" pasos hacia "));
  Serial.println(forward ? F("ADELANTE") : F("ATRAS"));

  moveSteps(forward ? steps : -steps);

  Serial.print(F("Completado. Posicion: "));
  Serial.println(currentPosition);
}

/*
 * Manejar comando de velocidad
 */
void handleSpeedCommand(const char* param) {
  if (param == NULL || param[0] == '\0') {
    Serial.print(F("Velocidad actual: "));
    Serial.print(pulseDelay);
    Serial.println(F(" us. Uso: SPEED [10-5000]"));
    return;
  }

  int newSpeed = atoi(param);
  if (newSpeed < MIN_SPEED || newSpeed > MAX_SPEED) {
    Serial.print(F("Fuera de rango ("));
    Serial.print(MIN_SPEED);
    Serial.print(F("-"));
    Serial.print(MAX_SPEED);
    Serial.println(F(" us)"));
    return;
  }

  pulseDelay = newSpeed;
  Serial.print(F("Velocidad: "));
  Serial.print(pulseDelay);
  Serial.print(F(" us ("));
  Serial.print(newSpeed < 500 ? F("RAPIDO") : newSpeed < 1000 ? F("MEDIO") : F("LENTO"));
  Serial.println(F(")"));
}

/*
 * Manejar comando continuo
 */
void handleContinuousCommand(const char* param) {
  if (param != NULL && strcmp(param, "FWD") == 0) {
    continuousDirection = true;
    continuousMode = true;
    enableMotor(true);
    Serial.println(F("Continuo ADELANTE (STOP para detener)"));

  } else if (param != NULL && strcmp(param, "BWD") == 0) {
    continuousDirection = false;
    continuousMode = true;
    enableMotor(true);
    Serial.println(F("Continuo ATRAS (STOP para detener)"));

  } else {
    Serial.println(F("Uso: CONT FWD o CONT BWD"));
  }
}

/*
 * Manejar comando de revoluciones
 */
void handleRevolutionCommand(const char* param) {
  if (param == NULL || param[0] == '\0') {
    Serial.println(F("Uso: REV [vueltas]"));
    return;
  }

  long revolutions = atol(param);
  if (revolutions <= 0 || revolutions > MAX_REVOLUTIONS) {
    Serial.print(F("Revoluciones invalidas (1-"));
    Serial.print(MAX_REVOLUTIONS);
    Serial.println(F(")"));
    return;
  }

  long steps = TOTAL_STEPS * revolutions;

  Serial.print(F("Ejecutando "));
  Serial.print(revolutions);
  Serial.print(F(" revolucion(es) ("));
  Serial.print(steps);
  Serial.println(F(" pasos) — envia STOP para interrumpir"));

  moveSteps(steps);

  if (!stopRequested) Serial.println(F("Revoluciones completadas"));
}

/*
 * Mover el motor N pasos (bloqueante).
 * Comprueba Serial cada 1000 pasos para detectar STOP.
 * Dirección: mismo convenio que arduino_uno_control (adelante = LOW en PIN_DIR).
 */
void moveSteps(long steps) {
  if (steps == 0) return;

  stopRequested = false;

  bool forward = (steps > 0);
  // Adelante = LOW, igual que arduino_uno_control.ino
  digitalWrite(PIN_DIR, forward ? LOW : HIGH);

  bool wasEnabled = motorEnabled;
  if (!motorEnabled) {
    enableMotor(true);
    delay(50);
  }

  // Imprimir hora de inicio si el RTC está disponible
  if (rtcAvailable) {
    DateTime t = rtc.now();
    Serial.print(F("  Inicio: "));
    if (t.hour() < 10) Serial.print('0');
    Serial.print(t.hour()); Serial.print(':');
    if (t.minute() < 10) Serial.print('0');
    Serial.print(t.minute()); Serial.print(':');
    if (t.second() < 10) Serial.print('0');
    Serial.println(t.second());
  }

  long absSteps = abs(steps);
  unsigned long startTime = millis();

  for (long i = 0; i < absSteps; i++) {
    // Comprobar STOP cada 1000 pasos
    if (i % 1000 == 0 && Serial.available()) {
      char cmd[8];
      int len = Serial.readBytesUntil('\n', cmd, sizeof(cmd) - 1);
      if (len > 0 && cmd[len - 1] == '\r') len--;
      cmd[len] = '\0';
      strToUpper(cmd);
      if (strcmp(cmd, "STOP") == 0) {
        stopRequested = true;
        Serial.println(F("  Movimiento INTERRUMPIDO"));
        break;
      }
    }

    digitalWrite(PIN_PUL, HIGH);
    delayMicroseconds(MIN_PULSE_WIDTH_US);
    digitalWrite(PIN_PUL, LOW);
    delayMicroseconds(pulseDelay);

    if (forward) currentPosition++;
    else         currentPosition--;

    if (currentPosition >= TOTAL_STEPS) currentPosition = 0;
    else if (currentPosition < 0)       currentPosition = TOTAL_STEPS - 1;

    // Mostrar progreso cada 10,000 pasos en movimientos largos
    if (absSteps > 10000 && i % 10000 == 0 && i > 0) {
      Serial.print(F("  Progreso: "));
      Serial.print((i * 100) / absSteps);
      Serial.println(F("%"));
    }
  }

  unsigned long duration = millis() - startTime;

  // Imprimir hora de fin si el RTC está disponible
  if (rtcAvailable) {
    DateTime t = rtc.now();
    Serial.print(F("  Fin: "));
    if (t.hour() < 10) Serial.print('0');
    Serial.print(t.hour()); Serial.print(':');
    if (t.minute() < 10) Serial.print('0');
    Serial.print(t.minute()); Serial.print(':');
    if (t.second() < 10) Serial.print('0');
    Serial.println(t.second());
  }

  if (!wasEnabled) {
    delay(50);
    enableMotor(false);
  }

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
  Serial.println(F("\n=== SECUENCIA DE PRUEBA ==="));
  Serial.println(F("Envia STOP en cualquier momento para interrumpir\n"));

  enableMotor(true);
  delay(500);

  Serial.println(F("1. 50 pasos adelante..."));
  moveSteps(50);
  if (stopRequested) goto test_end;
  delay(500);

  Serial.println(F("2. 50 pasos atras..."));
  moveSteps(-50);
  if (stopRequested) goto test_end;
  delay(500);

  Serial.println(F("3. Media revolucion adelante..."));
  moveSteps(TOTAL_STEPS / 2);
  if (stopRequested) goto test_end;
  delay(1000);

  Serial.println(F("4. Media revolucion atras..."));
  moveSteps(-(TOTAL_STEPS / 2));
  if (stopRequested) goto test_end;
  delay(500);

  Serial.println(F("5. Revolucion completa adelante..."));
  moveSteps(TOTAL_STEPS);
  if (stopRequested) goto test_end;

test_end:
  enableMotor(false);
  if (stopRequested)
    Serial.println(F("\n=== PRUEBA INTERRUMPIDA ===\n"));
  else
    Serial.println(F("\n=== PRUEBA COMPLETADA ===\n"));
}

/*
 * Mostrar estado del sistema
 */
void showStatus() {
  Serial.println(F("\n=== ESTADO ==="));

  Serial.print(F("Posicion: "));
  Serial.print(currentPosition);
  Serial.print(F(" pasos ("));
  Serial.print((currentPosition * 360.0) / TOTAL_STEPS, 1);
  Serial.println(F(" deg)"));

  Serial.print(F("Motor: "));
  Serial.println(motorEnabled ? F("HABILITADO") : F("DESHABILITADO"));

  Serial.print(F("Velocidad: "));
  Serial.print(pulseDelay);
  Serial.print(F(" us ("));
  Serial.print(pulseDelay < 500 ? F("RAPIDO") : pulseDelay < 1000 ? F("MEDIO") : F("LENTO"));
  Serial.println(F(")"));

  Serial.print(F("Modo continuo: "));
  if (continuousMode) {
    Serial.print(F("ACTIVO ("));
    Serial.print(continuousDirection ? F("ADELANTE") : F("ATRAS"));
    Serial.println(F(")"));
  } else {
    Serial.println(F("INACTIVO"));
  }

  Serial.print(F("Pasos por rev: "));
  Serial.println(TOTAL_STEPS);

  Serial.print(F("Reflector: "));
  Serial.println(relayState ? F("ENCENDIDO") : F("APAGADO"));

  Serial.print(F("RTC: "));
  Serial.println(rtcAvailable ? F("OK") : F("NO DISPONIBLE"));

  Serial.println(F("==============\n"));
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
  Serial.println(F("COMANDOS:"));
  Serial.println(F("  FWD [pasos]    - Mover adelante"));
  Serial.println(F("  BWD [pasos]    - Mover atras"));
  Serial.println(F("  REV [vueltas]  - Dar revoluciones completas"));
  Serial.println(F("  CONT FWD/BWD   - Movimiento continuo"));
  Serial.println(F("  STOP           - Detener (continuo o bloqueante)"));
  Serial.println(F("  ENABLE/DISABLE - Habilitar/deshabilitar motor"));
  Serial.println(F("  SPEED [us]     - Velocidad (10-5000 us)"));
  Serial.println(F("  STATUS         - Estado actual"));
  Serial.println(F("  TEST           - Secuencia de prueba automatica"));
  Serial.println(F("  RESET          - Posicion a 0"));
  Serial.println(F("  LIGHT_ON/OFF   - Control reflector"));
  Serial.println(F("  HELP           - Esta ayuda"));
  Serial.println(F("Ejemplos: FWD 100 | SPEED 500 | REV 2"));
  Serial.println();
}
