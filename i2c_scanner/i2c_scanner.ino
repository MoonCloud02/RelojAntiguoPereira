/*
 * I2C Scanner
 * 
 * Este sketch escanea el bus I2C y muestra las direcciones
 * de todos los dispositivos detectados.
 * 
 * El DS3231 debería aparecer en la dirección 0x68
 */

#include <Wire.h>

void setup() {
  Wire.begin();
  Serial.begin(9600);
  while (!Serial);
  
  Serial.println("\n=== I2C Scanner ===");
  Serial.println("Escaneando bus I2C...\n");
}

void loop() {
  byte error, address;
  int nDevices = 0;

  Serial.println("Iniciando escaneo...");

  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Dispositivo I2C encontrado en 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      
      // Identificar dispositivo común
      if (address == 0x68) {
        Serial.print(" (DS3231 RTC o DS1307)");
      } else if (address == 0x57) {
        Serial.print(" (EEPROM AT24C32 del DS3231)");
      }
      
      Serial.println();
      nDevices++;
    }
    else if (error == 4) {
      Serial.print("Error desconocido en dirección 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }    
  }
  
  if (nDevices == 0) {
    Serial.println("\n✗ NO se encontraron dispositivos I2C");
    Serial.println("\nVerifica:");
    Serial.println("  1. Conexiones SDA (A4) y SCL (A5)");
    Serial.println("  2. Alimentación VCC (5V) y GND");
    Serial.println("  3. Resistencias pull-up (módulos suelen tenerlas integradas)");
  } else {
    Serial.print("\n✓ Se encontraron ");
    Serial.print(nDevices);
    Serial.println(" dispositivo(s)");
  }
  
  Serial.println("\n---------------------------\n");
  delay(5000); // Esperar 5 segundos antes del siguiente escaneo
}
