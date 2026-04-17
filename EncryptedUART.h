#ifndef ENCRYPTED_UART_H
#define ENCRYPTED_UART_H

#include <Arduino.h>

// Initialize the Serial1 pins and Wi-Fi for time sync
void initEncryptedUART();

// Returns true if a message was successfully decrypted, and fills outMessage
bool pollEncryptedUART(String &outMessage);

// Send an encrypted message to the MCX
void sendEncryptedMessage(const char *msg);


#endif