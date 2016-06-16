/*
espDMX library
Copyright (c) 2016, Matthew Tong
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "Arduino.h"

extern "C" {
#include "osapi.h"
#include "ets_sys.h"
#include "mem.h"
#include "user_interface.h"
}

#include "espDMX.h"

#define UART_TX_FIFO_SIZE 0x80

struct dmx_ {
    uint8_t dmx_nr;
    uint8_t txPin;
    uint8_t ledPin;
    uint8_t ledIntensity;
    uint8_t state;
    uint16_t numChans;
    uint16_t txChan;
    byte data[512];
    long full_uni_time;
    long led_timer;
};

espDMX dmxA(0);
espDMX dmxB(1);

void dmx_interrupt_handler(dmx_t* dmx);

uint16_t dmx_get_tx_fifo_room(dmx_t* dmx);
void dmx_transmit(dmx_t* dmx);
void dmx_interrupt_enable(dmx_t* dmx);
void dmx_interrupt_arm(dmx_t* dmx);
void dmx_interrupt_disarm(dmx_t* dmx);
void dmx_set_baudrate(dmx_t* dmx, int baud_rate);
void dmx_set_chans(dmx_t* dmx, uint8_t* data, uint16_t numChans, uint16_t startChan);
int dmx_state(dmx_t* dmx);
void dmx_flush(dmx_t* dmx);
static void uart_ignore_char(char c);

dmx_t* dmx_init(int dmx_nr, int ledPin);
void dmx_uninit(dmx_t* dmx);

void ICACHE_RAM_ATTR dmx_interrupt_handler(dmx_t* dmx) {
  if(U0IS & (1 << UIFE)) {    // check status flag
    U0IC = (1 << UIFE);       // clear status flag
    dmxA._tx_empty_irq();     // do interupt
  }

  if(U1IS & (1 << UIFE)) {    // check status flag
    U1IC = (1 << UIFE);       // clear status flag
    dmxB._tx_empty_irq();     // do interupt
  }
}

uint16_t dmx_get_tx_fifo_room(dmx_t* dmx) {
    if(dmx == 0)
        return 0;
    return UART_TX_FIFO_SIZE - ((USS(dmx->dmx_nr) >> USTXC) & 0xff);
}

void dmx_transmit(dmx_t* dmx) {
  if(dmx == 0)
    return;
  
  if (dmx->state == DMX_DATA || dmx->state == DMX_FULL_UNI) {
    uint16_t txSize = ((dmx->state == DMX_FULL_UNI) ? 512 : dmx->numChans) - dmx->txChan;
    
    if (txSize > 0) {
      uint16_t fifoRoom = dmx_get_tx_fifo_room(dmx);
      
      txSize = (txSize > fifoRoom) ? fifoRoom : txSize;
      
      for(; txSize; --txSize)
        USF(dmx->dmx_nr) = dmx->data[dmx->txChan++];

      return;
    } else {
      dmx->state = DMX_START;
      
      return;
    }
  }
  
  if (dmx->state == DMX_START) {
    dmx_interrupt_disarm(dmx);
    
    // Wait for empty FIFO
    while (((USS(dmx->dmx_nr) >> USTXC) & 0xff) != 0) {
      yield();
    }

    // Allow last channel to be fully sent
    delayMicroseconds(44);
    
    // BREAK of ~120us
    pinMode(dmx->txPin, OUTPUT);
    digitalWrite(dmx->txPin, LOW);
    delayMicroseconds(115);

    // Reset channel counter
    dmx->txChan = 0;

    // Output full universe (512 channels) once every x seconds
    if (millis() > dmx->full_uni_time + DMX_FULL_UNI_TIMING) {
      dmx->full_uni_time = millis();
      dmx->state = DMX_FULL_UNI;

      // Status LED
      if (dmx->ledPin != DMX_NO_LED) {
        if (dmx->led_timer++ >= 2) {
          analogWrite(dmx->ledPin, dmx->ledIntensity);
          dmx->led_timer = 0;
        }else
          analogWrite(dmx->ledPin, 0);
      }
    } else
      dmx->state = DMX_DATA;
    
    // MAB of ~12us
    digitalWrite(dmx->txPin, HIGH);
    delayMicroseconds(7);
    
    // UART at 250k for DMX data
    pinMode(dmx->txPin, SPECIAL);
    dmx_set_baudrate(dmx, DMX_TX_BAUD);
    USC0(dmx->dmx_nr) = DMX_TX_CONF;
    
    // Empty FIFO
    dmx_flush(dmx);
  
    // DMX Start Code 0
    USF(dmx->dmx_nr) = 0;
    
    dmx_interrupt_arm(dmx);
  }
}

void dmx_flush(dmx_t* dmx) {
    if(dmx == 0)
        return;

    uint32_t tmp = 0x00000000;
    tmp |= (1 << UCTXRST);

    // Clear TX Fifo
    USC0(dmx->dmx_nr) |= (tmp);
    USC0(dmx->dmx_nr) &= ~(tmp);
}

void dmx_interrupt_enable(dmx_t* dmx) {
    if(dmx == 0)
        return;

    // Clear all 9 interupt bits
    USIC(dmx->dmx_nr) = 0x1ff;

    // Set TX Fifo Empty trigger point
    uint32_t conf1 = 0x00000000;
    conf1 |= (0x00 << UCFET);
    USC1(dmx->dmx_nr) = conf1;

    // Attach out interupt handler function
    ETS_UART_INTR_ATTACH(&dmx_interrupt_handler, dmx);

    // Disable RX Fifo Full Interupt
    USIE(dmx->dmx_nr) &= ~(1 << UIFF);
    
    ETS_UART_INTR_ENABLE();

}

void dmx_interrupt_arm(dmx_t* dmx) {
    if(dmx == 0)
        return;

    // Enable TX Fifo Empty Interupt
    USIE(dmx->dmx_nr) |= (1 << UIFE);
}

void dmx_interrupt_disarm(dmx_t* dmx) {
    if(dmx == 0)
        return;
    USIE(dmx->dmx_nr) &= ~(1 << UIFE);
    //ETS_UART_INTR_DISABLE(); // never disable irq complete may its needed by the other Serial Interface!
}

void dmx_set_baudrate(dmx_t* dmx, int baud_rate) {
    if(dmx == 0)
        return;
    USD(dmx->dmx_nr) = (ESP8266_CLOCK / baud_rate);
}

dmx_t* dmx_init(int dmx_nr, int ledPin) {
    dmx_t* dmx = (dmx_t*) os_malloc(sizeof(dmx_t));

    if(dmx == 0) {
      os_free(dmx);
      return 0;
    }

    system_set_os_print(0);
    ets_install_putc1((void *) &uart_ignore_char);
    
    // Initialize variables
    dmx->dmx_nr = dmx_nr;
    dmx->ledPin = ledPin;
    dmx->ledIntensity = 255;
    dmx->txPin = (dmx->dmx_nr == 0) ? 1 : 2;
    dmx->state = DMX_STOP;
    dmx->numChans = 0;
    dmx->txChan = 0;
    dmx->full_uni_time = 0;
    dmx->led_timer = 0;

    // Initialize empty DMX buffer
    for (int i = 0; i < 512; i++)
      dmx->data[i] = 0;

    if (dmx->ledPin != DMX_NO_LED) {
      // Status LED initalize
      pinMode(dmx->ledPin, OUTPUT);
      digitalWrite(dmx->ledPin, LOW);
    }

    // TX output set to idle
    pinMode(dmx->txPin, OUTPUT);
    digitalWrite(dmx->txPin, HIGH);
     
    dmx_interrupt_enable(dmx);
    
    return dmx;
}

void dmx_uninit(dmx_t* dmx) {
    if(dmx == 0)
        return;
    dmx_interrupt_disarm(dmx);

    pinMode(dmx->txPin, OUTPUT);
    digitalWrite(dmx->txPin, HIGH);
    
    os_free(dmx);
}

int dmx_get_state(dmx_t* dmx) {
  return dmx->state;
}

void dmx_set_state(dmx_t* dmx, int state) {
  dmx->state = state;
}

void dmx_set_chans(dmx_t* dmx, uint8_t* data, uint16_t num, uint16_t start) {
  uint16_t newNum = start + num - 1;
  if (newNum > 512)
    newNum = 512;

  // Is there any new channel data
  if (memcmp(data, &(dmx->data[start-1]), num) != 0) {
    // Find the highest channel with new data
    for (; newNum >= dmx->numChans; newNum--, num--) {
      if (dmx->data[newNum-1] != data[num-1])
        break;
    }

    newNum += 30;
    dmx->numChans = (newNum > 512) ? 512 : newNum;
    
    // Put data into our buffer
    memcpy(&(dmx->data[start-1]), data, num);
  }

  // Status LED
  if (dmx->ledPin != DMX_NO_LED) {
    if (dmx->led_timer++ >= 2) {
      analogWrite(dmx->ledPin, dmx->ledIntensity);
      dmx->led_timer = 0;
    }else
      analogWrite(dmx->ledPin, 0);
  }

  if (dmx->state == DMX_STOP) {
    dmx->state = DMX_START;
    dmx_transmit(dmx);
  }
}

espDMX::espDMX(uint8_t dmx_nr) :
  _dmx_nr(dmx_nr), _dmx(0) {
}

void espDMX::begin(uint8_t ledPin) {
    _dmx = dmx_init(_dmx_nr, ledPin);

    if(_dmx== 0)
        return;

    delay(5);
}

void espDMX::end() {
  if(_dmx== 0)
    return;
  dmx_uninit(_dmx);
  _dmx = 0;
}

void espDMX::setChans(byte *data, uint16_t numChans, uint16_t startChan) {
  if(_dmx== 0)
    return;
  dmx_set_chans(_dmx, data, numChans, startChan);
}

void espDMX::ledIntensity(uint8_t newIntensity) {
  if (_dmx == 0)
    return;
  _dmx->ledIntensity = newIntensity;
}

void espDMX::_tx_empty_irq(void) {
  if(_dmx == 0)
    return;

  dmx_transmit(_dmx);
}

int espDMX::available(void) { return 0; }
int espDMX::peek(void) { return 0; }
int espDMX::read(void) { return 0; }
void espDMX::flush(void) { return; }
size_t espDMX::write(uint8_t) { return 0; }
static void uart_ignore_char(char c) { return; }

espDMX::operator bool() const {
    return _dmx != 0;
}

