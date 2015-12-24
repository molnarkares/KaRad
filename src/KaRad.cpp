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


ADC_MODE(ADC_VCC);

#define USE_SERIAL Serial

const int led = 13;


void configModeCallback () {
	  digitalWrite(led, 1); //turn LED on if AP mode i.e. configuration is needed
}

enum sensorState_t {standalone,connected};
sensorState_t sensorState;

void setup() {
#ifdef WDT_ENABLED
  ESP.wdtEnable(10000);
#endif

  pinMode(led, OUTPUT);
  digitalWrite(led, 0);

  USE_SERIAL.begin(115200);

	//WiFiManager
	//Local intialization. Once its business is done, there is no need to keep it around
	WiFiManager wifiManager;
	//reset saved settings
	wifiManager.resetSettings();
	//wifiManager.setDebugOutput(false);


	wifiManager.setTimeout(180);

	wifiManager.setAPCallback(configModeCallback);

	if(wifiManager.autoConnect("KaRad1 AP")) {
		//if you get here you have connected to the WiFi
		USE_SERIAL.println("connected... :)");
		sensorState = connected;
	}else {
		//was unable to connect and timeout
		sensorState = standalone;
	}
	digitalWrite(led, 0); // turn off LED
}

void loop() {

#ifdef WDT_ENABLED
    ESP.wdtFeed();
#endif

    uint16_t voltage = ESP.getVcc();
}
