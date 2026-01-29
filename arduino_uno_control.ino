/*
 * Arduino UNO - Control de Motor Paso a Paso para Reloj de Torre
 * 
 * Este firmware controla el driver BH86 del motor paso a paso mediante
 * señales PUL (pulsos), DIR (dirección) y EN (habilitación).
 * Recibe comandos desde ESP32 vía comunicación serial.
 * 
 * Conexiones:
 * - Pin 8  -> PUL+ (Driver BH86)
 * - Pin 9  -> DIR+ (Driver BH86)
 * - Pin 10 -> EN+ (Driver BH86)
 * - Pin 0 (RX) -> TX del ESP32
 * - Pin 1 (TX) -> RX del ESP32
 * 
 * Autor: Miguel Angel Luna Garcia
 * Proyecto: Automatización Reloj Antiguo de Pereira
 * Fecha: Enero 2026
 */

// Definición de pines
#define PIN_PUL 8    // Señal de pulsos al driver
#define PIN_DIR 9    // Señal de dirección
#define PIN_EN  10   // Señal de habilitación

// Constantes del motor
#define STEPS_PER_REV 200           // Pasos por revolución del motor (1.8° por paso)
#define MICROSTEPS 1                // Micropasos configurados en el driver
#define GEAR_RATIO 20               // Relación de reducción de la caja reductora
#define TOTAL_STEPS (STEPS_PER_REV * MICROSTEPS * GEAR_RATIO)  // Pasos totales por revolución de salida

// Constantes del reloj
#define STEPS_PER_HOUR (TOTAL_STEPS / 12)    // Pasos para una hora (reloj de 12 horas)
#define STEPS_PER_MINUTE (STEPS_PER_HOUR / 60)  // Pasos por minuto

// Velocidad del motor
#define PULSE_DELAY_US 1000         // Microsegundos entre pulsos (controla velocidad)
#define MIN_PULSE_WIDTH_US 5        // Ancho mínimo del pulso (según driver)

// Variables globales
long currentPosition = 0;           // Posición actual en pasos
long targetPosition = 0;            // Posición objetivo
bool isMoving = false;              // Estado de movimiento
bool motorEnabled = false;          // Estado de habilitación del motor

// Buffer para comandos seriales
String inputString = "";
bool stringComplete = false;

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
  
  // Reservar memoria para el buffer
  inputString.reserve(50);
  
  Serial.println("Arduino UNO - Control Reloj de Torre");
  Serial.println("Sistema inicializado");
  Serial.println("Esperando comandos...");
}

void loop() {
  // Procesar comandos recibidos
  if (stringComplete) {
    processCommand(inputString);
    inputString = "";
    stringComplete = false;
  }
  
  // Ejecutar movimiento si está en curso
  if (isMoving) {
    moveOneStep();
  }
}

/*
 * Evento de recepción serial
 */
void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n') {
      stringComplete = true;
    } else {
      inputString += inChar;
    }
  }
}

/*
 * Procesar comandos recibidos por serial
 * Formato de comandos:
 * - MOVE:<pasos>     -> Mover X pasos (positivo = adelante, negativo = atrás)
 * - SYNC:<hh:mm>     -> Sincronizar a hora específica
 * - STOP             -> Detener movimiento
 * - STATUS?          -> Solicitar estado
 * - ENABLE           -> Habilitar motor
 * - DISABLE          -> Deshabilitar motor
 */
void processCommand(String command) {
  command.trim();
  
  if (command.startsWith("MOVE:")) {
    // Extraer número de pasos
    long steps = command.substring(5).toInt();
    moveToRelativePosition(steps);
    
  } else if (command.startsWith("SYNC:")) {
    // Sincronizar a hora específica (formato hh:mm)
    String timeStr = command.substring(5);
    syncToTime(timeStr);
    
  } else if (command == "STOP") {
    stopMovement();
    Serial.println("OK:STOPPED");
    
  } else if (command == "STATUS?") {
    sendStatus();
    
  } else if (command == "ENABLE") {
    enableMotor(true);
    Serial.println("OK:ENABLED");
    
  } else if (command == "DISABLE") {
    enableMotor(false);
    Serial.println("OK:DISABLED");
    
  } else {
    Serial.print("ERROR:UNKNOWN_COMMAND:");
    Serial.println(command);
  }
}

/*
 * Mover a posición relativa
 */
void moveToRelativePosition(long steps) {
  targetPosition = currentPosition + steps;
  isMoving = true;
  enableMotor(true);
  
  Serial.print("OK:MOVING:");
  Serial.println(steps);
}

/*
 * Sincronizar a hora específica
 * Formato: "hh:mm"
 */
void syncToTime(String timeStr) {
  int colonIndex = timeStr.indexOf(':');
  if (colonIndex == -1) {
    Serial.println("ERROR:INVALID_TIME_FORMAT");
    return;
  }
  
  int hours = timeStr.substring(0, colonIndex).toInt();
  int minutes = timeStr.substring(colonIndex + 1).toInt();
  
  // Validar hora
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
    Serial.println("ERROR:INVALID_TIME_VALUES");
    return;
  }
  
  // Convertir a formato 12 horas
  if (hours > 12) {
    hours -= 12;
  } else if (hours == 0) {
    hours = 12;
  }
  
  // Calcular posición objetivo
  long targetSteps = (hours * STEPS_PER_HOUR) + (minutes * STEPS_PER_MINUTE);
  
  // Calcular diferencia (siempre mover hacia adelante, camino más corto)
  long diff = targetSteps - (currentPosition % TOTAL_STEPS);
  
  // Ajustar para tomar el camino más corto
  if (diff > TOTAL_STEPS / 2) {
    diff -= TOTAL_STEPS;
  } else if (diff < -TOTAL_STEPS / 2) {
    diff += TOTAL_STEPS;
  }
  
  moveToRelativePosition(diff);
  
  Serial.print("OK:SYNC:");
  Serial.print(hours);
  Serial.print(":");
  Serial.println(minutes);
}

/*
 * Detener movimiento inmediatamente
 */
void stopMovement() {
  isMoving = false;
  targetPosition = currentPosition;
  enableMotor(false);
}

/*
 * Enviar estado actual
 */
void sendStatus() {
  if (isMoving) {
    Serial.print("MOVING:");
  } else {
    Serial.print("IDLE:");
  }
  Serial.print(currentPosition);
  Serial.print(":");
  Serial.println(motorEnabled ? "ENABLED" : "DISABLED");
}

/*
 * Habilitar o deshabilitar motor
 */
void enableMotor(bool enable) {
  motorEnabled = enable;
  digitalWrite(PIN_EN, enable ? LOW : HIGH);  // EN activo en BAJO
}

/*
 * Ejecutar un paso del motor
 */
void moveOneStep() {
  if (currentPosition == targetPosition) {
    isMoving = false;
    enableMotor(false);
    Serial.print("OK:");
    Serial.println(currentPosition);
    return;
  }
  
  // Determinar dirección
  bool forward = (targetPosition > currentPosition);
  digitalWrite(PIN_DIR, forward ? HIGH : LOW);
  
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
}

/*
 * Convertir posición a hora (para debug)
 */
void positionToTime(long position, int &hours, int &minutes) {
  long normalizedPos = position % TOTAL_STEPS;
  if (normalizedPos < 0) normalizedPos += TOTAL_STEPS;
  
  hours = normalizedPos / STEPS_PER_HOUR;
  minutes = (normalizedPos % STEPS_PER_HOUR) / STEPS_PER_MINUTE;
  
  if (hours == 0) hours = 12;
}
