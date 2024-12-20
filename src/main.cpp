#include "user_interface.h"
#include <Adafruit_VL53L0X.h>
#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <OneButton.h>
#include <TM1637TinyDisplay.h>

#define CLK D6
#define DIO D5
// #define BUTTON D7
#define ADJUST 2 // adjustment: add thickness of the desktop

/*
features:
- measures desk height in cm, inches or bananas

interface: block sensor for more than 3 seconds to change units. 
Hold until desired unit is selected, then let go to save.

*/

enum Unit {
	CENTIMETERS,
	INCHES,
	BANANAS
};

TM1637TinyDisplay display(CLK, DIO);
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
VL53L0X_RangingMeasurementData_t measure;

#ifdef BUTTON
OneButton button(BUTTON, true, true);
time_t pressStartTime;
#endif

float avg = 0, avgold = 0, longavg;
bool displayActive = true;
long t = 0;
time_t ignoreUntil = 0, msgUntil = 0;
Unit units = CENTIMETERS;

unsigned long unitChangeTime = 0;
unsigned long closeProximityStart = 0;
bool cyclingUnits = false;

#ifdef BUTTON
IRAM_ATTR void checkTicks() {
	button.tick();
}

void fpm_wakup_cb_func(void) {
	wifi_fpm_close();
}

void doSleep() {
	display.showString("    ");
	delay(250);
	display.showString(" Bye");
	display.setBrightness(7);
	delay(1000);
	display.setBrightness(0);
	display.showString("    ");
	display.setSegments(0b10000000, 3);
	delay(100);
	detachInterrupt(digitalPinToInterrupt(BUTTON));
	WiFi.mode(WIFI_OFF);
	wifi_station_disconnect();
	wifi_set_opmode(NULL_MODE);
	wifi_set_opmode_current(NULL_MODE);
	delay(100);
	wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
	wifi_fpm_open();
	gpio_pin_wakeup_disable();
	gpio_pin_wakeup_enable(GPIO_ID_PIN(BUTTON), GPIO_PIN_INTR_LOLEVEL);
	wifi_fpm_set_wakeup_cb(fpm_wakup_cb_func);
	wifi_fpm_do_sleep(0xFFFFFFF);
	delay(10);

	button.reset();
	display.setBrightness(7);
	display.showString("    Hello    ");
	msgUntil = millis() + 500;
	while (digitalRead(BUTTON) == LOW) {
	}
	delay(100);
	attachInterrupt(digitalPinToInterrupt(BUTTON), checkTicks, CHANGE);
	t = millis();
	displayActive = true;
};

void singleClick() {
	Serial.println("singleClick() detected.");
	cycleUnit();
}

void doubleClick() {
	Serial.println("doubleClick() detected.");
	display.showString("2   ");
	msgUntil = millis() + 1000;
	display.setBrightness(7);
}

void multiClick() {
	int n = button.getNumberClicks();
	display.showString(String(n).c_str());
	msgUntil = millis() + 1000;
	display.setBrightness(7);

	Serial.print("multiClick(");
	Serial.print(n);
	Serial.println(") detected.");
}

void pressStart() {
	Serial.println("pressStart()");
	display.showString("OFF ");
	msgUntil = millis() + 1000;
	display.setBrightness(7);
	pressStartTime = millis() - 1000;
}

void pressStop() {
	Serial.print("pressStop(");
	Serial.print(millis() - pressStartTime);
	Serial.println(") detected.");
	doSleep();
}
#endif

void displayHeight(float height) {
	switch (units) {
	case CENTIMETERS:
		display.showNumber(int(height / 10), false, 3, 0);
		display.showString(" ", 1, 3);
		break;
	case INCHES:
		display.showNumberDec(int(height / 2.54), 0b01000000, false, 3, 0);
		display.showString("\"", 1, 3);
		break;
	case BANANAS:
		display.showNumberDec(int(height / 1.80), 0b10000000, false, 3, 0);
		display.showString("]", 1, 3);
		break;
	}
}

void saveCurrentUnit() {
	EEPROM.write(2, static_cast<uint8_t>(units));
	EEPROM.commit();
}

void printUnit(Unit unit) {
	switch (unit) {
	case CENTIMETERS:
		display.showString(" c  ");
		display.setSegments(0b01010101, 2);
		break;
	case INCHES:
		display.showString("inch");
		break;
	case BANANAS:
		display.showString("bnan");
		break;
	}
	msgUntil = millis() + 1000;
	t = millis();
	displayActive = true;
	display.setBrightness(7);
}

void cycleUnit() {
	units = static_cast<Unit>((units + 1) % 3);
	printUnit(units);
}

void setup() {

	Serial.begin(115200);
	WiFi.mode(WIFI_OFF);

	display.setBrightness(7);
	display.showString("    Hello    ");
	msgUntil = millis() + 1000;

	if (!lox.begin()) {
		Serial.println(F("Failed to boot VL53L0X"));
		display.showString("err ");
		while (1) {
			delay(100);
		}
	}
	lox.configSensor(Adafruit_VL53L0X::VL53L0X_SENSE_DEFAULT);

	display.clear();

	// read preferences
	EEPROM.begin(512);
	if (EEPROM.read(0) != 0x55 || EEPROM.read(1) != 0xAA) {
		Serial.println("Invalid EEPROM, initializing...");
		EEPROM.write(0, 0x55);
		EEPROM.write(1, 0xAA);
		EEPROM.write(2, static_cast<uint8_t>(units));
		EEPROM.commit();
	} else {
		units = static_cast<Unit>(EEPROM.read(2));
	}

#ifdef BUTTON
	attachInterrupt(digitalPinToInterrupt(BUTTON), checkTicks, CHANGE);

	button.attachClick(singleClick);
	button.attachDoubleClick(doubleClick);
	button.attachMultiClick(multiClick);
	button.setPressMs(1000);
	button.attachLongPressStart(pressStart);
	button.attachLongPressStop(pressStop);
#endif
}

void loop() {

#ifdef BUTTON
	button.tick();
#endif

	if (millis() > ignoreUntil) {
		ignoreUntil = 0;
		lox.rangingTest(&measure, false);

		if (measure.RangeStatus != 4) {

			// change units by holding your finger on the sensor for more than 3 seconds
			if (measure.RangeMilliMeter < 50) {
				if (closeProximityStart == 0) {
					closeProximityStart = millis();
				}
				if (millis() - closeProximityStart > 3000) {
					if (millis() - closeProximityStart < 3000 + 3 * 3000) {
						cyclingUnits = true;
						if (millis() - unitChangeTime > 3000) {
							cycleUnit();
							unitChangeTime = millis();
						}
					} else {
						cyclingUnits = false;
						if (millis() - unitChangeTime > 3000) {
							units = static_cast<Unit>(EEPROM.read(2));
							unitChangeTime = millis();
							display.showString("____");
							msgUntil = millis() + 1500;
						}
					}
				}
			} else if (measure.RangeMilliMeter > 100) {
				if (cyclingUnits) {
					saveCurrentUnit();
					cyclingUnits = false;
					display.showString("save");
					msgUntil = millis() + 1000;
					display.setBrightness(7);
				}
				closeProximityStart = 0;
			}

			avg = .8 * avg + .2 * measure.RangeMilliMeter;
			longavg = .9 * longavg + .1 * measure.RangeMilliMeter;

			if (millis() > msgUntil) {
				msgUntil = 0;

				// display dimming after 3 seconds
				if (millis() - t > 3000) {
					display.setBrightness(1);
					t = millis();
					displayActive = false;
					displayHeight(longavg + ADJUST);
				}

				// jitter protection
				if (int((longavg - (int)longavg % 10 - 5) / 10) != int((avgold - (int)longavg % 10 - 5) / 10)) {
					Serial.println(longavg);
					t = millis();
					if (displayActive == false) avg = measure.RangeMilliMeter;
					displayActive = true;
					avgold = longavg;
					display.setBrightness(7);
				}

				if (displayActive) {
					displayHeight(avg + ADJUST);
				} else {
					ignoreUntil = millis() + 250;
				}
			}
		} else {
			Serial.println("Too high, out of range");
			display.showString(" ^^ ");
			ignoreUntil = millis() + 100;
		}
	}
	delay(10);
}
