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
char ap_hostname[] = "KaRad1";
#define CPM_BEEP_LIMIT 100


void configModeCallback () {
	  digitalWrite(led_pin, LOW); //turn LED on if AP mode i.e. configuration is needed
}

enum sensorState_t {
	sensor_standalone,
	sensor_connected,
	sensor_update};

bool beeping = false;

sensorState_t sensorState;

uint16_t cpm,cpm_u;

unsigned long elapsedms;

bool sendUpdate(uint16_t voltage, uint16_t cpm);
void beep(void);
void cpmHandler(void);

WiFiManager wifiManager;

void setup() {
#ifdef WDT_ENABLED
  ESP.wdtEnable(10000);
#endif

  pinMode(led_pin, OUTPUT);
  digitalWrite(led_pin, HIGH);


  analogWriteFreq(2000);
  pinMode(beep_pin,OUTPUT);

  Serial.begin(115200);

	//reset saved settings
//	wifiManager.resetSettings();


	wifiManager.setTimeout(180);

	wifiManager.setAPCallback(configModeCallback);


	ArduinoOTA.onStart([]() {
	    sensorState = sensor_update;
	    beep();
	  });
	ArduinoOTA.onEnd([]() {
	    beep();
	    delay(100);
	    beep();
	  });
	ArduinoOTA.onError([](ota_error_t error) {
	    Serial.printf("Error[%u]: ", error);
	    sensorState = sensor_connected;
	  });

	if(wifiManager.autoConnect(ap_hostname)) {
	//if you get here you have connected to the WiFi
		Serial.println("connected... :)");
		sensorState = sensor_connected;
		ArduinoOTA.setHostname(ap_hostname);
		ArduinoOTA.begin();
		wifiManager.setTimeout(20); //significantly shorter timeout for runtime reconnects
	}else {
	//was unable to connect and timeout
		Serial.println("No connection was possible within 3 minutes... :(");
		sensorState = sensor_standalone;
		beeping = true;
	}
	digitalWrite(led_pin, HIGH); // turn off LED
	elapsedms = millis();
	cpm=0;
	cpm_u=0;

}

void loop() {

#ifdef WDT_ENABLED
    ESP.wdtFeed();
#endif

    if(sensorState == sensor_connected) {
    	uint16_t voltage = ESP.getVcc();
    	if(!sendUpdate(voltage,cpm)) {
    		beeping = true;
    	}
    }

    if(sensorState == sensor_update) {
    	ArduinoOTA.handle();
    }else {
    	delay(60000);
    }

}

bool sendUpdate(uint16_t voltage, uint16_t cpm) {
	if(wifiManager.autoConnect(ap_hostname)) {
		return true;
	}else {
		return false;
	}
}


void cpmHandler(void) {
	char rtcStore[2];
	system_rtc_mem_read(64, rtcStore, 2);

	unsigned long msnow = millis();
	unsigned long msdiff;
	cpm++;
	if(msnow < elapsedms) { //overrun
		msdiff = elapsedms - msnow;
	}else {
		msdiff = msnow - elapsedms;
	}

	if(msdiff >= 60000) {

		cpm_u = cpm;
	}
	if(beeping || (cpm > CPM_BEEP_LIMIT)) {
		beep();
	}
}

void beep(void) {
	analogWrite(beep_pin,PWMRANGE>>1);
	delay(10);
	analogWrite(beep_pin,0);
}
