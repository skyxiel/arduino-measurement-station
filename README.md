# 01 · Arduino Electrical Measurement Station

Turns the UNO into a small multi-channel instrument. It measures **voltage, resistance, light level, temperature and potentiometer position**, shows them on the LCD, and streams CSV to a PC where a Python script logs and plots them live.

**Chain:** sensor → voltage divider → 10-bit ADC → engineering units → LCD + serial → Python → CSV + graphs

## What it demonstrates

- **Voltage dividers** used three ways: scaling a voltage down (A0), measuring an unknown resistor (A1) and reading resistive sensors (A2, A3)
- **Ratiometric measurement**: resistance is worked out from an ADC *ratio*, so it doesn't depend on the supply voltage
- **Measuring your own supply** with the ATmega328P's internal 1.1 V bandgap. USB "5 V" is often 4.7–5.1 V, and correcting for that makes absolute voltage readings noticeably more accurate
- **Oversampling** (16 readings averaged) to reduce noise
- **Thermistor linearisation** with the Beta equation
- **Calibration** against a multimeter, saved in EEPROM
- **Serial protocol + data logging** to Python

## Parts (from the kit)

| Qty | Part |
|---|---|
| 1 | Arduino UNO R3 + USB cable |
| 1 | LCD1602 |
| 5 | 10 kΩ resistors (dividers) |
| 1 | 220 Ω (LCD backlight), 1 kΩ (LCD contrast) |
| 1 | Photoresistor (LDR) |
| 1 | 10 kΩ NTC thermistor |
| 1 | 10 kΩ potentiometer |
| 1 | Push button |
| – | Any resistors to test as "Rx", a 9 V battery or AA cells to measure |

## Wiring

| Arduino | Connects to |
|---|---|
| D7, D8 | LCD RS, E |
| D9, D10, D11, D12 | LCD D4, D5, D6, D7 |
| 5V / GND | LCD VDD / VSS; RW → GND; A → 220 Ω → 5V; K → GND |
| – | LCD V0 → 1 kΩ → GND (adjust the value if the text is too faint or dark) |
| D2 | Button → GND (internal pull-up used) |
| A0 | Midpoint of probe+ → **10 kΩ** → A0 → **10 kΩ** → GND |
| A1 | Midpoint of 5V → **Rx (unknown)** → A1 → **10 kΩ** → GND |
| A2 | Midpoint of 5V → **LDR** → A2 → **10 kΩ** → GND |
| A3 | Midpoint of 5V → **thermistor** → A3 → **10 kΩ** → GND |
| A4 | Potentiometer wiper (outer legs to 5V and GND) |

```
 Voltage probe (A0)          Unknown resistor (A1)
 probe+ ──[10k]──┬── A0        5V ──[ Rx ]──┬── A1
                 [10k]                       [10k]
 probe- ── GND ──┘            GND ──────────┘
```

> ⚠️ **Only measure low voltages (0–10 V DC)** with the 10k/10k divider, and connect probe– to Arduino GND. Never connect it to mains. To go higher, use a 100 kΩ top resistor and change `R_TOP`. The maximum is then about 55 V, but keep to battery-level voltages.

## Running it

1. Open `measurement_station/measurement_station.ino` in the Arduino IDE and upload it. It needs the **LiquidCrystal** library (install it from the Library Manager if it is missing) and the built-in `EEPROM` library.
2. Short-press the button to change the LCD page. Hold it for 1 s to reset min/max.
3. Log and plot on the PC:

```bash
pip install -r tools/requirements.txt
python tools/serial_logger.py --list
python tools/serial_logger.py COM3 --cols voltage_V temp_C
```

The CSV is saved in `logs/`. To plot a saved log later:

```bash
python tools/plot_log.py logs/20260922_101500.csv
```

### Calibration (recommended)

1. Measure the UNO's 5V pin with a multimeter. The reported `vcc_V` is the first CSV column after time. Type `bg <1.1 × meter ÷ reported>`, e.g. `bg 1.085`. This corrects every reading.
2. Put a known voltage on the probe (e.g. a 9 V battery), read it with the meter and type `cal 9.12`, using your meter reading.

Both values are stored in EEPROM and survive power cycles.

## Experiments to write up

1. **Accuracy study.** Measure 10 resistors (100 Ω → 1 MΩ) with the station and a multimeter, then plot the % error against resistance. Explain why error is worst at the extremes: with a 10 kΩ reference the ADC ratio becomes tiny, so quantisation error dominates. *Extension:* add a second reference resistor and switch between them automatically.
2. **Battery discharge curve.** Log a AA cell under a 100 Ω load for a few hours (`rate 5000`), then plot voltage against time.
3. **Thermistor vs. reality.** Put the thermistor and a thermometer in a cup of warm water as it cools. Compare the Beta equation with a Steinhart–Hart fit using three calibration points.
4. **Noise and oversampling.** Set `OVERSAMPLE` to 1, 4, 16 and 64 and log the standard deviation of a constant voltage. Noise should fall roughly as 1/√N.
5. **LDR response time.** Cover the LDR suddenly at `rate 50` and measure the time constant from the plot.

## Ideas to extend it

- Current measurement with a 1 Ω shunt resistor + power calculation (P = VI)
- Auto-ranging resistance using two reference resistors switched by digital pins
- Capacitance meter: time an RC charge to 63 % (one time constant)
- Save logs to an SD card for standalone use
