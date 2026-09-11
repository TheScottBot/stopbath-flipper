/*
 * Experiment credentials for the FD4 and FD15 to FD17 hardware experiments.
 *
 * Copy this file to experiment_credentials.h (which git ignores) and put the
 * network you want to test against in it. The values are compiled into the
 * experiment build so a real Wi-Fi code and a real NFC record can be scanned
 * from the device before the appliance side exists.
 *
 * This mechanism exists for the experiments only. The application proper
 * receives its payloads over the link and holds them in memory for as long as
 * they are displayed (specification Part 5), and nothing in it will ever read
 * a credential from a header or from the SD card. Note that the built
 * experiment FAP in dist/ contains these strings, so use a network set up for
 * the purpose.
 *
 * The reserved characters of the Wi-Fi code grammar (semicolon, comma, colon,
 * backslash, double quote) are not escaped by the experiment build, so keep
 * them out of both values. The appliance escapes them properly.
 */
#pragma once

#define EXPERIMENT_WIFI_SSID       "StopBathExample"
#define EXPERIMENT_WIFI_PASSPHRASE "example1"
