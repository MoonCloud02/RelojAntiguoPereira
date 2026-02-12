// ===================================================
// SD Card Manager - Gestión de datos en tarjeta SD
// ===================================================
// Comandos disponibles:
// - LIST: Listar todos los archivos en la SD
// - READ <archivo>: Leer contenido de un archivo
// - DELETE <archivo>: Eliminar un archivo específico
// - DELETEALL: Eliminar todos los archivos de datos
// - INFO: Información sobre la tarjeta SD
// - HELP: Mostrar ayuda de comandos
// ===================================================

#include <SPI.h>
#include <SD.h>

const int chipSelect = 4;  // Pin CS de la SD (mismo que arduino_uno_control.ino)
String inputCommand = "";
bool commandComplete = false;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Esperar a que se conecte el puerto serial
  }
  
  Serial.println(F("================================="));
  Serial.println(F("SD Card Manager - Iniciando..."));
  Serial.println(F("================================="));
  
  // Inicializar tarjeta SD
  if (!SD.begin(chipSelect)) {
    Serial.println(F("ERROR: No se pudo inicializar la tarjeta SD"));
    Serial.println(F("Verifica:"));
    Serial.println(F("  - Tarjeta SD insertada correctamente"));
    Serial.println(F("  - Conexiones SPI correctas"));
    Serial.println(F("  - Pin CS configurado correctamente"));
    return;
  }
  
  Serial.println(F("Tarjeta SD inicializada correctamente"));
  Serial.println();
  printHelp();
  Serial.println();
  Serial.print(F(">> "));
}

void loop() {
  // Leer comandos desde Serial
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    
    if (inChar == '\n' || inChar == '\r') {
      if (inputCommand.length() > 0) {
        commandComplete = true;
      }
    } else {
      inputCommand += inChar;
    }
  }
  
  // Procesar comando cuando esté completo
  if (commandComplete) {
    inputCommand.trim();
    inputCommand.toUpperCase();
    
    Serial.println();
    processCommand(inputCommand);
    
    inputCommand = "";
    commandComplete = false;
    Serial.println();
    Serial.print(F(">> "));
  }
}

void processCommand(String cmd) {
  if (cmd == "LIST") {
    listFiles();
  }
  else if (cmd.startsWith("READ ")) {
    String filename = cmd.substring(5);
    filename.trim();
    readFile(filename);
  }
  else if (cmd.startsWith("DELETE ")) {
    String filename = cmd.substring(7);
    filename.trim();
    deleteFile(filename);
  }
  else if (cmd == "DELETEALL") {
    deleteAllFiles();
  }
  else if (cmd == "INFO") {
    showSDInfo();
  }
  else if (cmd == "HELP") {
    printHelp();
  }
  else if (cmd.length() > 0) {
    Serial.print(F("Comando no reconocido: "));
    Serial.println(cmd);
    Serial.println(F("Escribe HELP para ver los comandos disponibles"));
  }
}

void listFiles() {
  Serial.println(F("=== Archivos en la tarjeta SD ==="));
  
  File root = SD.open("/");
  if (!root) {
    Serial.println(F("ERROR: No se pudo abrir el directorio raíz"));
    return;
  }
  
  int fileCount = 0;
  unsigned long totalSize = 0;
  
  root.rewindDirectory();
  
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      // Ignorar archivos con nombres vacíos o inválidos
      if (name.length() > 0 && name != "." && name != "..") {
        fileCount++;
        Serial.print(F("  "));
        Serial.print(name);
        Serial.print(F(" - "));
        Serial.print(entry.size());
        Serial.println(F(" bytes"));
        totalSize += entry.size();
      }
    }
    entry.close();
  }
  
  root.close();
  
  Serial.println(F("--------------------------------"));
  Serial.print(F("Total: "));
  Serial.print(fileCount);
  Serial.print(F(" archivo(s), "));
  Serial.print(totalSize);
  Serial.println(F(" bytes"));
}

void readFile(String filename) {
  Serial.print(F("=== Leyendo archivo: "));
  Serial.print(filename);
  Serial.println(F(" ==="));
  
  File dataFile = SD.open(filename);
  
  if (!dataFile) {
    Serial.print(F("ERROR: No se pudo abrir el archivo '"));
    Serial.print(filename);
    Serial.println(F("'"));
    return;
  }
  
  Serial.println(F("--------------------------------"));
  
  int lineCount = 0;
  while (dataFile.available()) {
    String line = dataFile.readStringUntil('\n');
    lineCount++;
    Serial.print(lineCount);
    Serial.print(F(": "));
    Serial.println(line);
  }
  
  dataFile.close();
  
  Serial.println(F("--------------------------------"));
  Serial.print(F("Total: "));
  Serial.print(lineCount);
  Serial.println(F(" líneas"));
}

void deleteFile(String filename) {
  Serial.print(F("¿Eliminar archivo '"));
  Serial.print(filename);
  Serial.println(F("'?"));
  Serial.println(F("Escribe 'YES' para confirmar o cualquier otra cosa para cancelar"));
  Serial.print(F(">> "));
  
  // Esperar confirmación
  String confirmation = waitForInput();
  confirmation.trim();
  confirmation.toUpperCase();
  
  if (confirmation == "YES") {
    if (SD.exists(filename)) {
      if (SD.remove(filename)) {
        Serial.print(F("Archivo '"));
        Serial.print(filename);
        Serial.println(F("' eliminado correctamente"));
      } else {
        Serial.print(F("ERROR: No se pudo eliminar '"));
        Serial.print(filename);
        Serial.println(F("'"));
      }
    } else {
      Serial.print(F("ERROR: El archivo '"));
      Serial.print(filename);
      Serial.println(F("' no existe"));
    }
  } else {
    Serial.println(F("Operación cancelada"));
  }
}

void deleteAllFiles() {
  Serial.println(F("¡ADVERTENCIA! Esto eliminará TODOS los archivos de la SD"));
  Serial.println(F("Escribe 'DELETEALL' para confirmar o cualquier otra cosa para cancelar"));
  Serial.print(F(">> "));
  
  // Esperar confirmación
  String confirmation = waitForInput();
  confirmation.trim();
  confirmation.toUpperCase();
  
  if (confirmation == "DELETEALL") {
    Serial.println(F("Eliminando archivos pos_0000.txt a pos_1439.txt..."));
    
    int deletedCount = 0;
    char filename[16];
    
    for (int i = 0; i < 1440; i++) {
      sprintf(filename, "pos_%04d.txt", i);
      
      if (SD.exists(filename)) {
        if (SD.remove(filename)) {
          deletedCount++;
          Serial.print(F("  Eliminado: "));
          Serial.println(filename);
        } else {
          Serial.print(F("  ERROR al eliminar: "));
          Serial.println(filename);
        }
      }
      
      // Mostrar progreso cada 100 archivos
      if (i % 100 == 0 && i > 0) {
        Serial.print(F("  Progreso: "));
        Serial.print(i);
        Serial.println(F("/1440 revisados..."));
      }
    }
    
    Serial.println(F("--------------------------------"));
    Serial.print(F("Archivos eliminados: "));
    Serial.println(deletedCount);
  } else {
    Serial.println(F("Operación cancelada"));
  }
}

void showSDInfo() {
  Serial.println(F("=== Información de la tarjeta SD ==="));
  Serial.println(F("Estado: Inicializada correctamente"));
  
  // Contar archivos y espacio usado
  File root = SD.open("/");
  int fileCount = 0;
  unsigned long totalSize = 0;
  
  if (root) {
    root.rewindDirectory();
    while (true) {
      File entry = root.openNextFile();
      if (!entry) {
        break;
      }
      if (!entry.isDirectory()) {
        String name = String(entry.name());
        // Ignorar archivos con nombres vacíos o inválidos
        if (name.length() > 0 && name != "." && name != "..") {
          fileCount++;
          totalSize += entry.size();
        }
      }
      entry.close();
    }
    root.close();
  }
  
  Serial.print(F("Archivos almacenados: "));
  Serial.println(fileCount);
  Serial.print(F("Espacio usado: "));
  Serial.print(totalSize);
  Serial.println(F(" bytes"));
  Serial.print(F("                "));
  Serial.print(totalSize / 1024.0, 2);
  Serial.println(F(" KB"));
}

String waitForInput() {
  String input = "";
  bool complete = false;
  
  while (!complete) {
    while (Serial.available()) {
      char inChar = (char)Serial.read();
      
      if (inChar == '\n' || inChar == '\r') {
        if (input.length() > 0) {
          complete = true;
          Serial.println();
        }
      } else {
        input += inChar;
      }
    }
  }
  
  return input;
}

void printHelp() {
  Serial.println(F("=== Comandos disponibles ==="));
  Serial.println(F("LIST"));
  Serial.println(F("  Listar todos los archivos en la SD"));
  Serial.println();
  Serial.println(F("READ <archivo>"));
  Serial.println(F("  Leer y mostrar el contenido de un archivo"));
  Serial.println(F("  Ejemplo: READ datos.txt"));
  Serial.println();
  Serial.println(F("DELETE <archivo>"));
  Serial.println(F("  Eliminar un archivo específico"));
  Serial.println(F("  Ejemplo: DELETE datos.txt"));
  Serial.println();
  Serial.println(F("DELETEALL"));
  Serial.println(F("  Eliminar TODOS los archivos de la SD"));
  Serial.println(F("  (Requiere confirmación)"));
  Serial.println();
  Serial.println(F("INFO"));
  Serial.println(F("  Mostrar información sobre la tarjeta SD"));
  Serial.println();
  Serial.println(F("HELP"));
  Serial.println(F("  Mostrar esta ayuda"));
  Serial.println(F("============================"));
}
