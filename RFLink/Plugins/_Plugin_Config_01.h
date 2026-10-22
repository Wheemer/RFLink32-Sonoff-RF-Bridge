// ************************************* //
// * Arduino Project RFLink32        * //
// * https://github.com/couin3/RFLink  * //
// * 2018..2020 Stormteam - Marc RIVES * //
// * More details in RFLink.ino file   * //
// ************************************* //

// ****************************************************************************************************************************************
// RFLink List of Plugins
// ****************************************************************************************************************************************
// Here are all plugins listed that are supported and used after compilation.
// When needed additional plugins can be added or selected plugins can be enabled/disabled.
//
// BEWARE OF THE PLUGIN_MAX setting!!       TX: 51      RX: 24

#ifndef _RFLINK_PLUGIN_CONFIG_01_H_
#define _RFLINK_PLUGIN_CONFIG_01_H_

#include "2_Signal.h"
#include "1_Radio.h"

// ****************************************************************************************************************************************
// Translation Plugin for oversized packets due to their breaks/pause being too short between packets
// Used for Flamingo FA500R and various others, do NOT exclude this plugin.
#define PLUGIN_001 // DO NOT CHANGE OR EXCLUDE!!
// ------------------------------------------------------------------------
// -- Any of the following protocols can be excluded whenever not needed --
// ------------------------------------------------------------------------
#define PLUGIN_002 // Lacrosse v2 2300/3600
#define PLUGIN_003 // Kaku : Klik-Aan-Klik-Uit (with code wheel) aka ARC
#define PLUGIN_004 // NewKAKU : Klik-Aan-Klik-Uit with automatic coding aka Intertechno.
#define PLUGIN_005 // Eurodomest
#define PLUGIN_006 // Blyss
#define PLUGIN_007 // Conrad RSL2
#define PLUGIN_008 // Kambrook
#define PLUGIN_009 // X10 RF
#define PLUGIN_010 // TRC02 RGB Switch
#define PLUGIN_011 // Home Confort
#define PLUGIN_012 // Flamingo FA500R
#define PLUGIN_013 // Powerfix/Quigg
#define PLUGIN_014 // Ikea Koppla
#define PLUGIN_015 // Home Easy EU
#define PLUGIN_017 // RTS
#define PLUGIN_016 // Silvercrest
#define PLUGIN_018 // Louvolite
// Environmental sensor decoders are intentionally excluded from this command-focused build.
// -------------------
// Motion Sensors, include when needed
// -------------------
#define PLUGIN_060 // Ajax Chubb Varel 433 MHz. motion sensors
#define PLUGIN_061 // Chinese PIR motion door and window sensors
#define PLUGIN_062 // Chuango Alarm Devices
#define PLUGIN_063 // Oregon PIR/ALARM/LIGHT
#define PLUGIN_064 // Atlantic (and Visonic)
// -------------------
// Doorbells
// -------------------
#define PLUGIN_070 // Select Plus (Action - Quhwa)
#define PLUGIN_071 // Plieger York
#define PLUGIN_072 // Byron SX doorbell
#define PLUGIN_073 // Deltronic doorbell
#define PLUGIN_074 // RL02
#define PLUGIN_075 // Silvercrest
#define PLUGIN_077 // Avantek
// -------------------
// Smoke detectors / Fire Places
// -------------------
#define PLUGIN_080 // Flamingo FA20 / KD101 smoke detector
#define PLUGIN_081 // Mertik Maxitrol / Dru fireplace
#define PLUGIN_082 // Mertik Maxitrol / Dru fireplace
#define PLUGIN_087 // NOX Alarm
// -------------------
// Misc
// -------------------
#define PLUGIN_076 // CAME-TOP432 Gate/Door opener
// -------------------
// 868 MHZ
// -------------------
// -------------------
// Housekeeping
// Unknown packets pass directly to the WebSocket learn workflow.
// -------------------
// ****************************************************************************************************************************************
// RFLink List of Plugins that have TRANSMIT functionality
// ****************************************************************************************************************************************
// Here are all plugins listed that are supported and used after compilation.
// When needed additional plugins can be added or selected plugins can be enabled/disabled.
// ****************************************************************************************************************************************
// ------------------------------------------------------------------------
// -- Any of the following protocols can be excluded whenever not needed --
// ------------------------------------------------------------------------
#define PLUGIN_TX_003           // Kaku : Klik-Aan-Klik-Uit (with code wheel) aka ARC
#define PLUGIN_TX_004 // NewKAKU : Klik-Aan-Klik-Uit with automatic coding aka Intertechno.
// #define PLUGIN_TX_005           // Eurodomest
// #define PLUGIN_TX_006           // Blyss
// #define PLUGIN_TX_007           // Conrad RSL2
// #define PLUGIN_TX_008           // Kambrook
#define PLUGIN_TX_009           // X10 RF
// #define PLUGIN_TX_010		   // TRC02 RGB switch
// #define PLUGIN_TX_011           // Home Confort
// #define PLUGIN_TX_012           // Flamingo FA500R  (11)
// #define PLUGIN_TX_013           // Powerfix/Quigg
// #define PLUGIN_TX_015           // Home Easy EU     (14)
#define PLUGIN_TX_017           // RTS
#define PLUGIN_TX_016           // Silvercrest
#define PLUGIN_TX_018           // Louvolite
// -------------------
// Doorbells
// -------------------
// #define PLUGIN_TX_070           // Select Plus (Action - Quhwa)
// #define PLUGIN_TX_072           // Byron SX doorbell
// #define PLUGIN_TX_073           // Deltronic doorbell
// #define PLUGIN_TX_074           // RL02
#define PLUGIN_TX_077           // Avantek

// -------------------
// Smoke detectors
// -------------------
// #define PLUGIN_TX_080           // Flamingo FA20 / KD101 smoke detector
// #define PLUGIN_TX_082           // Mertik Maxitrol / Dru fireplace
// -------------------
// -------------------
// Alarm Systems
// -------------------
#define PLUGIN_TX_087
// -------------------
// Misc
// -------------------
#define PLUGIN_TX_076               // CAME TOP-432 Door Opener
// -------------------
//       -=#=-
// -------------------

#endif // _RFLINK_PLUGIN_CONFIG_01_H_
