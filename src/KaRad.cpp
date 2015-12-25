/*
 * KaRad.c
 *
 *  Created on: Dec 24, 2015
 *      Author: molnarkaroly
 */


/**
 * KaRad
 *
 * (C) Karoly Molnar 2015
 * MIT license see license.md in the repository
 */

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

extern "C" {
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"
#include "cont.h"
}

ADC_MODE(ADC_VCC);

const int led_pin = BUILTIN_LED;//or 13;
const int beep_pin = D1; // GPIO05
const char ap_hostname[] = "KaRad1";
#define CPM_BEEP_LIMIT 100
#define T_REPORT_MS (60 * 1000)

#define CFG_VALID	0x1abbcc

void configModeCallback () {
	digitalWrite(led_pin, LOW); //turn LED on if AP mode i.e. configuration is needed
}

enum sensorState_t {
	sensor_standalone,
	sensor_connected,
	sensor_update,
	sensor_unknown};

bool sendUpdate(uint16_t voltage, uint16_t cpm);

WiFiManager wifiManager;

typedef struct {
	uint32_t s_cpm;
	uint32_t s_time;
	uint32_t s_beep;
	uint32_t s_report;
	sensorState_t sensorState;
	uint32_t s_valid;
}saved_t;

saved_t rtcStore;
uint32_t cpm;

#define DEBUG

void setup() {
#ifdef WDT_ENABLED
	ESP.wdtEnable(10000);
#endif
	bool report = false;
	//the wakeup was caused by a new impulse form the GM tube
	uint32_t t_now = millis();//we store ms not us
	system_rtc_mem_read(65, &rtcStore, sizeof(rtcStore));
	pinMode(led_pin, OUTPUT);

	if(rtcStore.s_valid != CFG_VALID) {
		rtcStore.s_cpm = 0;
		rtcStore.s_time = system_get_time()/1000;
		rtcStore.s_beep = 0;
		rtcStore.s_report = 0;
		rtcStore.sensorState = sensor_unknown;
		rtcStore.s_valid = CFG_VALID;
		cpm = 0;
	}else {
		if((rtcStore.s_beep) || (rtcStore.s_cpm > CPM_BEEP_LIMIT)) {
			analogWriteFreq(2000);
			pinMode(beep_pin,OUTPUT);
			analogWrite(beep_pin,PWMRANGE>>1);
			delay(10);
			analogWrite(beep_pin,0);
		}


#ifdef DEBUG
		digitalWrite(led_pin, LOW);
		delay(50);
		digitalWrite(led_pin, HIGH);
#endif

		uint32_t t_diff;
		if(t_now > rtcStore.s_time) {
			t_diff = t_now - rtcStore.s_time;
		}else {
			t_diff = (0xffffffff - rtcStore.s_time) + t_now;
		}
		rtcStore.s_cpm++;
		if(t_diff >= T_REPORT_MS) { // time to report!
			if(rtcStore.s_report) {
				cpm = (rtcStore.s_cpm * t_diff) / T_REPORT_MS;
				rtcStore.s_cpm = 0;
				rtcStore.s_report = 0;
			}else {
				rtcStore.s_cpm--;
				rtcStore.s_report = 1;
				system_rtc_mem_write(65, &rtcStore, sizeof(rtcStore));
				system_deep_sleep_set_option(1);	// enable radio after wakeup
				system_deep_sleep(2); // wake up immediately
			}
		}else {
			rtcStore.s_time = t_now;
			system_rtc_mem_write(65, &rtcStore, sizeof(rtcStore));
			system_deep_sleep_set_option(4);	// no radio after wakeup
			system_deep_sleep(0); // wait for falling edge on reset pin
		}

	}


	Serial.begin(115200);

	//reset saved settings
	//	wifiManager.resetSettings();
	if(rtcStore.sensorState != sensor_standalone) {
		struct rst_info *thisreset;
		thisreset = system_get_rst_info();

		if(thisreset->reason != REASON_DEEP_SLEEP_AWAKE) {
			wifiManager.setTimeout(180);
		}else {
			wifiManager.setTimeout(20);
		}

		wifiManager.setAPCallback(configModeCallback);


		ArduinoOTA.onStart([]() {
			rtcStore.sensorState = sensor_update;
		});
		ArduinoOTA.onEnd([]() {
			//invalidate config
			rtcStore.s_valid = 0;
			system_rtc_mem_write(65, &rtcStore, sizeof(rtcStore));
		});
		ArduinoOTA.onError([](ota_error_t error) {
			Serial.printf("Error[%u]: ", error);
			rtcStore.sensorState = sensor_connected;
		});

		if(wifiManager.autoConnect(ap_hostname)) {
			//if you get here you have connected to the WiFi
			Serial.println("connected... :)");
			rtcStore.sensorState = sensor_connected;
			ArduinoOTA.setHostname(ap_hostname);
			ArduinoOTA.begin();
		}else {
			//was unable to connect and timeout
			Serial.println("No connection was possible within 3 minutes... :(");
			rtcStore.sensorState = sensor_standalone;
			rtcStore.s_beep = 1;
		}
	}else {
		rtcStore.s_time = millis();
		system_rtc_mem_write(65, &rtcStore, sizeof(rtcStore));
		system_deep_sleep_set_option(4);	// no radio after wakeup
		system_deep_sleep(0); // wait for falling edge on reset pin
	}
}

void loop() {

#ifdef WDT_ENABLED
	ESP.wdtFeed();
#endif

	if(rtcStore.sensorState == sensor_connected) {
		uint16_t voltage = ESP.getVcc();
		if(!sendUpdate(voltage,cpm)) {
			rtcStore.s_beep = 1;
		}
		rtcStore.s_time = millis();
		system_rtc_mem_write(65, &rtcStore, sizeof(rtcStore));
		system_deep_sleep_set_option(4);	// no radio after wakeup
		system_deep_sleep(0); // wait for falling edge on reset pin

	}

	if(rtcStore.sensorState == sensor_update) {
		ArduinoOTA.handle();
	}
}

bool sendUpdate(uint16_t voltage, uint16_t cpm) {
	if(wifiManager.autoConnect(ap_hostname)) {
		return true;
	}else {
		return false;
	}
}

