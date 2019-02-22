#include <Arduino.h>
#include <Joystick.h>

//#define DEBUG
#include "_mylibs.h"

//-----------------------------------------------------------------------------------------------------------------------------------------//
// Common

#define PHASE_WAIT_FOR_FADE_1 0
#define PHASE_WAIT_FOR_FADE_2 1
#define PHASE_RECEIVE_DATA    2
#define PHASE_PROCESSING_DATA 3

uint8_t phase = PHASE_WAIT_FOR_FADE_1;

//-----------------------------------------------------------------------------------------------------------------------------------------//
// Source

// Specification: https://www.spektrumrc.com/ProdInfo/Files/Remote%20Receiver%20Interfacing%20Rev%20A.pdf

// Spektrum channels layout
enum {
  SRC_CHANID_THROTTLE,
  SRC_CHANID_AILERON,
  SRC_CHANID_ELEVATOR,
  SRC_CHANID_RUDDER,
  SRC_CHANID_PITCH,
  SRC_CHANID_AUX_1,
  SRC_CHANID_AUX_2,
  SRC_CHANID_AUX_3,
  SRC_CHANID_AUX_4,
  SRC_CHANID_AUX_5,
  SRC_CHANID_AUX_6,
  SRC_CHANID_AUX_7,
  SRC_CHANID_LAST
};

// Port settings
#define srcPort Serial1
#define SRC_PORT_BAUDRATE 115200 // 125000 according to the specification, but doesn't work...
#define SRC_PORT_OPTIONS  SERIAL_8N1

// Obtained after the data stream monitoring.
// I didn't understand why such data and I didn't find anything like this in the specification. Perhaps this is a feature of the VBC.
#define SRC_FADE_1 0xF3 // 243
#define SRC_FADE_2 0x17 // 23

#define SRC_FRAME_CHAN_COUNT 7

#define SRC_MASK_2048_CHANID 0x7800
#define SRC_MASK_2048_POS    0x07FF

byte_t buf[sizeof(uint16_t)*SRC_FRAME_CHAN_COUNT];
uint8_t bufIndex = 0;

uint16_t channels[SRC_CHANID_LAST];

void srcInit() {
  srcPort.begin(SRC_PORT_BAUDRATE, SRC_PORT_OPTIONS);
}

//-----------------------------------------------------------------------------------------------------------------------------------------//
// Joystick

#define PULSE_WIDTH_MIN  344 // Minimal pulse (experimental)
#define PULSE_WIDTH_MAX 1704 // Maximal pulse (experimental)
#define PULSE_WIDTH_MID 1024 // Middle pulse = (MIN_PULSE_WIDTH + MAX_PULSE_WIDTH)/2
#define PULSE_JITTER       1 // Dead zone. If possible, do not use it.

#define USB_STICK_MIN -32767
#define USB_STICK_MAX  32767
#define USB_STICK_MID  0     // (USB_STICK_MIN + USB_STICK_MAX)/2

// Create the Joystick
Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_JOYSTICK, 2, 0, true, true, true, true, true, true, false, false, false, false, false);

void joyInit() {
  Joystick.setXAxisRange(USB_STICK_MIN, USB_STICK_MAX);
  Joystick.setYAxisRange(USB_STICK_MIN, USB_STICK_MAX);
  Joystick.setZAxisRange(USB_STICK_MIN, USB_STICK_MAX);
  Joystick.setRxAxisRange(USB_STICK_MIN, USB_STICK_MAX);
  Joystick.setRyAxisRange(USB_STICK_MIN, USB_STICK_MAX);
  Joystick.setRzAxisRange(USB_STICK_MIN, USB_STICK_MAX); // Not used. But when I disabled it (and set "false" in the corresponding Joystick parameter), then there were some glitches. Didn't understand yet.

  Joystick.begin(false);
}

// Convert a value from range [PULSE_WIDTH_MIN, PULSE_WIDTH_MAX] to range [USB_STICK_MIN, USB_STICK_MAX]
uint16_t joyValue(uint16_t rcVal) {
  if (rcVal > (PULSE_WIDTH_MID + PULSE_JITTER)) {
    return constrain(
             map(rcVal, PULSE_WIDTH_MID, PULSE_WIDTH_MAX, USB_STICK_MID, USB_STICK_MAX),
             USB_STICK_MID,
             USB_STICK_MAX
           );
  }
  else if (rcVal < (PULSE_WIDTH_MID - PULSE_JITTER)) {
    return constrain(
             map(rcVal, PULSE_WIDTH_MIN, PULSE_WIDTH_MID, USB_STICK_MIN, USB_STICK_MID),
             USB_STICK_MIN,
             USB_STICK_MID
           );
  }
  else {
    return USB_STICK_MID;
  }
}

void joySet() {
  Joystick.setXAxis(joyValue(channels[SRC_CHANID_AILERON]));
  Joystick.setYAxis(joyValue(channels[SRC_CHANID_ELEVATOR]));
  Joystick.setZAxis(joyValue(channels[SRC_CHANID_RUDDER]));
  Joystick.setRxAxis(joyValue(channels[SRC_CHANID_THROTTLE]));
  Joystick.setRyAxis(joyValue(channels[SRC_CHANID_PITCH]));
  Joystick.setRzAxis(USB_STICK_MID); // Not used
  Joystick.setButton(0, channels[SRC_CHANID_AUX_1] > PULSE_WIDTH_MID);
  Joystick.setButton(1, channels[SRC_CHANID_AUX_2] > PULSE_WIDTH_MID);
}

//-----------------------------------------------------------------------------------------------------------------------------------------//
// Application initialization procedure

void setup() {
  debugInit();
  srcInit();
  joyInit();
}

//-----------------------------------------------------------------------------------------------------------------------------------------//
// Source data port handler

void getDataFromSrc() {
  while ((phase != PHASE_PROCESSING_DATA) && (srcPort.available() > 0)) {
    byte_t b = srcPort.read();

    switch (phase) {
      case PHASE_WAIT_FOR_FADE_1:
        //debugPrintf("-%d", b);
        if (b == SRC_FADE_1) {
          phase = PHASE_WAIT_FOR_FADE_2;
        }
        break;
      case PHASE_WAIT_FOR_FADE_2:
        //debugPrintf("-%d", b);
        if (b == SRC_FADE_2) {
          phase = PHASE_RECEIVE_DATA;
          bufIndex = 0;
          //debugPrintf("\n");
        } else {
          phase = PHASE_WAIT_FOR_FADE_1;
        }
        break;
      case PHASE_RECEIVE_DATA:
        buf[bufIndex++] = b;
        if (bufIndex == sizeof(buf)) {
          phase = PHASE_PROCESSING_DATA;
          /*
            for (uint8_t i = 0; i < sizeof(buf); i++) {
              debugPrintf("%d ", buf[i]);
            }
            debugPrintf("\n");
          */
        }
        break;
    }
  }
}

//-----------------------------------------------------------------------------------------------------------------------------------------//
// Main loop

void loop() {
  getDataFromSrc();

  if (phase == PHASE_PROCESSING_DATA) {
    phase = PHASE_WAIT_FOR_FADE_1;

    for (uint8_t i = 0; i < SRC_FRAME_CHAN_COUNT; i++) {
      // big-endian!
      uint16_t data = (buf[i * 2] << 8) | buf[i * 2 + 1];
      uint16_t chan = (data & SRC_MASK_2048_CHANID) >> 11;
      uint16_t pos  = data & SRC_MASK_2048_POS;

      /*
        One cannot assume that a packet will have the same data in the same index in the servo[] array in each frame; it is
        necessary that the Channel ID field be examined for each index in every packet.
      */
      if (chan >= SRC_CHANID_LAST) {
        debugPrintf("BAD CHAN %d\n", chan);
      } else {
        channels[chan] = pos;
        debugPrintf("%d=%d ", chan, pos);
      }
    }
    debugPrintf("\n");

    joySet();
    Joystick.sendState();
  }
}

//-----------------------------------------------------------------------------------------------------------------------------------------//
