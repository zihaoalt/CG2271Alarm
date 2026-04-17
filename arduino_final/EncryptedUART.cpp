#include "EncryptedUART.h"
#include <string.h>
#include <WiFi.h>
#include <time.h>


// ==================== USER CONFIG ========================================
static const uint32_t LINK_BAUD  = 9600;
static const int      PIN_RX2    = 18;   // Serial1 RX  (from MCXC444 TX)
static const int      PIN_TX2    = 17;   // Serial1 TX  (to   MCXC444 RX)

// =============== CHACHA20 (RFC 8439) — MUST MATCH MCXC444 ================

/* 32-byte shared key. Same bytes on both boards. */
static const uint8_t KEY[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

/* ESP32 transmits to MCXC444 -> we are the "ESP32 -> MCXC444" direction. */
static const uint8_t TX_NONCE[12] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,   // 
};

// MCXC444 -> ESP32 direction  (must match MCXC's tx_nonce = ...0x00)
static const uint8_t RX_NONCE[12] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01,   // 
};

static inline uint32_t rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

#define CHACHA_QR(a, b, c, d)                  \
    do {                                       \
        a += b; d ^= a; d = rotl32(d, 16);     \
        c += d; b ^= c; b = rotl32(b, 12);     \
        a += b; d ^= a; d = rotl32(d,  8);     \
        c += d; b ^= c; b = rotl32(b,  7);     \
    } while (0)

/* Generate one 64-byte keystream block for (key, nonce, counter). */
void chacha20_block(const uint8_t key[32], const uint8_t nonce[12],
                    uint32_t counter, uint8_t out[64])
{
    uint32_t state[16];

    /* "expand 32-byte k" as 4 little-endian 32-bit words. */
    state[0] = 0x61707865u;   /* "expa" */
    state[1] = 0x3320646eu;   /* "nd 3" */
    state[2] = 0x79622d32u;   /* "2-by" */
    state[3] = 0x6b206574u;   /* "te k" */

    for (int i = 0; i < 8; i++) {
        state[4 + i] =  (uint32_t)key[4*i]
                     | ((uint32_t)key[4*i + 1] <<  8)
                     | ((uint32_t)key[4*i + 2] << 16)
                     | ((uint32_t)key[4*i + 3] << 24);
    }

    state[12] = counter;

    for (int i = 0; i < 3; i++) {
        state[13 + i] =  (uint32_t)nonce[4*i]
                      | ((uint32_t)nonce[4*i + 1] <<  8)
                      | ((uint32_t)nonce[4*i + 2] << 16)
                      | ((uint32_t)nonce[4*i + 3] << 24);
    }

    uint32_t x[16];
    memcpy(x, state, sizeof(state));

    for (int i = 0; i < 10; i++) {
        CHACHA_QR(x[0], x[4], x[ 8], x[12]);
        CHACHA_QR(x[1], x[5], x[ 9], x[13]);
        CHACHA_QR(x[2], x[6], x[10], x[14]);
        CHACHA_QR(x[3], x[7], x[11], x[15]);
        CHACHA_QR(x[0], x[5], x[10], x[15]);
        CHACHA_QR(x[1], x[6], x[11], x[12]);
        CHACHA_QR(x[2], x[7], x[ 8], x[13]);
        CHACHA_QR(x[3], x[4], x[ 9], x[14]);
    }

    for (int i = 0; i < 16; i++) {
        uint32_t v = x[i] + state[i];
        out[4*i    ] = (uint8_t)(v      );
        out[4*i + 1] = (uint8_t)(v >>  8);
        out[4*i + 2] = (uint8_t)(v >> 16);
        out[4*i + 3] = (uint8_t)(v >> 24);
    }
}

// ==================== FRAMING (Serial1) ==================================
#define FRAME_LEN    64
#define MAX_PAYLOAD  59

static uint32_t tx_counter = 0;
static uint8_t  rx_buf[FRAME_LEN];
static int      rx_idx = 0;

/* Encrypt `msg` and transmit a 64-byte frame to the MCXC444. */
void sendEncryptedMessage(const char *msg) {
    uint8_t frame[FRAME_LEN];
    memset(frame, 0, FRAME_LEN);

    size_t len = strlen(msg);
    if (len > MAX_PAYLOAD) len = MAX_PAYLOAD;

    /* Big-endian counter header (plaintext). */
    frame[0] = (uint8_t)(tx_counter >> 24);
    frame[1] = (uint8_t)(tx_counter >> 16);
    frame[2] = (uint8_t)(tx_counter >>  8);
    frame[3] = (uint8_t)(tx_counter      );

    frame[4] = (uint8_t)len;
    memcpy(&frame[5], msg, len);

    uint8_t ks[64];
    chacha20_block(KEY, TX_NONCE, tx_counter, ks);
    for (int i = 0; i < 60; i++) {
        frame[4 + i] ^= ks[i];
    }

    tx_counter++;
    Serial1.write(frame, FRAME_LEN);
    Serial1.flush();
}

/* Decrypt a 64-byte ciphertext frame into `out` (null-terminated).
 * Returns payload length, or -1 on error. */
int onFrameReceived(const uint8_t *cipher64, char *out, size_t out_size) {
    uint32_t ctr = ((uint32_t)cipher64[0] << 24)
                 | ((uint32_t)cipher64[1] << 16)
                 | ((uint32_t)cipher64[2] <<  8)
                 | ((uint32_t)cipher64[3]      );

    uint8_t ks[64];
    chacha20_block(KEY, RX_NONCE, ctr, ks);

    uint8_t plain[60];
    for (int i = 0; i < 60; i++) {
        plain[i] = cipher64[4 + i] ^ ks[i];
    }

    uint8_t len = plain[0];
    if (len > MAX_PAYLOAD || (size_t)(len + 1) > out_size) return -1;

    memcpy(out, &plain[1], len);
    out[len] = '\0';
    return (int)len;
}


bool pollEncryptedUART(String &outMessage) {
    // Keep track of the last time a byte arrived
    static unsigned long lastRxTime = 0; 

    // --- THE FIX: AUTO-RECOVERY TIMEOUT ---
    // If we have partial bytes, but 50ms have passed with silence, 
    // we lost a byte. Flush the system to realign!
    if (rx_idx > 0 && (millis() - lastRxTime > 50)) {
        Serial.println("[RX] Sync lost! Flushing buffer to auto-recover.");
        rx_idx = 0;
        // Dump any lingering garbage in the hardware buffer
        while(Serial1.available()) Serial1.read(); 
    }

    while (Serial1.available()) {
        lastRxTime = millis(); // Record the exact time this byte arrived
        
        uint8_t b = (uint8_t)Serial1.read();
        if (rx_idx < FRAME_LEN) rx_buf[rx_idx++] = b;

        if (rx_idx >= FRAME_LEN) {
            char plain[FRAME_LEN];
            int n = onFrameReceived(rx_buf, plain, sizeof(plain));
            
            rx_idx = 0; // Reset for the next packet
            
            if (n >= 0) {
                outMessage = String(plain);
                
                if (outMessage.indexOf("GET_TIME") >= 0) {
                    // sendCurrentTimeReply();
                    return false; 
                }
                return true; 
            } else {
                Serial.println("[RX] bad frame dropped");
                
                // --- AGGRESSIVE RECOVERY ---
                // If a frame fails decryption, instantly flush the rest of the 
                // hardware buffer so the next packet starts with a clean slate.
                while(Serial1.available()) Serial1.read();
            }
        }
    }
    return false;
}


void initEncryptedUART() {
    Serial1.begin(LINK_BAUD, SERIAL_8N1, PIN_RX2, PIN_TX2);
    Serial.println("ESP32 encrypted-UART link ready.");
}