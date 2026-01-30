/*
 * Configurar Hora del RTC DS3231
 * 
 * Este sketch configura la hora inicial del módulo RTC DS3231.
 * Cargue este sketch una sola vez para establecer la hora, luego
 * cargue el sketch principal 'arduino_uno_control.ino'.
 * 
 * Conexiones:
 * - A4 (SDA) -> SDA del DS3231
 * - A5 (SCL) -> SCL del DS3231
 * - 5V -> VCC del DS3231
 * - GND -> GND del DS3231
 * 
 * Instrucciones:
 * 1. Compile y cargue este sketch al Arduino
 *    (Automáticamente usará la fecha/hora de tu PC)
 * 2. Abra el Monitor Serial (9600 baudios) para verificar
 * 3. Después de confirmar, cargue el sketch principal
 * 
 * NOTA: El RTC se configurará con la hora exacta del momento
 *       en que se COMPILE el sketch (no cuando se cargue).
 *       Si hay retraso, puede cargar el sketch nuevamente.
 * 
 * Autor: Miguel Angel Luna Garcia
 * Proyecto: Automatización Reloj Antiguo de Pereira
 * Fecha: Enero 2026
 */

#include <Wire.h>
#include <RTClib.h>

RTC_DS3231 rtc;

void setup() {
  Serial.begin(9600);
  delay(2000);  // Esperar para que se abra el Monitor Serial
  
  Serial.println(F("=== Configuración del RTC DS3231 ===\n"));
  
  // Inicializar I2C y RTC
  Wire.begin();
  
  if (!rtc.begin()) {
    Serial.println(F("ERROR: No se encontró el RTC DS3231"));
    Serial.println(F("Verifique las conexiones:"));
    Serial.println(F("  - SDA del DS3231 a pin A4 del Arduino"));
    Serial.println(F("  - SCL del DS3231 a pin A5 del Arduino"));
    Serial.println(F("  - VCC del DS3231 a 5V del Arduino"));
    Serial.println(F("  - GND del DS3231 a GND del Arduino"));
    while (1) delay(1000);
  }
  
  Serial.println(F("RTC DS3231 detectado correctamente\n"));
  
  // Verificar si el RTC perdió energía
  if (rtc.lostPower()) {
    Serial.println(F("El RTC perdió energía, configurando nueva hora..."));
  } else {
    Serial.println(F("Hora actual del RTC:"));
    DateTime now = rtc.now();
    printDateTime(now);
    Serial.println();
  }
  
  // ========================================
  // CONFIGURAR FECHA Y HORA AQUÍ
  // ========================================
  
  // OPCIÓN 1 (RECOMENDADA): Usar fecha y hora de compilación (automático) 
  // Tomará la fecha y hora de tu PC al momento de compilar el sketch
  DateTime newTime(F(__DATE__), F(__TIME__));
  
  // OPCIÓN 2 (MANUAL): Descomentar la línea siguiente para configurar manualmente
  // Formato: año, mes, día, hora, minuto, segundo
  // DateTime newTime(2026, 1, 30, 14, 30, 0);
  
  // ========================================
  
  Serial.println(F("\nConfigurando nueva hora en el RTC..."));
  Serial.print(F("Nueva hora: "));
  printDateTime(newTime);
  Serial.println();
  
  rtc.adjust(newTime);
  
  Serial.println(F("\n¡Hora configurada exitosamente!"));
  
  // Verificar la hora configurada
  delay(1000);
  DateTime verificar = rtc.now();
  Serial.print(F("Verificación - Hora actual: "));
  printDateTime(verificar);
  Serial.println();
  
  // Leer y mostrar temperatura
  float temp = rtc.getTemperature();
  Serial.print(F("Temperatura del DS3231: "));
  Serial.print(temp);
  Serial.println(F(" °C"));
  
  Serial.println(F("\n=== Configuración Completa ==="));
  Serial.println(F("Ahora puede:"));
  Serial.println(F("  1. Cerrar este Monitor Serial"));
  Serial.println(F("  2. Cargar el sketch 'arduino_uno_control.ino'"));
  Serial.println(F("  3. El RTC mantendrá la hora incluso sin alimentación"));
  Serial.println(F("     gracias a la batería CR2032"));
}

void loop() {
  // Mostrar hora actual cada segundo
  static unsigned long lastPrint = 0;
  
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    
    DateTime now = rtc.now();
    Serial.print(F("\rHora: "));
    printTime(now);
    Serial.print(F("  "));
  }
}

/*
 * Imprimir fecha y hora completa
 */
void printDateTime(DateTime dt) {
  // Fecha
  Serial.print(dt.year(), DEC);
  Serial.print('/');
  if (dt.month() < 10) Serial.print('0');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  if (dt.day() < 10) Serial.print('0');
  Serial.print(dt.day(), DEC);
  
  // Día de la semana
  const char* daysOfWeek[] = {"Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado"};
  Serial.print(" (");
  Serial.print(daysOfWeek[dt.dayOfTheWeek()]);
  Serial.print(") ");
  
  // Hora
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
