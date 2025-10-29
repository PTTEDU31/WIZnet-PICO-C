#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include "hardware/uart.h"


#define LED_PIN 25   // LED on-board Pico


// ==============================
// UART1: Modbus RTU (RS-485)
// ==============================
#define MODBUS_UART         uart1
#define MODBUS_TX_PIN       4       // TX (GPIO0)
#define MODBUS_RX_PIN       5       // RX (GPIO1)
#define RS485_DIR_PIN       6       // DE/RE control pin
#define MODBUS_BAUDRATE     115200 

// ==============================
// Modbus Register Config
// ==============================
#define SLAVE_ID            0x01
#define REG_START_ADDR      0x0000
#define REG_QTY             4



#endif
