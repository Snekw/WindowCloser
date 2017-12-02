/*
	This program is free software : you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.If not, see <http://www.gnu.org/licenses/>.
*/

#include <Button.h>
#include <DS3232RTC.h>
#include <Time.h>
#include <TimeLib.h>
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <Stepper.h>

// LCD Definitions
#define I2C_ADDR    0x27 
#define BACKLIGHT_PIN     3
#define En_pin  2
#define Rw_pin  1
#define Rs_pin  0
#define D4_pin  4
#define D5_pin  5
#define D6_pin  6
#define D7_pin  7
LiquidCrystal_I2C  lcd(I2C_ADDR, En_pin, Rw_pin, Rs_pin, D4_pin, D5_pin, D6_pin, D7_pin);

// Stepper Definitions
#define STEPPER_STEPS 200
#define STEPPER_PIN_1 8
#define STEPPER_PIN_2 9
#define STEPPER_PIN_3 10
#define STEPPER_PIN_4 11
#define STEPPER_SPEED 100

#define STEPPER_ANGLE 4

Stepper stepper = Stepper(STEPPER_STEPS, STEPPER_PIN_1, STEPPER_PIN_2, STEPPER_PIN_3, STEPPER_PIN_4);

// Input Definitions
#define INPUT_CONTROL_MAIN 2
#define INPUT_CONTROL_1 7
#define INPUT_CONTROL_2 12
#define INPUT_INT_DS 3

Button bMain = Button(INPUT_CONTROL_MAIN, BUTTON_PULLUP_INTERNAL, true, 50);
Button bControl1 = Button(INPUT_CONTROL_1, BUTTON_PULLUP_INTERNAL, true, 50);
Button bControl2 = Button(INPUT_CONTROL_2, BUTTON_PULLUP_INTERNAL, true, 50);

// State definitions
#define STATE_MAIN 0
#define STATE_OPEN_EDIT 1
#define STATE_CLOSE_EDIT 2
#define STATE_TIME_EDIT 3
#define STATE_GET_WINDOW_STATE 4
#define STATE_MANUAL_WINDOW_OPEN 5
#define STATE_MANUAL_WINDOW_CLOSE 6
#define STATE_ENABLE_EDIT 7

// Window state definitions
#define WINDOW_OPEN 1
#define WINDOW_CLOSED 2
#define WINDOW_UNKNOWN 0

uint8_t state = STATE_GET_WINDOW_STATE;

tmElements_t openTime;
tmElements_t closeTime;

char openWindowEnable = false;
char closeWindowEnable = false;

tmElements_t editTime;
uint8_t editState = 1;
uint16_t lastInput = 0;
uint8_t windowState = WINDOW_UNKNOWN;

bool wasPwrDown = false;
bool uiRefresh = true;

void printMenu(uint8_t state, bool forceRefresh = false);

bool BMainHold = false;
void onBMainRelease(Button &b) {
	lastInput = 0;
	if (BMainHold) {
		BMainHold = false;
		return;
	}
	if (wasPwrDown) {
		wasPwrDown = false;
		return;
	}
	switch (state)
	{
	case STATE_OPEN_EDIT:
		openTime = editTime;
		writeEEPROM();
		activateAlarms();
		state = STATE_MAIN;
		break;
	case STATE_CLOSE_EDIT:
		closeTime = editTime;
		writeEEPROM();
		activateAlarms();
		state = STATE_MAIN;
		break;
	case STATE_TIME_EDIT:
		RTC.write(editTime);
		state = STATE_MAIN;
		break;
	case STATE_MAIN:
		state = STATE_ENABLE_EDIT;
		break;
	case STATE_ENABLE_EDIT:
		state = STATE_MAIN;
		break;
	default:
		break;
	}
}

void onBMainHold(Button &b) {
	lastInput = 0;
	BMainHold = true;
	if (state != STATE_MAIN && state != STATE_GET_WINDOW_STATE) {
		state = STATE_MAIN;
		return;
	}
	if (state == STATE_MAIN) {
		RTC.read(editTime);
		editTime.Second = 0;
		state = STATE_TIME_EDIT;
	}
}

// Control 1

bool BControl1Hold = false;
void onBControl1Release(Button &b) {
	lastInput = 0;
	if (BControl1Hold) {
		BControl1Hold = false;
		return;
	}
	switch (state)
	{
	case STATE_MAIN:
		state = STATE_OPEN_EDIT;
		editState = STATE_OPEN_EDIT;
		editTime = openTime;
		break;
	case STATE_TIME_EDIT:
	case STATE_CLOSE_EDIT:
	case STATE_OPEN_EDIT:
		editTime.Hour++;
		if (editTime.Hour > 23) {
			editTime.Hour = 0;
		}
		printMenu(editState, true);
		break;
	case STATE_GET_WINDOW_STATE:
		windowState = WINDOW_OPEN;
		state = STATE_MAIN;
		break;
	case STATE_ENABLE_EDIT:
		openWindowEnable = !openWindowEnable;
		writeEEPROM();
		printMenu(state, true);
		break;
	default:
		break;
	}
}

void onBControl1Hold(Button &b) {
	lastInput = 0;
	BControl1Hold = true;
	switch (state)
	{
	case STATE_TIME_EDIT:
	case STATE_CLOSE_EDIT:
	case STATE_OPEN_EDIT:
		editTime.Hour--;
		if (editTime.Hour > 250) {
			editTime.Hour = 23;
		}
		printMenu(editState, true);
		break;
	case STATE_MAIN:
		state = STATE_MANUAL_WINDOW_OPEN;
	default:
		break;
	}
}

// Control 2

bool BControl2Hold = false;
void onBControl2Release(Button &b) {
	lastInput = 0;
	if (BControl2Hold) {
		BControl2Hold = false;
		return;
	}
	uint8_t minuteAdjustSize = 5;
	switch (state)
	{
	case STATE_MAIN:
		state = STATE_CLOSE_EDIT;
		editState = STATE_CLOSE_EDIT;
		editTime = closeTime;
		break;
	case STATE_TIME_EDIT:
		minuteAdjustSize = 1;
	case STATE_CLOSE_EDIT:
	case STATE_OPEN_EDIT:
		editTime.Minute += minuteAdjustSize;
		if (editTime.Minute > 55) {
			editTime.Minute = 0;
		}
		printMenu(editState, true);
		break;
	case STATE_GET_WINDOW_STATE:
		windowState = WINDOW_CLOSED;
		state = STATE_MAIN;
		break;
	case STATE_ENABLE_EDIT:
		closeWindowEnable = !closeWindowEnable;
		writeEEPROM();
		printMenu(state, true);
		break;
	default:
		break;
	}
}

void onBControl2Hold(Button &b) {
	lastInput = 0;
	BControl2Hold = true;
	uint8_t minuteAdjustSize = 5;
	switch (state)
	{
	case STATE_TIME_EDIT:
		minuteAdjustSize = 1;
	case STATE_CLOSE_EDIT:
	case STATE_OPEN_EDIT:
		editTime.Minute -= minuteAdjustSize;
		if (editTime.Minute > 250) {
			editTime.Minute = 55;
		}
		printMenu(editState, true);
		break;
	case STATE_MAIN:
		state = STATE_MANUAL_WINDOW_CLOSE;
	default:
		break;
	}
}

// Helper funcs

float getTemperature() {
	int t = RTC.temperature();
	return t / 4.0;
}

String getCurrentTimeString() {
	tmElements_t tm;
	byte status = RTC.read(tm);
	Serial.print("Curr time status: ");
	Serial.println(status);
	if (status > 0) {
		delay(100);
		return getCurrentTimeString();
	}
	else {
		return getTimeString(tm);
	}
}

String getTimeString(tmElements_t tm) {
	char buffer[9];
	snprintf(buffer, 9, "%02d:%02d", tm.Hour, tm.Minute);
	return buffer;
}

uint8_t lastState = 255;
void printMenu(uint8_t _state, bool forceRefresh = false) {
	if (_state == lastState && !forceRefresh && !uiRefresh) {
		return;
	}
	uiRefresh = false;
	Serial.print("State: ");
	Serial.println(_state, DEC);
	lcd.clear();
	lcd.home();
	lastState = _state;
	switch (_state) {
	default:
	case STATE_MAIN:
		lcd.print("Kello:         ");
		lcd.print(getCurrentTimeString());
		lcd.setCursor(0, 1);
		lcd.print("Lampotila:    ");
		lcd.print(getTemperature(), DEC);
		lcd.setCursor(18, 1);
		lcd.print((char)223); // Degree symbol
		lcd.print("C");
		lcd.setCursor(0, 2);
		lcd.print("Aukaisu:       ");
		lcd.print(getTimeString(openTime));
		lcd.setCursor(0, 3);
		lcd.print("Sulkeminen:    ");
		lcd.print(getTimeString(closeTime));
		break;
	case STATE_OPEN_EDIT:
		lcd.print("Aseta aukaisu aika");
		lcd.setCursor(0, 1);
		lcd.print(getTimeString(editTime));
		break;
	case STATE_CLOSE_EDIT:
		lcd.print("Aseta sulkemis aika");
		lcd.setCursor(0, 1);
		lcd.print(getTimeString(editTime));
		break;
	case STATE_GET_WINDOW_STATE:
		getWindowState();
		break;
	case STATE_TIME_EDIT:
		lcd.print("Aseta kellon aika"); // TODO
		lcd.setCursor(0, 1);
		lcd.print(getTimeString(editTime));
		break;
	case STATE_MANUAL_WINDOW_OPEN:
		lcd.print("Ikkuna avataan");
		openWindow(false);
		state = STATE_MAIN;
		delay(100);
		break;
	case STATE_MANUAL_WINDOW_CLOSE:
		lcd.print("Ikkuna suljetaan");
		closeWindow(false);
		state = STATE_MAIN;
		delay(100);
		break;
	case STATE_ENABLE_EDIT:
		lcd.print("Tila");
		lcd.setCursor(0, 1);
		lcd.print("Avaus:        ");
		if (openWindowEnable) {
			lcd.print("Kylla");
		}
		else {
			lcd.print("   Ei");
		}
		lcd.setCursor(0, 2);
		lcd.print("Sulkeminen:   ");
		if (closeWindowEnable) {
			lcd.print("Kylla");
		}
		else {
			lcd.print("   Ei");
		}
		break;
	}
}

void getWindowState() {
	lcd.clear();
	lcd.backlight();
	lcd.display();
	lcd.home();
	lcd.print("Ikkunan tila?");
	lcd.setCursor(0, 4);
	lcd.print("      Auki    Kiinni");
}

void openWindow(char alarm) {
	// Don't open the window with alarm if the enable is not set
	if (!openWindowEnable && alarm) {
		return;
	}
	if (windowState == WINDOW_UNKNOWN) {
		return getWindowState();
	}
	if (windowState == WINDOW_OPEN) {
		return;
	}

	windowState = WINDOW_UNKNOWN;
	// Drive stepper
	stepper.setSpeed(STEPPER_SPEED);
	stepper.step(STEPPER_STEPS * STEPPER_ANGLE);
	windowState = WINDOW_OPEN;
}

void closeWindow(char alarm) {
	// Don't open the window with alarm if the enable is not set
	if (!closeWindowEnable && alarm) {
		return;
	}
	if (windowState == WINDOW_UNKNOWN) {
		return getWindowState();
	}
	if (windowState == WINDOW_CLOSED) {
		return;
	}

	windowState = WINDOW_UNKNOWN;
	// Drive stepper
	stepper.setSpeed(STEPPER_SPEED);
	stepper.step(-(STEPPER_STEPS * STEPPER_ANGLE));
	windowState = WINDOW_CLOSED;
}

void readEEPROM() {
	int addr = 0;
	EEPROM.get(addr, openTime);
	addr = sizeof(tmElements_t);
	EEPROM.get(addr, closeTime);
	addr += sizeof(tmElements_t);
	EEPROM.get(addr, openWindowEnable);
	addr += sizeof(char);
	EEPROM.get(addr, closeWindowEnable);

	// Adjust for invalid values in the times
	if (openTime.Hour >= 24) {
		openTime.Hour = 20;
	}
	if (openTime.Minute >= 60) {
		openTime.Minute = 0;
	}
	if (closeTime.Hour >= 24) {
		closeTime.Hour = 6;
	}
	if (closeTime.Minute >= 60) {
		closeTime.Minute = 0;
	}
	if (openWindowEnable > 1) {
		openWindowEnable = true;
	}
	if (closeWindowEnable > 1) {
		closeWindowEnable = true;
	}
}

void writeEEPROM() {
	int addr = 0;
	EEPROM.put(addr, openTime);
	addr = sizeof(tmElements_t);
	EEPROM.put(addr, closeTime);
	addr += sizeof(tmElements_t);
	EEPROM.put(addr, openWindowEnable);
	addr += sizeof(char);
	EEPROM.put(addr, closeWindowEnable);
}

void activateAlarms() {
	// Open alarm
	RTC.setAlarm(ALARM_TYPES_t::ALM1_MATCH_HOURS, 0, openTime.Minute, openTime.Hour, 0);
	// Close alarm
	RTC.setAlarm(ALARM_TYPES_t::ALM2_MATCH_HOURS, 0, closeTime.Minute, closeTime.Hour, 0);
}

// Empty call for wakeup interrupt 
void wakeUp() {
}

void pwrDown() {
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	sleep_enable();
	attachInterrupt(0, wakeUp, LOW);
	attachInterrupt(1, wakeUp, LOW);
	// turn off the LCD
	lcd.noDisplay();
	lcd.setBacklight(LOW);
	Serial.println("Power down");

	// Delay required for all actions to finish before sleep
	delay(100);
	sleep_mode();
	sleep_disable();
	detachInterrupt(0);
	detachInterrupt(1);
	Serial.println("Power up");
	delay(100);

	if (RTC.alarm(1)) {
		openWindow(true);
		pwrDown();
	}
	else if (RTC.alarm(2)) {
		closeWindow(true);
		pwrDown();
	}
	else {
		wasPwrDown = true;
		lcd.display();
		lcd.setBacklight(HIGH);
		uiRefresh = true;
	}
}

void setup()
{
	Serial.begin(9600);
	Wire.begin();
	lcd.begin(20, 4);
	lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
	lcd.setBacklight(HIGH);
	lcd.display();
	lcd.home();
	pinMode(INPUT_INT_DS, INPUT);

	// Enable the interrupt flags on the DS3231
	RTC.alarmInterrupt(1, true);
	RTC.alarmInterrupt(2, true);

	readEEPROM();
	activateAlarms();

	bMain.releaseHandler(onBMainRelease);
	bMain.holdHandler(onBMainHold, 700);
	bControl1.releaseHandler(onBControl1Release);
	bControl1.holdHandler(onBControl1Hold, 700);
	bControl2.releaseHandler(onBControl2Release);
	bControl2.holdHandler(onBControl2Hold, 700);
}

void loop()
{
	bMain.process();
	bControl1.process();
	bControl2.process();

	// No sleep mode when asking for window state
	if (state != STATE_GET_WINDOW_STATE) {
		lastInput++;
		if (lastInput > 1000) {
			state = 0;
			lastInput = 0;
			pwrDown();
		}
	}

	printMenu(state);
	delay(10);
}