# Automatización Reloj Antiguo de Pereira

Sistema de automatización para reloj de torre histórico utilizando motor paso a paso en lazo cerrado y caja reductora planetaria de alta precisión.

## 📋 Descripción del Proyecto

Este proyecto implementa un sistema de automatización para el funcionamiento del reloj antiguo de torre, reemplazando el sistema mecánico tradicional por un sistema electromecánico controlado digitalmente. La solución garantiza precisión, confiabilidad y permite el control remoto del mecanismo.

## 🔧 Componentes Principales

### Motor Paso a Paso en Lazo Cerrado
**Modelo:** 86HBD5401-37K5Ø14EN

#### Especificaciones Eléctricas
- **Fases:** 2 fases
- **Ángulo de paso básico:** 1.8° ± 5%
- **Corriente nominal (por fase):** 6.4A
- **Resistencia (por fase) @25℃:** 0.8Ω ± 0.15Ω
- **Inductancia (por fase) @1kHz 1Vrms:** 7.3mH ± 20%
- **Torque de retención:** 12.8Nm ± 15% (113.3 lb-in)
- **Inercia del rotor:** Aprox. 4000 g·cm²
- **Peso:** Aprox. 5.6 kg

#### Especificaciones Mecánicas
- **Dimensiones:** 86mm × 86mm
- **Diámetro del eje:** Ø14mm
- **Longitud del eje:** 37mm ± 1mm

#### Encoder Integrado
- **Voltaje de entrada:** DC 5V
- **Resolución:** 1000 CPR/fase
- **Tipo de señal:** Onda cuadrada
- **Voltaje de salida:** DC 5V
- **Señales:** CHA, CHB (cuadratura)

#### Especificaciones Ambientales
- **Rango de temperatura operativa:** -20℃ ~ +50℃ (-4°F ~ 122°F)
- **Humedad relativa:** 15% RH ~ 95% RH
- **Clase de aislamiento:** B (130℃ / 266°F)
- **Elevación máxima de temperatura:** 80K
- **Resistencia dieléctrica:** 500V AC por 1 minuto
- **Resistencia de aislamiento:** 100MΩ mínimo

### Driver para Motor en Lazo Cerrado
**Modelo:** BH86

#### Características
- Control en lazo cerrado con retroalimentación por encoder
- Indicador de estado LED
- Protección contra sobrecorriente y sobrecalentamiento
- Dimensiones compactas: 150mm × 97.5mm × 50mm

#### Conexiones
- **Alimentación:** V+, V- (entrada de potencia)
- **Motor:** A+, A-, B+, B- (conexión de fases)
- **Encoder:** VCC, EGND, EA+, EA-, EB+, EB- (retroalimentación)
- **Señales de control:**
  - PUL+, PUL- (señal de pulsos/pasos)
  - DIR+, DIR- (dirección de giro)
  - EN+, EN- (habilitación del motor)
  - EX+, EX- (entrada externa)
  - ALM+, ALM- (salida de alarma)

### Caja Reductora Planetaria
**Modelo:** DLF86-L2-20-S-P2

#### Especificaciones Técnicas
- **Relación de reducción:** 20:1
- **Backlash (huelgo angular):** ≤10 arcmin
- **Eficiencia:** ≥95%
- **Torque nominal de salida:** 105 N·m
- **Torque máximo permisible:** 210 N·m
- **Velocidad nominal de entrada:** 3000 rpm
- **Velocidad máxima de entrada:** 4000 rpm
- **Nivel de ruido:** ≤60 dB
- **Clasificación de protección:** IP65 (protección contra polvo y agua)
- **Vida útil:** 20,000 horas

#### Especificaciones Mecánicas
- **Diámetro de brida:** Ø115mm
- **Diámetro de montaje:** Ø98.4mm
- **Eje de entrada:** Ø14F7 (compatible con motor)
- **Eje de salida:** Ø20h7
- **Tipo de lubricación:** Lubricación permanente
- **Fuerza radial:** 490N
- **Fuerza axial:** 460N

#### Condiciones de Operación
- **Rango de temperatura:** -15℃ ~ +80℃
- **Montaje:** 4 tornillos M5×10

#### Accesorios Incluidos
- 4 tornillos de montaje
- 1 chaveta (5×5×25mm)
- 2 tapones

### Fuente de Alimentación
**Modelo:** Fuente Conmutada 48V DC – 6.25A – 300W

#### Especificaciones
- **Voltaje de salida:** 48V DC
- **Corriente máxima:** 6.25A
- **Potencia nominal:** 300W
- **Tipo:** Fuente conmutada (Switching Power Supply)
- **Aplicación:** Alimentación del driver BH86 y motor paso a paso

#### Cálculo de Potencia Requerida
```
Potencia del motor = Corriente por fase × Voltaje × Número de fases
Potencia estimada = 6.4A × 48V × 0.7 (factor de utilización) ≈ 215W
Margen de seguridad = 300W / 215W ≈ 1.4× (adecuado)
```

### Sistema de Control

#### Arduino UNO
**Función:** Controlador principal de señales del motor

**Responsabilidades:**
- Generación de señales PUL (pulsos) para control de pasos del motor
- Control de dirección (DIR) del movimiento
- Habilitación/deshabilitación (EN) del driver
- Recepción de comandos desde ESP32 vía comunicación serial
- Gestión de la lógica de movimiento del reloj

**Conexiones al Driver BH86:**
- Pin digital → PUL+ (señal de pulsos)
- GND → PUL-
- Pin digital → DIR+ (dirección)
- GND → DIR-
- Pin digital → EN+ (habilitación)
- GND → EN-

**Comunicación Serial:**
- TX (Pin 1) → RX del ESP32
- RX (Pin 0) → TX del ESP32
- GND común

#### ESP32
**Función:** Gestión de tiempo real (RTC) y recuperación ante cortes de energía

**Responsabilidades:**
- Mantener la hora actual utilizando el RTC interno
- Guardar la hora en memoria no volátil (NVS - Non-Volatile Storage) periódicamente
- Detectar cortes de energía mediante comparación de tiempo
- Calcular el desfase temporal tras restauración de energía
- Enviar comandos de ajuste al Arduino UNO para sincronizar el reloj físico
- Proporcionar interfaz para ajuste manual de hora

**Características del RTC ESP32:**
- Reloj de tiempo real interno con bajo consumo
- Memoria NVS persistente (flash interna)
- Precisión: ±5 ppm (dependiendo del cristal)
- Mantiene hora durante modo deep sleep (con batería de respaldo opcional)

**Lógica de Recuperación:**
1. Al arrancar, leer hora guardada en NVS y hora actual del RTC
2. Calcular diferencia de tiempo durante el corte de energía
3. Convertir diferencia de tiempo a pasos del motor necesarios
4. Enviar comandos al Arduino para mover el reloj a la hora correcta
5. Actualizar hora en NVS cada minuto

## 🔌 Esquema de Conexión

### Cableado del Motor
| Cable | Color | Función |
|-------|-------|---------|
| A+    | Negro | Fase A positivo |
| A-    | Verde | Fase A negativo |
| B+    | Rojo  | Fase B positivo |
| B-    | Azul  | Fase B negativo |

### Cableado del Encoder
| Cable | Color       | Función |
|-------|-------------|---------|
| VCC   | Rojo        | Alimentación +5V |
| GND   | Negro       | Tierra |
| PA+   | Azul/Negro  | Canal A positivo |
| PA-   | Verde/Negro | Canal A negativo |
| PB+   | Azul        | Canal B positivo |
| PB-   | Verde       | Canal B negativo |

### Secuencia de Fases (Paso Completo)
Vista desde el lado de montaje:

**Sentido Horario (CW):** A+ → B+ → A- → B-  
**Sentido Antihorario (CCW):** B- → A- → B+ → A+

### Diagrama de Conexión del Sistema Completo

```
Fuente 48V DC
    │
    ├─ V+ ──────────┐
    └─ V- ──────────┼─────── GND común
                    │
              Driver BH86
                    │
    ┌───────────────┼───────────────┐
    │               │               │
Motor (A+,A-,B+,B-) │         Encoder (EA+,EA-,EB+,EB-)
                    │
              Señales Control
                    │
         ┌──────────┴──────────┐
         │                     │
    Arduino UNO           ESP32
    (PUL,DIR,EN)      (RTC + NVS)
         │                     │
         └─────────┬───────────┘
               Serial (TX/RX)
```

### Conexiones Arduino UNO ↔ Driver BH86
| Pin Arduino | Señal Driver | Función |
|-------------|--------------|---------|
| Pin 8       | PUL+         | Generación de pulsos |
| GND         | PUL-         | Tierra señal pulsos |
| Pin 9       | DIR+         | Control dirección |
| GND         | DIR-         | Tierra señal dirección |
| Pin 10      | EN+          | Habilitación motor |
| GND         | EN-          | Tierra señal enable |

### Conexiones Arduino UNO ↔ ESP32
| Arduino UNO | ESP32   | Función |
|-------------|---------|---------|
| TX (Pin 1)  | RX2 (GPIO 16) | Transmisión Arduino → ESP32 |
| RX (Pin 0)  | TX2 (GPIO 17) | Recepción ESP32 → Arduino |
| GND         | GND     | Tierra común |

### Protocolo de Comunicación Serial
**Baudrate:** 9600 bps  
**Formato:** 8N1 (8 bits de datos, sin paridad, 1 bit de parada)

**Comandos ESP32 → Arduino:**
- `MOVE:<pasos>` - Mover reloj X pasos adelante (positivo) o atrás (negativo)
- `SYNC:<hh:mm>` - Sincronizar a hora específica
- `STOP` - Detener movimiento inmediato
- `STATUS?` - Solicitar estado actual

**Respuestas Arduino → ESP32:**
- `OK:<posicion>` - Comando ejecutado, posición actual
- `MOVING` - Motor en movimiento
- `IDLE` - Motor detenido
- `ERROR:<código>` - Error en ejecución

## 🛠️ Instalación

### 1. Montaje Mecánico
1. Instalar la caja reductora en el eje del reloj utilizando los 4 tornillos M5×10 incluidos
2. Montar el motor paso a paso en la brida de entrada de la caja reductora
3. Asegurar la chaveta en el eje de entrada según las especificaciones (GB1096-79)
4. Verificar el correcto alineamiento de los ejes

### 2. Conexiones Eléctricas
1. Conectar los cables del motor al driver BH86 según la tabla de cableado
2. Conectar el encoder al puerto correspondiente del driver
3. Conectar la fuente de alimentación 48V DC a V+ y V- del driver
4. Conectar Arduino UNO al driver según tabla de conexiones (pines 8, 9, 10)
5. Conectar ESP32 al Arduino mediante comunicación serial (TX/RX)
6. Asegurar GND común entre todos los componentes

### 3. Configuración del Driver
1. Verificar los parámetros de corriente según las especificaciones del motor (6.4A)
2. Configurar el modo de subdivisión de pasos si es necesario
3. Ajustar los parámetros de lazo cerrado para optimizar la respuesta

### 4. Programación de Microcontroladores
1. **Arduino UNO:** Cargar el firmware [arduino_uno_control.ino](arduino_uno_control.ino)
   - Configurar pines de salida para PUL, DIR, EN
   - Inicializar comunicación serial a 9600 bps
   - Implementar lógica de control de pasos

2. **ESP32:** Cargar el firmware [esp32_rtc_sync.ino](esp32_rtc_sync.ino)
   - Configurar RTC interno
   - Inicializar NVS para almacenamiento persistente
   - Establecer comunicación serial con Arduino
   - Configurar rutina de sincronización post-corte

### 5. Calibración Inicial
1. Establecer posición inicial del reloj (12:00)
2. Ajustar hora en ESP32
3. Verificar movimiento correcto del motor en ambas direcciones
4. Confirmar sincronización entre hora ESP32 y posición física del reloj

## ⚙️ Cálculos de Operación

### Velocidad de Salida
Con motor a 3000 rpm (velocidad nominal):
```
Velocidad de salida = 3000 rpm / 20 = 150 rpm
```

### Torque Disponible
**Importante:** El holding torque (12.8 N·m) es el torque estático máximo. El torque dinámico disponible durante el movimiento es menor y disminuye con la velocidad.

Torque dinámico estimado a velocidad operativa (50-70% del holding torque):
```
Torque del motor (dinámico) = 12.8 N·m × 0.6 = 7.68 N·m (aproximado)
Torque de salida = 7.68 N·m × 20 × 0.95 = 145.9 N·m
```

Torque máximo disponible en arranque (con holding torque):
```
Torque de salida (estático) = 12.8 N·m × 20 × 0.95 = 243.2 N·m
```
⚠️ **Nota:** El torque de salida estático (243.2 N·m) excede el límite de la caja reductora (210 N·m). Es necesario limitar el torque del motor mediante configuración del driver o considerar las condiciones reales de operación donde el torque dinámico será menor.

### Resolución Angular
Con encoder de 1000 CPR y reducción 20:1:
```
Resolución de salida = 1000 × 4 (cuadratura) × 20 = 80,000 pasos/revolución
Resolución angular = 360° / 80,000 = 0.0045° por paso
```

## 🔐 Características de Protección

- **IP65:** Protección completa contra polvo y chorros de agua
- **Clase de aislamiento B:** Operación segura hasta 130℃
- **Alarma integrada:** Señal ALM para detección de errores
- **Lazo cerrado:** Corrección automática de pérdida de pasos
- **Protección térmica:** Prevención de sobrecalentamiento

## 📊 Mantenimiento

### Inspección Regular
- Verificar el nivel de ruido (debe mantenerse ≤60 dB)
- Inspeccionar visualmente conexiones eléctricas
- Comprobar temperatura de operación del motor y driver

### Lubricación
- La caja reductora cuenta con lubricación permanente
- No requiere relubricación durante su vida útil de 20,000 horas

### Vida Útil Estimada
- **Caja reductora:** 20,000 horas de operación continua
- **Motor:** Según uso y condiciones ambientales
- **Driver:** Vida útil extendida con ventilación adecuada

## ⚠️ Precauciones

1. **Instalación:**
   - Asegurar correcto alineamiento de ejes para evitar cargas radiales excesivas
   - Utilizar tornillos con el torque especificado

2. **Operación:**
   - No exceder los 4000 rpm de velocidad máxima de entrada
   - Mantener temperatura ambiente dentro del rango especificado
   - No superar el torque máximo permisible de 210 N·m

3. **Eléctricas:**
   - Verificar polaridad de conexiones antes de energizar
   - Asegurar tierra adecuada en el sistema
   - Proteger cables del encoder de interferencias electromagnéticas
   - La fuente de 48V DC debe tener protección contra cortocircuitos y sobrecarga
   - Mantener conexiones seriales alejadas de cables de potencia

4. **Sistema de Control:**
   - Realizar respaldo de la hora almacenada en ESP32 periódicamente
   - Verificar funcionamiento del RTC antes de puesta en marcha
   - Probar la sincronización post-corte en ambiente controlado
   - No manipular conexiones seriales con el sistema energizado

## 📝 Documentación Técnica

- [GearBox.pdf](GearBox.pdf) - Especificaciones de la caja reductora planetaria
- [MotorDriver.pdf](MotorDriver.pdf) - Especificaciones del motor y driver
- [arduino_uno_control.ino](arduino_uno_control.ino) - Firmware para Arduino UNO
- [esp32_rtc_sync.ino](esp32_rtc_sync.ino) - Firmware para ESP32 con gestión RTC

## 🏛️ Contexto Histórico

Este proyecto preserva la funcionalidad del reloj antiguo de torre de Pereira, combinando la herencia histórica con tecnología moderna para garantizar su funcionamiento preciso y confiable para las futuras generaciones.

## 📄 Licencia

Este proyecto es privado y de preservación patrimonial.

## 👤 Autor

Miguel Angel Luna Garcia - Proyecto de automatización de reloj histórico
Cristian David Alvarez Cardona - Soporte técnico y documentación

---

**Última actualización:** Enero 2026
