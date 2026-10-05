/*
 * Denon AVR-1912 Controller
 *
 * Copyright (C) 2026 Mark Clark
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the LICENSE file for the complete license.
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "FT6236.h"
#include "display.h"
#include "main.h"

/* the touch scree object */
FT6236 ts = FT6236(TFT_HEIGHT, TFT_WIDTH);

/* the tft object */
TFT_eSPI tft = TFT_eSPI();

/* globals shared between functions */
String nowPlayingArtist = "";
String nowPlayingTitle = "";
String nowPlayingStation = "";
String lastMVresponse = "";
String lastZ2response = "";

unsigned long lastNSEPoll = 0;
const unsigned long NSE_POLL_INTERVAL = 10000;
bool mainMuted = false;
bool zone2Muted = false;

// --------------------------------------------------
// Send a generic command and return the response
// --------------------------------------------------
String sendDenonCommand(const char *command) {
  WiFiClient client;
  String response;

  if (!client.connect(clientIP, 23)) {
    Serial.println("Denon connection FAILED");
    client.stop();
    return "";
  }

  client.print(command);
  client.print("\r");

  unsigned long start = millis();

  while (millis() - start < 500) {
    while (client.available()) {
      char c = client.read();

      if (c == '\r') {
        client.stop();
        return response;
      }

      response += c;
    }

    delay(1);
  }

  client.stop();

  return response;
}
// --------------------------------------------------
// NSE command is parsed differently than others
// --------------------------------------------------
bool sendDenonNSE(String &artist, String &title, String &station) {
  WiFiClient client;

  artist = "";
  title = "";
  station = "";

  if (!client.connect(clientIP, 23)) {
    Serial.println("Denon connection FAILED");
    return false;
  }

  client.print("NSE");
  client.print("\r");

  uint8_t buffer[1024];
  size_t length = 0;

  unsigned long start = millis();

  while (millis() - start < 2000 && length < sizeof(buffer)) {
    while (client.available() && length < sizeof(buffer)) {
      buffer[length++] = client.read();
    }

    delay(1);
  }

  client.stop();

  // Parse the CR-terminated NSE records.
  size_t pos = 0;

  while (pos < length) {
    if (pos + 4 > length) {
      break;
    }

    if (buffer[pos] != 'N' ||
        buffer[pos + 1] != 'S' ||
        buffer[pos + 2] != 'E') {
      pos++;
      continue;
    }

    int field = buffer[pos + 3] - '0';

    size_t dataStart = pos + 4;

    // NSE1-NSE6 have a one-byte cursor/playable flag.
    if (field >= 1 && field <= 6) {
      dataStart++;
    }

    size_t dataEnd = dataStart;

    while (dataEnd < length &&
           buffer[dataEnd] != 0x00 &&
           buffer[dataEnd] != 0x0D) {
      dataEnd++;
    }

    String value;

    for (size_t i = dataStart; i < dataEnd; i++) {
      value += (char)buffer[i];
    }

    value.trim();

    if (field == 1) {
      int separator = value.indexOf(" - ");

      if (separator >= 0) {
        artist = value.substring(0, separator);
        title = value.substring(separator + 3);
      } else {
        title = value;
      }
    } else if (field == 2) {
      station = value;
    }

    // Move to the next CR-terminated record.
    while (pos < length && buffer[pos] != 0x0D) {
      pos++;
    }

    if (pos < length) {
      pos++;
    }
  }

  return true;
}

// --------------------------------------------------
// Text lines that are too wide get a 2nd line
// --------------------------------------------------
void drawWrappedText(TFT_eSPI *tft, const String &text,
                     int x, int y, int maxWidth) {

  String line1 = "";
  String line2 = "";

  int lastSpace = -1;

  for (int i = 0; i < text.length(); i++) {
    if (text[i] == ' ') {
      lastSpace = i;
    }

    String test = text.substring(0, i + 1);

    if (tft->textWidth(test) > maxWidth) {
      if (lastSpace >= 0) {
        line1 = text.substring(0, lastSpace);
        line2 = text.substring(lastSpace + 1);
      } else {
        line1 = text;
      }

      break;
    }
  }

  if (line1 == "") {
    line1 = text;
  }

  tft->setCursor(x, y);
  tft->print(line1);

  if (line2 != "") {
    tft->setCursor(x, y + 10);
    tft->print(line2);
  }
}

// --------------------------------------------------
// NSE fields are fixed length, special handling
// --------------------------------------------------
String parseNSEField(const String &response, int field) {
  String tag = "NSE";
  tag += field;

  int start = response.indexOf(tag);

  if (start < 0) {
    return "";
  }

  // Skip "NSEn" and the binary status byte.
  start += tag.length() + 1;

  // Each NSE record ends with CR.
  int end = response.indexOf('\r', start);

  if (end < 0) {
    return "";
  }

  String value = "";

  // Copy only printable characters, stopping at NULL padding.
  for (int i = start; i < end; i++) {
    char c = response[i];

    if (c == '\0') {
      break;
    }

    if (c >= 32 && c <= 126) {
      value += c;
    }
  }

  value.trim();

  return value;
}

// --------------------------------------------------
// Main Volume changes in half steps and no decimal
// --------------------------------------------------
String parseMainVolume(const String &response) {
  String value = response.substring(2);

  Serial.print("main vol response..");
  Serial.println(response);

  if (value[0] == '0') {
    value[0] = ' ';
  }

  if (value.length() == 3 && value.endsWith("5")) {
    value.remove(value.length() - 1);
    return value + ".5";
  }

  return value + ".0";
}

// --------------------------------------------------
// Zone2 volume is just an integer after the string
// --------------------------------------------------
String parseZone2Volume(const String &response) {
  return response.substring(2);
}

#if 1
// keep for testing new commands
void testDenonCommand() {
  WiFiClient client;

  Serial.println("Connecting to Denon...");

  if (!client.connect(clientIP, 23)) {
    Serial.println("Denon connection FAILED");
    return;
  }

  Serial.println("Connected.");

  client.print("MV?\r");

  unsigned long start = millis();

  while (millis() - start < 500) {
    while (client.available()) {
      char c = client.read();
      Serial.write(c);
    }
  }

  client.stop();
  Serial.println("\nConnection closed.");
}
#endif

// --------------------------------------------------
// Dynamic text size, not currently used
// --------------------------------------------------
void drawFittedText(TFT_eSPI *tft, const String &text,
                    int x, int y, int maxWidth) {

  int textSize;

  // Try largest font first.
  for (textSize = 3; textSize >= 1; textSize--) {
    tft->setTextSize(textSize);

    int width = tft->textWidth(text);

    if (width <= maxWidth) {
      break;
    }
  }

  tft->setTextSize(textSize);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setCursor(x, y);
  tft->print(text);
}

// --------------------------------------------------
// Setup monitor, fixed screen info, and networking
// --------------------------------------------------
void setup() {

  Serial.begin(115200);
  delay(200);

  initTouchScreen(&ts);
  initTft(&tft);
  logDisplayDebugInfo(&tft);

  Serial.printf("TFT width=%d height=%d\n", tft.width(), tft.height());

  // blank the entire screen
  tft.fillScreen(TFT_BLACK);

  // --------------------------------------------------
  // Title
  // --------------------------------------------------
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextSize(2);
  tft.setCursor(70, 12);
  tft.print(" Denon AVR-1912 ");

  // thick line
  tft.drawFastHLine(10, 35, 300, TFT_RED);
  tft.drawFastHLine(10, 36, 300, TFT_RED);
  tft.drawFastHLine(10, 37, 300, TFT_RED);
  tft.drawFastHLine(10, 38, 300, TFT_RED);
  tft.drawFastHLine(10, 39, 300, TFT_RED);

  // --------------------------------------------------
  // Connect to local area network
  // --------------------------------------------------
  WiFi.begin(SSID, WIFI_PWD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Wi-Fi connected, IP address: ");
  Serial.println(WiFi.localIP());
  // --------------------------------------------------
  // MAIN volume
  // --------------------------------------------------
  tft.drawRect(MAIN_VOL_X, MAIN_VOL_Y, MAIN_VOL_W, MAIN_VOL_H, TFT_ORANGE);
  tft.setTextSize(2);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setCursor(MAIN_VOL_X + 8, MAIN_VOL_Y - 10);
  tft.print("[  Main  ]");

  // main volume down
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(MAIN_VOL_X + 15, MAIN_VOL_Y + 18);
  tft.print("-");

  // main volume label
  tft.setTextSize(2);
  tft.setCursor(MAIN_VOL_X + 80, MAIN_VOL_Y + 22);
  tft.print("Vol ..");

  // main volume up
  tft.setTextSize(3);
  tft.setCursor(MAIN_VOL_X + 200, MAIN_VOL_Y + 18);
  tft.print("+");

  // Small MUTE button
  tft.drawRect(MAIN_MUT_X, MAIN_MUT_Y, MAIN_MUT_W, MAIN_MUT_H, TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, TFT_GREENYELLOW);
  tft.setCursor(MAIN_MUT_X + 8, MAIN_MUT_Y + 7);
  tft.print("      ");
  tft.setCursor(MAIN_MUT_X + 8, MAIN_MUT_Y + 14);
  tft.print(" Mute ");
  tft.setCursor(MAIN_MUT_X + 8, MAIN_MUT_Y + 21);
  tft.print("      ");

  // --------------------------------------------------
  // ZONE 2 volume
  // --------------------------------------------------
  tft.drawRect(ZONE2_VOL_X, ZONE2_VOL_Y, ZONE2_VOL_W, ZONE2_VOL_H, TFT_YELLOW);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(ZONE2_VOL_X + 8, ZONE2_VOL_Y - 10);
  tft.print("[ Zone-2 ]");

  // zone 2 volume down
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(ZONE2_VOL_X + 15, ZONE2_VOL_Y + 18);
  tft.print("-");

  // zone 2 volume label
  tft.setTextSize(2);
  tft.setCursor(ZONE2_VOL_X + 80, ZONE2_VOL_Y + 22);
  tft.print("Vol ..");

  // zone 2 volume up
  tft.setTextSize(3);
  tft.setCursor(ZONE2_VOL_X + 200, ZONE2_VOL_Y + 18);
  tft.print("+");

  // Small MUTE button
  tft.drawRect(ZONE2_MUT_X, ZONE2_MUT_Y, ZONE2_MUT_W, ZONE2_MUT_H, TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, TFT_GREENYELLOW);
  tft.setCursor(ZONE2_MUT_X + 8, ZONE2_MUT_Y + 7);
  tft.print("      ");
  tft.setCursor(ZONE2_MUT_X + 8, ZONE2_MUT_Y + 14);
  tft.print(" Mute ");
  tft.setCursor(ZONE2_MUT_X + 8, ZONE2_MUT_Y + 21);
  tft.print("      ");

  // --------------------------------------------------
  // NOW PLAYING
  // --------------------------------------------------
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(18, 205);
  tft.print("Now playing...");
  tft.drawFastHLine(10, 215, 300, TFT_RED);

  tft.drawFastHLine(10, 315, 300, TFT_RED);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setTextSize(2);
  tft.setCursor(18, 230);
  tft.print(nowPlayingArtist);

  tft.setCursor(18, 260);
  tft.print(nowPlayingTitle);

  tft.setTextSize(1);
  tft.setCursor(18, 300);
  tft.print(nowPlayingStation);

  // --------------------------------------------------
  // Preset selection buttons
  // --------------------------------------------------
  tft.drawRect(PRESET1_X, PRESETS_Y, PRESETS_W, PRESETS_H, TFT_ORANGE);
  tft.drawRect(PRESET2_X, PRESETS_Y, PRESETS_W, PRESETS_H, TFT_YELLOW);
  tft.drawRect(PRESET3_X, PRESETS_Y, PRESETS_W, PRESETS_H, TFT_CYAN);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setCursor(PRESET1_X + 10, PRESETS_Y + 8);
  tft.print("PRESET 1");

  tft.setCursor(PRESET2_X + 10, PRESETS_Y + 8);
  tft.print("PRESET 2");

  tft.setCursor(PRESET3_X + 10, PRESETS_Y + 8);
  tft.print("PRESET 3");

  String response = sendDenonCommand("NSP1");

  Serial.print("NSP RESPONSE: [");
  Serial.print(response);
  Serial.println("]");
}

// --------------------------------------------------
// The main loop that runs forever
// --------------------------------------------------
void loop() {

  // poll for a change in songs
  if (millis() - lastNSEPoll >= NSE_POLL_INTERVAL) {
    lastNSEPoll = millis();

    String artist;
    String title;
    String station;

    if (sendDenonNSE(artist, title, station)) {
    
      if (artist != nowPlayingArtist ||
          title != nowPlayingTitle ||
          station != nowPlayingStation) {

        Serial.println("SONG CHANGED");

        Serial.print("TITLE: ");
        Serial.println(title);

        Serial.print("ARTIST: ");
        Serial.println(artist);

        Serial.print("STATION: ");
        Serial.println(station);

        nowPlayingTitle = title;
        nowPlayingArtist = artist;
        nowPlayingStation = station;

        tft.fillRect(15, 225, 290, 60, TFT_BLACK);

        tft.setTextSize(1);
        tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
        drawWrappedText(&tft, nowPlayingArtist, 18, 230, 200);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        drawWrappedText(&tft, nowPlayingTitle, 18, 260, 200);
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        drawWrappedText(&tft, nowPlayingStation, 18, 290, 200);
      }
    }
  }

  if (ts.touched()) {
    TS_Point p = ts.getPoint();

    Serial.printf("Touch: x=%d y=%d\n", p.x, p.y);

    // process first row of presets
    if (p.y >= PRESETS_Y && p.y <= (PRESETS_Y + PRESETS_H)) {
        if (p.x > PRESET3_X) {
           sendDenonCommand("NSP3");
        } else {
          if (p.x > PRESET2_X) {
            sendDenonCommand("NSP2");
          } else {
            sendDenonCommand("NSP1");
          }
        }
    }

    if (p.x >= MAIN_VOL_X &&
      p.x < MAIN_VOL_X + MAIN_VOL_W &&
      p.y >= MAIN_VOL_Y &&
      p.y < MAIN_VOL_Y + MAIN_VOL_H) {

    // MUTE processing.
    if (p.x >= MAIN_MUT_X &&
      p.x < MAIN_MUT_X + MAIN_MUT_W &&
      p.y >= MAIN_MUT_Y &&
      p.y < MAIN_MUT_Y + MAIN_MUT_H) {

      mainMuted = !mainMuted;

      if (mainMuted) {
        Serial.println("MAIN MUTE ON");
        sendDenonCommand("MUON");
      } else {
        Serial.println("MAIN MUTE OFF");
        sendDenonCommand("MUOFF");
      }

      tft.fillRect(
        MAIN_VOL_X + 65,
        MAIN_VOL_Y + 5,
        MAIN_VOL_X + 50,
        MAIN_VOL_H - 10,
        TFT_BLACK);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextSize(2);

      if (mainMuted) {
        tft.setCursor(MAIN_VOL_X + 80, MAIN_VOL_Y + 22);
        tft.print("MUTED    ");
      } else {
        tft.setCursor(MAIN_VOL_X + 80, MAIN_VOL_Y + 22);
        tft.print("Vol ");
        tft.print(parseMainVolume(lastMVresponse));
      }

    } else {

      String response;

      if (p.x < MAIN_VOL_X + MAIN_VOL_W / 2) {
        Serial.println("MAIN VOLUME DOWN");
        response = sendDenonCommand("MVDOWN");
      } else {
        Serial.println("MAIN VOLUME UP");
        response = sendDenonCommand("MVUP");
      }

      if (response.length() > 0) {
        lastMVresponse = response;

        // Clear the volume display area, but not the MUTE button.
        tft.fillRect(
          MAIN_VOL_X + 65,
          MAIN_VOL_Y + 5,
          MAIN_VOL_X + 50,
          MAIN_VOL_H - 10,
          TFT_BLACK);

        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(MAIN_VOL_X + 80, MAIN_VOL_Y + 22);
        tft.print("Vol ");
        tft.print(parseMainVolume(response));
      }
    }
  }
  else if (p.x >= ZONE2_VOL_X &&
         p.x < ZONE2_VOL_X + ZONE2_VOL_W &&
         p.y >= ZONE2_VOL_Y &&
         p.y < ZONE2_VOL_Y + ZONE2_VOL_H) {

         // MUTE processing
         if (p.x >= ZONE2_MUT_X &&
           p.x < ZONE2_MUT_X + ZONE2_MUT_W &&
           p.y >= ZONE2_MUT_Y &&
           p.y < ZONE2_MUT_Y + ZONE2_MUT_H) {

         zone2Muted = !zone2Muted;

         if (zone2Muted) {
           Serial.println("ZONE 2 MUTE ON");
           sendDenonCommand("Z2MUON");
         } else {
           Serial.println("ZONE 2 MUTE OFF");
           sendDenonCommand("Z2MUOFF");
         }

         tft.fillRect(
           ZONE2_VOL_X + 65,
           ZONE2_VOL_Y + 5,
           ZONE2_VOL_X + 50,
           ZONE2_VOL_H - 10,
           TFT_BLACK);

         tft.setTextColor(TFT_WHITE, TFT_BLACK);
         tft.setTextSize(2);

         if (zone2Muted) {
           tft.setCursor(ZONE2_VOL_X + 80, ZONE2_VOL_Y + 22);
           tft.print("MUTED    ");
         } else {
           tft.setCursor(ZONE2_VOL_X + 80, ZONE2_VOL_Y + 22);
           tft.print("Vol ");
           tft.print(parseZone2Volume(lastZ2response));
         }
      } else {

        String response;

        if (p.x < ZONE2_VOL_X + ZONE2_VOL_W / 2) {
          Serial.println("ZONE 2 VOLUME DOWN");
          response = sendDenonCommand("Z2DOWN");
        } else {
          Serial.println("ZONE 2 VOLUME UP");
          response = sendDenonCommand("Z2UP");
        }

        lastZ2response = response;

        // Clear the volume display area, but not the MUTE button.
        tft.fillRect(
          ZONE2_VOL_X + 65,
          ZONE2_VOL_Y + 5,
          ZONE2_VOL_X + 50,
          ZONE2_VOL_H - 10,
          TFT_BLACK
        );

        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(ZONE2_VOL_X + 80, ZONE2_VOL_Y + 22);
        tft.print("Vol ");
        tft.print(parseZone2Volume(response));
      }
    }

    // causes an auto-repeat type action ever 200ms if 
    // touch area held down
    delay(200);
  }
}
