#include "user_interface.h"
#include <Adafruit_VL53L0X.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <OneButton.h>
#include <TM1637TinyDisplay.h>

#define CLK D6
#define DIO D5
// #define BUTTON D7


/*
features:
- measures desk height in cm or inches

interface:
- button press:
- double click:
- triple click:
- long press: turn off

*/

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
bool unitInch = false;

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
	unitInch = !unitInch;
	if (unitInch) {
		display.showString("inch");
	} else {
		display.showString(" c  ");
		display.setSegments(0b01010101, 2);
	}
	msgUntil = millis() + 1000;
	display.setBrightness(7);
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
	if (unitInch) {
		display.showNumberDec(int(height / 2.54), 0b01000000, false, 3, 0);
		display.showString("\"", 1, 3);
	} else {
		display.showNumber(int(height / 10), false, 3, 0);
		display.showString(" ", 1, 3);
	}
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
			avg = .8 * avg + .2 * measure.RangeMilliMeter;
			longavg = .9 * longavg + .1 * measure.RangeMilliMeter;

			if (millis() > msgUntil) {
				msgUntil = 0;

				// display dimming after 3 seconds
				if (millis() - t > 3000) {
					display.setBrightness(1);
					t = millis();
					displayActive = false;
					displayHeight(longavg);
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
					displayHeight(avg);
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
