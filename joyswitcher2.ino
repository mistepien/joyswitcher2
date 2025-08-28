//IMPORTANT FOR ATTINY13 + MicroCore (board in Arduino IDE)!!!

//USBASP with new firmware in mode USBasp-slow works like a charm even in 128kHz

//USBtinyISP (tested with Sparkfun (Tiny AVR Programmer PGM-11801) and USBISP based on
//ATtiny 2313 DO NOT work with 128kHz.


/*---------------------------------------------------
  |   DDRx   |   PORTx  |    result                 |
  --------------------------------------------------|
  --------------------------------------------------|
  |    0     |     0    |  INPUT / Tri-state (hi-Z) |
  ---------------------------------------------------
  |    0     |     1    |  INPUT_PULLUP             |
  ---------------------------------------------------
  |    1     |     0    | OUTPUT (LOW)              |
  ---------------------------------------------------
  |    1     |     1    | OUTPUT (HIGH)             |
  ---------------------------------------------------


  LOW(0) state of both registers is DEFAULT,
  thus every pin is in INPUT mode without doing anything.

  ------------------            -----------------
  |  HARDWARE XOR  |            |  SOFTWARE XOR |
  ------------------            -----------------
  PINx = byte;    <========>    PORTx ^= byte;

  ------------------
  |  HARDWARE XOR  |
  ------------------

  "The port input pins I/O location is read only, while the data register and the
  data direction register are read/write. However, writing a logic one to a bit
  in the PINx register, will result in a toggle in the
  corresponding bit in the data register."

  That is more efficient since XOR operation is done in hardware, not software,
  and it saves cycles since in code there is no need to bother about XOR.*/


#include <avr/power.h>
#include <avr/sleep.h>
#include <EEPROM.h>



#define _nop() __asm__ volatile("nop")

#if defined(__AVR_ATtiny13__) || defined(__AVR_ATtiny13A__)
enum pins {
  TOGGLE_BTN = 1,
  TOGGLE_PORT = 3,
  ENABLE_OUT = 4,
  UNUSED0 = 0,
  UNUSED1 = 2
};
#define WDTIE_WDIE WDTIE
#elif defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
enum pins {
  TOGGLE_BTN = 2,
  TOGGLE_PORT = 3,
  ENABLE_OUT = 4,
  UNUSED0 = 0,
  UNUSED1 = 1
};
#define WDTIE_WDIE WDIE
#endif

#if defined(__AVR_ATtiny85__)
constexpr byte _max_eeprom_cells = 255;
#else
constexpr byte _max_eeprom_cells = E2END + 1;
#endif

byte _toggle_port_state;
byte _index;  //indicate current cell -- that is always kept in cell 0

byte EEPROM_save_toggle_port_state(byte value) {
  switch (value) {
    case 255:
      if (_index < (_max_eeprom_cells - 1)) {
        _index++;
      } else {
        _index = 1;
      }
      //_index = _index < (_max_eeprom_cells - 1) ? _index + 1 : 1;
      value = 0;
      break;
    default:
      value++;
      break;
  }
  EEPROM.update(0, _index);
  EEPROM.write(_index, value);
  return value;
}

byte EEPROM_initiate() {
  _index = EEPROM.read(0);
  byte port_state;
  switch (_index) {
    case 0:
    case _max_eeprom_cells ... 255:
      _index = 1;
      port_state = 0;
      //EEPROM.update(0, _index);
      //EEPROM.update(_index, port_state);
      break;
    default:
      port_state = EEPROM.read(_index);
      break;
  }
  return port_state;
}

void setup() {
  bitSet(PORTB, ENABLE_OUT);  //set ENABLE_OUT_PIN to HIGH
  bitSet(DDRB, ENABLE_OUT);   //set ENABLE_OUT_PIN as OUTPUT

  //bitClear(ADCSRA, ADEN);
  //bitSet(ACSR,ACD);
  power_all_disable();  //https://www.nongnu.org/avr-libc/user-manual/group__avr__power.html

  //input for TOGGLEBTN and UNUSED PINS
  //DDRB &= ~(bit(TOGGLE_BTN) | bit(UNUSED0) | bit(UNUSED1)); //INPUT(0) is default

  //input_pullup for UNUSED PINS
  PORTB |= (bit(UNUSED0) | bit(UNUSED1));

  //OUTPUT + read EEPROM
  _toggle_port_state = EEPROM_initiate();
  bitSet(DDRB, TOGGLE_PORT);  //OUTPUT (LOW BY DEFAULT)
  if (_toggle_port_state & 1) {
    bitSet(PORTB, TOGGLE_PORT);
  }

  _nop();

  bitClear(PORTB, ENABLE_OUT);  //set ENABLEOUTPUT to LOW -- LOW enables 4053s


  //enable_INT0();
  set_sleep_mode(SLEEP_MODE_IDLE);

  //WDTCR &= ~(bit(WDP1) | bit(WDP0) | bit(WDP2) | bit(WDP3)); //16ms
  // WDTCR = (WDTCR & ~(bit(WDP1) |bit(WDP2) | bit(WDP3)   )) | bit(WDP0); //32ms
  //WDTCR = (WDTCR & ~(bit(WDP0) | bit(WDP2) | bit(WDP3))) | bit(WDP1);  //64
  WDTCR = (WDTCR & ~(bit(WDP2) | bit(WDP3))) | (bit(WDP0) | bit(WDP1));  //125ms
  //WDTCR = (WDTCR & ~(bit(WDP0) | bit(WDP1) | bit(WDP3))) | bit(WDP2);  //250ms
  //WDTCR = (WDTCR & ~(bit(WDP1) | bit(WDP3))) | (bit(WDP0) | bit(WDP2));  //0.5s
}

void enable_INT0() {
  /*
    The falling edge of INT0 generates an interrupt request
    falling/rising edge allows to wake-up only from IDLE, since it need
    clock -- in Power-Down clock is off so falling/rising cannot wake-up
    from Power-Down
    https://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny13A-Data-Sheet-DS40002307A.pdf
    page 36, table 7-1
  */
  MCUCR = (MCUCR & ~(bit(ISC00))) | bit(ISC01);  //The falling edge of INT0 generates an interrupt request.
  GIFR = 1 << INTF0;                             //purge interrupt flag so that we don't get a spurious interrupt immediately
  GIMSK = 1 << INT0;
}

void disable_INT0() {
  MCUCR &= ~(bit(ISC00) | bit(ISC01));
  GIMSK = 0;
}

void gosleep() {
  sleep_bod_disable();
  sleep_mode();
}

void swap_ports() {
  PINB = bit(ENABLE_OUT);  //when changing port disable all channels for moment
  PINB = bit(TOGGLE_PORT);
  //no need for EEPROM.update since just line _toggle_port_state was flipped
  _toggle_port_state = EEPROM_save_toggle_port_state(_toggle_port_state);
  PINB = bit(ENABLE_OUT);
}


void _delay_wd() {
  bitSet(WDTCR, WDTIE_WDIE);  // Enable watchdog timer interrupts
  gosleep();
  bitClear(WDTCR, WDTIE_WDIE);  //Disable watchdog timer interrupts
}


void loop() {
  enable_INT0();
  gosleep();

  /*that is sleeping and waiting to be awaken by the INT0
    
    after awaking ports are swapped and loop starts again with sleep
  */
  disable_INT0();
  swap_ports();
  _delay_wd();
}

int main(void) {  //the only thing we need here from init() is interrupts()/sei;
  interrupts();

  setup();

  while (1) {
    loop();
  }

  return 0;
}
