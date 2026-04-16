/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_rtc.h"

#include <string.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define UART_TX_PTE22   22 //portE pin22
#define UART_RX_PTE23   23 //portE pin23
#define UART2_INT_PRIO  128

#define RED_PIN         31   // PTE31
#define GREEN_PIN       5    // PTD5
#define BLUE_PIN        29   // PTE29
#define SW2             3    // PTC3
#define MAX_MSG_LEN		64
#define DEBOUNCE_DELAY_MS 200

#define JOY_CENTRE  2048
#define JOY_DEAD    100
#define PUSH_BUTTON 12
#define LONG_PRESS_MS  200u
#define ADC_SE0      0
#define ADC_SE0_PIN  20
#define ADC_SE4a      4
#define ADC_SE4_PIN  21

volatile uint32_t adc_hitcount = 0;

SemaphoreHandle_t sendSema;
SemaphoreHandle_t sw2Sema;
QueueHandle_t send_queue;
QueueHandle_t recv_queue;
QueueHandle_t RTC_queue;
QueueHandle_t alarm_queue;

typedef enum{
	BTN_PRESSED,
	BTN_RELEASED
} btn_raw_t;

typedef struct{
	btn_raw_t type;
	TickType_t tick;
} btn_msg_t;

typedef enum{
	EVT_SHORT_PRESS,
	EVT_LONG_PRESS
} btn_evt_t;

QueueHandle_t g_btn_raw_queue;
QueueHandle_t g_btn_evt_queue;
volatile int recv_ptr=0;
volatile int send_ptr=0;
typedef struct {
	char message[MAX_MSG_LEN];
} TMessage;

typedef enum {
    STATE_IDLE,
    STATE_SET_TIMER,
    STATE_SET_ALARM,
    STATE_TIMER_RUNNING,
    STATE_ALARM_FIRING
} AlarmState_t;

volatile AlarmState_t currentAlarmState = STATE_ALARM_FIRING;//STATE_IDLE;

typedef enum {
    EVT_JOY_LEFT,
    EVT_JOY_RIGHT,
    EVT_JOY_PRESS,
    EVT_JOY_LONG_PRESS,
	EVT_TIMEOUT,
	EVT_JOY_UP,
	EVT_JOY_DOWN,
	EVT_SW2,
	EVT_ALARM_FIRE,
	EVT_SNOOZE
} AlarmEvent_t;

volatile char recv_buffer[MAX_MSG_LEN];
volatile char send_buffer[MAX_MSG_LEN];

#define QLEN	5

volatile uint8_t lockSW2 = 0;

typedef enum { RED, GREEN, BLUE } TLED;

static uint32_t tx_counter = 0;

/* =========================================================================
 *  ChaCha20 block function (RFC 8439) — used in CTR mode for UART framing.
 *  This is a portable reference implementation. It is bit-for-bit compatible
 *  with mbedtls_chacha20, libsodium's crypto_stream_chacha20_ietf, and the
 *  Arduino Crypto library's ChaCha(20)-IETF; swap it for any of those if you
 *  prefer a vetted build — the API on the ESP32 side just has to produce the
 *  same 64-byte keystream block for (key, nonce, counter).
 *
 *  Frame format (64 bytes on the wire, big-endian counter):
 *      [0..3]   counter        (plaintext — receiver reads this to derive ks)
 *      [4]      length         (encrypted)
 *      [5..63]  payload || 0s  (encrypted, up to 59 bytes of payload)
 *
 *  Confidentiality only — no MAC / authentication by design. Each direction
 *  gets its own nonce (tx_nonce, rx_nonce, see below), so the MCXC444's
 *  counter=N and the ESP32's counter=N produce different keystreams and the
 *  two-time-pad attack across directions is not possible.
 * ========================================================================= */

/* 32-byte (256-bit) key — MUST match the ESP32 side. Replace in deployment. */
static const uint8_t key[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

/* Direction-specific nonces — MUST match the ESP32 side (which uses them
 * swapped). The last byte is the direction tag:
 *     0x00 : MCXC444 -> ESP32   (this board's tx_nonce, ESP32's rx_nonce)
 *     0x01 : ESP32  -> MCXC444  (this board's rx_nonce, ESP32's tx_nonce)
 * Because each direction uses a different nonce, MCXC444's counter=N and
 * ESP32's counter=N produce different keystreams, so keystream reuse across
 * directions is impossible. */
static const uint8_t tx_nonce[12] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};
static const uint8_t rx_nonce[12] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01,
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

/* Generate one 64-byte ChaCha20 keystream block for (key, nonce, counter). */
void chacha20_block(const uint8_t key[32], const uint8_t nonce[12],
                    uint32_t counter, uint8_t out[64])
{
    uint32_t state[16];

    /* "expand 32-byte k" loaded as 4 little-endian 32-bit words. */
    state[0] = 0x61707865u;   /* "expa" */
    state[1] = 0x3320646eu;   /* "nd 3" */
    state[2] = 0x79622d32u;   /* "2-by" */
    state[3] = 0x6b206574u;   /* "te k" */

    /* 256-bit key: 8 little-endian 32-bit words. */
    for (int i = 0; i < 8; i++) {
        state[4 + i] =  (uint32_t)key[4*i]
                     | ((uint32_t)key[4*i + 1] <<  8)
                     | ((uint32_t)key[4*i + 2] << 16)
                     | ((uint32_t)key[4*i + 3] << 24);
    }

    /* 32-bit block counter. */
    state[12] = counter;

    /* 96-bit nonce: 3 little-endian 32-bit words. */
    for (int i = 0; i < 3; i++) {
        state[13 + i] =  (uint32_t)nonce[4*i]
                      | ((uint32_t)nonce[4*i + 1] <<  8)
                      | ((uint32_t)nonce[4*i + 2] << 16)
                      | ((uint32_t)nonce[4*i + 3] << 24);
    }

    uint32_t x[16];
    memcpy(x, state, sizeof(state));

    /* 20 rounds = 10 iterations of (column round + diagonal round). */
        for (int i = 0; i < 10; i++) {
            /* column rounds */
            CHACHA_QR(x[0], x[4], x[ 8], x[12]);
            CHACHA_QR(x[1], x[5], x[ 9], x[13]);
            CHACHA_QR(x[2], x[6], x[10], x[14]);
            CHACHA_QR(x[3], x[7], x[11], x[15]);
            /* diagonal rounds */
            CHACHA_QR(x[0], x[5], x[10], x[15]);
            CHACHA_QR(x[1], x[6], x[11], x[12]);
            CHACHA_QR(x[2], x[7], x[ 8], x[13]);
            CHACHA_QR(x[3], x[4], x[ 9], x[14]);
        }

        /* Final state = working + initial; serialize little-endian. */
        for (int i = 0; i < 16; i++) {
            uint32_t v = x[i] + state[i];
            out[4*i    ] = (uint8_t)(v      );
            out[4*i + 1] = (uint8_t)(v >>  8);
            out[4*i + 2] = (uint8_t)(v >> 16);
            out[4*i + 3] = (uint8_t)(v >> 24);
        }
    }
int onFrameReceived(const uint8_t *cipher64, char *out, size_t out_size) {
    uint32_t ctr = ((uint32_t)cipher64[0] << 24)
                 | ((uint32_t)cipher64[1] << 16)
                 | ((uint32_t)cipher64[2] <<  8)
                 | ((uint32_t)cipher64[3]);

    uint8_t ks[64];
    chacha20_block(key, rx_nonce, ctr, ks);

    uint8_t plain[60];
    for (int i = 0; i < 60; i++) {
        plain[i] = cipher64[4 + i] ^ ks[i];
    }

    uint8_t len = plain[0];
    if (len > 59 || len + 1 > out_size) return -1;

    memcpy(out, &plain[1], len);
    out[len] = '\0';
    return len;
}

void sendMessage(const char *msg) {
    uint8_t frame[64] = {0};
    size_t len = strlen(msg);
    if (len > 59) len = 59;

    frame[0] = (tx_counter >> 24) & 0xFF;
    frame[1] = (tx_counter >> 16) & 0xFF;
    frame[2] = (tx_counter >>  8) & 0xFF;
    frame[3] = (tx_counter) & 0xFF;

    frame[4] = (uint8_t)len;

    memcpy(&frame[5], msg, len);

    uint8_t ks[64];
    chacha20_block(key, tx_nonce, tx_counter, ks);

    for (int i = 0; i < 60; i++) {
        frame[4 + i] ^= ks[i];
    }

    tx_counter++;
    xQueueSend(send_queue, (TMessage *)frame, portMAX_DELAY);
}

void initSW2Interrupt() {
    //Disable the interrupt for PORTC
    NVIC_DisableIRQ(PORTC_PORTD_IRQn);
    // ENABLE PORTC
    SIM->SCGC5 |= SIM_SCGC5_PORTC_MASK;
    //CONFIGURE PORTC3 as GPIO
    PORTC->PCR[SW2] &= ~PORT_PCR_MUX_MASK;
    PORTC->PCR[SW2] |= PORT_PCR_MUX(1);

    // Enable pull-up resistor
    PORTC->PCR[SW2] &= ~PORT_PCR_PS_MASK;
    PORTC->PCR[SW2] |= PORT_PCR_PS(1);

    GPIOC->PDDR &= ~(1 << SW2); // Set as input

    PORTC->PCR[SW2] &= ~PORT_PCR_PE_MASK;
    PORTC->PCR[SW2] |= PORT_PCR_PE(1); // Enable pull resistor

    //CONFIGURE THE INTERRUPT FOR FALLING EDGE
    PORTC->PCR[SW2] &= ~PORT_PCR_IRQC_MASK;
    PORTC->PCR[SW2] |= PORT_PCR_IRQC(10);
    //set lower priority for the interrupt
    NVIC_SetPriority(PORTC_PORTD_IRQn, 192);
    NVIC_ClearPendingIRQ(PORTC_PORTD_IRQn);
    NVIC_EnableIRQ(PORTC_PORTD_IRQn);
}


void initButton() {
    NVIC_DisableIRQ(PORTA_IRQn);
    SIM->SCGC5 |= (SIM_SCGC5_PORTA_MASK);

    PORTA->PCR[PUSH_BUTTON] &= ~PORT_PCR_MUX_MASK;
    PORTA->PCR[PUSH_BUTTON] |= PORT_PCR_MUX(1);

    PORTA->PCR[PUSH_BUTTON] &= ~PORT_PCR_PS_MASK;
    PORTA->PCR[PUSH_BUTTON] |= PORT_PCR_PS(1);
    PORTA->PCR[PUSH_BUTTON] &= ~PORT_PCR_PE_MASK;
    PORTA->PCR[PUSH_BUTTON] |= PORT_PCR_PE(1);

    GPIOA->PDDR &= ~(1 << PUSH_BUTTON);

    PORTA->PCR[PUSH_BUTTON] &= ~PORT_PCR_IRQC_MASK;
    PORTA->PCR[PUSH_BUTTON] |= PORT_PCR_IRQC(0b1011);

    NVIC_SetPriority(PORTA_IRQn, 0);
    NVIC_ClearPendingIRQ(PORTA_IRQn);
    NVIC_EnableIRQ(PORTA_IRQn);
}

void setMCGIRClk() {
    MCG->C1 &= ~MCG_C1_CLKS_MASK;
    MCG->C1 |= ((MCG_C1_CLKS(0b01))| MCG_C1_IRCLKEN_MASK);
    MCG->C2 &= ~MCG_C2_IRCS_MASK;
    MCG->C2 |= MCG_C2_IRCS(0b1);
    MCG->SC &= ~MCG_SC_FCRDIV_MASK;
    MCG->SC |= MCG_SC_FCRDIV(0b0);
}

void setTPMClock(){
    setMCGIRClk();
    SIM->SOPT2 &= ~SIM_SOPT2_TPMSRC_MASK;
    SIM->SOPT2 |= SIM_SOPT2_TPMSRC(0b11);
}

void initUART2(uint32_t baud_rate)
{
    NVIC_DisableIRQ(UART2_FLEXIO_IRQn);

    SIM->SCGC4 |= SIM_SCGC4_UART2_MASK;
    SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;

    UART2->C2 &= ~((UART_C2_TE_MASK) | (UART_C2_RE_MASK));

    PORTE->PCR[UART_TX_PTE22] &= ~PORT_PCR_MUX_MASK;
    PORTE->PCR[UART_TX_PTE22] |= PORT_PCR_MUX(4);

    PORTE->PCR[UART_RX_PTE23] &= ~PORT_PCR_MUX_MASK;
    PORTE->PCR[UART_RX_PTE23] |= PORT_PCR_MUX(4);

    uint32_t bus_clk = CLOCK_GetBusClkFreq();
    uint32_t sbr = (bus_clk + (baud_rate * 8)) / (baud_rate * 16);

    UART2->BDH &= ~UART_BDH_SBR_MASK;
    UART2->BDH |= ((sbr >> 8) & UART_BDH_SBR_MASK);
    UART2->BDL = (uint8_t)(sbr & 0xFF);

    UART2->C1 &= ~UART_C1_LOOPS_MASK;
    UART2->C1 &= ~UART_C1_RSRC_MASK;
    UART2->C1 &= ~UART_C1_PE_MASK;
    UART2->C1 &= ~UART_C1_M_MASK;

    UART2->C2 |= UART_C2_RIE_MASK;

    UART2->C2 |= UART_C2_RE_MASK;
    UART2->C2 |= UART_C2_TE_MASK;

    NVIC_SetPriority(UART2_FLEXIO_IRQn, UART2_INT_PRIO);
    NVIC_ClearPendingIRQ(UART2_FLEXIO_IRQn);
    NVIC_EnableIRQ(UART2_FLEXIO_IRQn);
}

void initADC() {
    // Disable & clear interrupt
	NVIC_DisableIRQ(ADC0_IRQn);
	NVIC_ClearPendingIRQ(ADC0_IRQn);
    // Enable clock gating to relevant configurations
	SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;
    // Set pins from Q3 to ADC
	PORTE->PCR[ADC_SE0_PIN] &= ~PORT_PCR_MUX_MASK;
	PORTE->PCR[ADC_SE0_PIN] |= PORT_PCR_MUX(0);
	PORTE->PCR[ADC_SE4_PIN] &= ~PORT_PCR_MUX_MASK;
	PORTE->PCR[ADC_SE4_PIN] |= PORT_PCR_MUX(0);
    // Configure the ADC
    // Enable ADC interrupt
	ADC0->SC1[0] |= ADC_SC1_AIEN_MASK;
    // Select single-ended ADC
	ADC0->SC1[0] &= ~ADC_SC1_DIFF_MASK;
	ADC0->SC1[0] |= ADC_SC1_DIFF(0b0);
    // Set 12 bit conversion
	ADC0->CFG1 &= ~ADC_CFG1_MODE_MASK;
	ADC0->CFG1 |= ADC_CFG1_MODE(0b01);
    // Select software conversion trigger
	ADC0->SC2 &= ~ADC_SC2_ADTRG_MASK;
    // Configure alternate voltage reference
	ADC0->SC2 &= ~ADC_SC2_REFSEL_MASK;
	ADC0->SC2 |= ADC_SC2_REFSEL(0b01);
    // Don't use averaging
	ADC0->SC3 &= ~ADC_SC3_AVGE_MASK;
	ADC0->SC3 |= ADC_SC3_AVGE(0);
    // Switch off continuous conversion.
	ADC0->SC3 &= ~ADC_SC3_ADCO_MASK;
	ADC0->SC3 |= ADC_SC3_ADCO(0);
    // Set highest priority
	NVIC_SetPriority(ADC0_IRQn, 0);
	NVIC_EnableIRQ(ADC0_IRQn);

}

void startADC(int channel) {
	//mask and set the channel
//	ADC0->SC1[0] &= ~ADC_SC1_ADCH_MASK;
//	ADC0->SC1[0] |= ADC_SC1_ADCH(channel);
	ADC0 -> SC1[0] = ADC_SC1_AIEN_MASK | ADC_SC1_ADCH(channel);
}

volatile int results[2];
//IMPORTANT: Change the n below to the right ADC channel
//ADC_XXX and ADC_YYY are the ADC Channels identified in Q3
void ADC0_IRQHandler(){
	static int turn=0;
	//clear pending IRQ
	adc_hitcount += 1;
	NVIC_ClearPendingIRQ(ADC0_IRQn);
	//if conversion is complete, result[turn] = result register
	if(ADC0->SC1[0] & ADC_SC1_COCO_MASK) {

		results[turn] = ADC0->R[0];
		//PRINTF("Turn = %d, Result = %d\r\n", turn, result[turn]);
		turn = 1 - turn;
		if(turn == 0) {
			startADC(ADC_SE0);
		} else {
			startADC(ADC_SE4a);
		}
	}
}

void initPWM() {
    //turn on clock gating to TPM0
	SIM->SCGC6 |= SIM_SCGC6_TPM0_MASK;
	//turn on clock gating to PORTD and PORTE
	SIM->SCGC5 |= (SIM_SCGC5_PORTD_MASK |SIM_SCGC5_PORTE_MASK);
	//configure the RGB LED MUX to be for PWM
	PORTE->PCR[BLUE_PIN] &= ~PORT_PCR_MUX_MASK;
	PORTE->PCR[BLUE_PIN] |= PORT_PCR_MUX(0b11);
	PORTE->PCR[RED_PIN] &= ~PORT_PCR_MUX_MASK;
	PORTE->PCR[RED_PIN] |= PORT_PCR_MUX(0b11);
	PORTD->PCR[GREEN_PIN] &= ~PORT_PCR_MUX_MASK;
	PORTD->PCR[GREEN_PIN] |= PORT_PCR_MUX(0b100);
	//set pins to output (good practice)
	GPIOD->PDDR |= (1 << GREEN_PIN);
	GPIOE->PDDR |= (1 << BLUE_PIN);
	GPIOE->PDDR |= (1 << RED_PIN);
	//the following code is used to setup TPM0
	//turn off TPM0 by clearing the clock mode
	//clear and set the prescalar
	TPM0 -> SC &= ~(TPM_SC_CMOD_MASK | TPM_SC_PS_MASK);
	TPM0->SC|= TPM_SC_PS(0b111);
	//set centre-aligned PWM mode
	TPM0->SC|= TPM_SC_CPWMS_MASK;
	//initialize count to 0
	TPM0 -> CNT = 0;
	//choose and initialize modulo
	TPM0->MOD= 125;
	//Configure TPM0 channels.
	TPM0->CONTROLS[2].CnSC&=~(TPM_CnSC_MSA_MASK|TPM_CnSC_ELSA_MASK);
	TPM0->CONTROLS[2].CnSC |= (TPM_CnSC_MSB(1)|TPM_CnSC_ELSB(1));
	TPM0->CONTROLS[4].CnSC&=~(TPM_CnSC_MSA_MASK|TPM_CnSC_ELSA_MASK);
	TPM0->CONTROLS[4].CnSC |= (TPM_CnSC_MSB(1)|TPM_CnSC_ELSB(1));
	TPM0->CONTROLS[5].CnSC&=~(TPM_CnSC_MSA_MASK|TPM_CnSC_ELSA_MASK);
	TPM0->CONTROLS[5].CnSC |= (TPM_CnSC_MSB(1)|TPM_CnSC_ELSB(1));
	//IMPORTANT: Configure a REVERSE PWM signal!!! (for a reverse signal use high true since the LEDs are active low)
	//i.e. it sets when counting up and clears when counting
	//down. This is because the LEDs are active low.
}

void startPWM() {
	//set CMOD for TPM0
	TPM0 -> SC |= TPM_SC_CMOD(0b1);
}

void stopPWM() {
	//mask CMOD for TPM0
	TPM0->SC &= ~TPM_SC_CMOD_MASK;
}

void setPWM(int LED, int percent) {
    //convert percent into a value
    int value = (int)((percent/100.0)*(double)TPM0->MOD);
    switch(LED) {
       case(RED):
        	TPM0->CONTROLS[4].CnV=value;
        	break;
    // set TPM0 control value
    // repeat for BLUE and GREEN
        case(BLUE):
			TPM0->CONTROLS[2].CnV=value;
        	break;
        case(GREEN):
			TPM0->CONTROLS[5].CnV=value;
        	break;
        default:
        	printf("invalid LED.\r\n");    }
}

void PORTC_PORTD_IRQHandler(void) {
    NVIC_ClearPendingIRQ(PORTC_PORTD_IRQn);

    BaseType_t hpw = pdFALSE;
    if (PORTC->ISFR & (1 << SW2)) {
        PORTC->ISFR |= (1 << SW2); // Clear the interrupt flag

        if (lockSW2 == 0) {
            lockSW2 = 1;
            PORTC->PCR[SW2] &= ~PORT_PCR_IRQC_MASK;   // IRQC = 0 => interrupt disabled

            xSemaphoreGiveFromISR(sw2Sema, &hpw);
            portYIELD_FROM_ISR(hpw);
        }
    }
}

void PORTA_IRQHandler(void)
{
    NVIC_ClearPendingIRQ(PORTA_IRQn);

    if (PORTA->ISFR & (1u << PUSH_BUTTON)) {
        BaseType_t higher_woken = pdFALSE;

        btn_msg_t msg;
        msg.tick = xTaskGetTickCountFromISR();

        bool pin_high = (GPIOA->PDIR & (1u << PUSH_BUTTON)) != 0;
        msg.type = pin_high ? BTN_RELEASED : BTN_PRESSED;

        xQueueSendFromISR(g_btn_raw_queue, &msg, &higher_woken);

        PORTA->ISFR |= (1u << PUSH_BUTTON);
        portYIELD_FROM_ISR(higher_woken);
    }
}

void UART2_FLEXIO_IRQHandler(void)
{
    BaseType_t hpw = pdFALSE;
  char rx_data;

    if(UART2->S1 & UART_S1_RDRF_MASK) // READ
      {
        rx_data = UART2->D;
        /* Assemble a fixed-length 64-byte ciphertext frame. We cannot use
         * a '\n' delimiter here because the ciphertext is random binary
         * and any byte can legally be 0x0A. */
        recv_buffer[recv_ptr++] = rx_data;
        if (recv_ptr >= MAX_MSG_LEN) {
          TMessage msg;
          memcpy(msg.message, recv_buffer, MAX_MSG_LEN);
          xQueueSendFromISR(recv_queue, (void *)&msg, &hpw);
          portYIELD_FROM_ISR(hpw);
          recv_ptr = 0;
        }
      }
    else if (UART2->S1 & UART_S1_TDRE_MASK){ // write
      /* Length-based end-of-frame. '\0' is a legal byte in ciphertext. */
      if (send_ptr >= MAX_MSG_LEN) {
        send_ptr = 0;

        // disable the transmit interrupt
        UART2->C2 &= ~UART_C2_TIE_MASK;

        // disable the transmitter
        UART2->C2 &= ~UART_C2_TE_MASK;

        xSemaphoreGiveFromISR(sendSema, &hpw);
        portYIELD_FROM_ISR(hpw);
      } else {
        UART2->D = send_buffer[send_ptr++];
      }
    }
}

static void ledIndicatorTask(void *p) {
    PRINTF("LED Indicator Task Started\r\n");
    int brightness = 0;
    int step = 5;

    while(1) {
        if (currentAlarmState == STATE_ALARM_FIRING) {
            // Flashing red: turn off green, toggle red rapidly
            setPWM(GREEN, 100);
            setPWM(BLUE, 100);
            setPWM(RED, 0);
            vTaskDelay(pdMS_TO_TICKS(250));
            setPWM(RED, 100);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else{
            // Breathing green: ramp brightness up and down, red off
        	setPWM(BLUE, 100);
            setPWM(RED, 100);
            setPWM(GREEN, brightness);
            brightness += step;
            if (brightness >= 100) {
                brightness = 100;
                step = -5;
            } else if (brightness <= 0) {
                brightness = 0;
                step = 5;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void vTaskButton(void *pv)
{
    btn_msg_t  msg;
    TickType_t press_tick = 0;
    bool       is_down    = false;

    for (;;) {
        if (xQueueReceive(g_btn_raw_queue, &msg, portMAX_DELAY) == pdTRUE) {

            if (msg.type == BTN_PRESSED) {
                press_tick = msg.tick;
                is_down    = true;

            } else if (msg.type == BTN_RELEASED && is_down) {
                is_down = false;

                TickType_t held = msg.tick - press_tick;

                btn_evt_t evt = (held >= pdMS_TO_TICKS(LONG_PRESS_MS))
                                ? EVT_LONG_PRESS
                                : EVT_SHORT_PRESS;

                xQueueSend(g_btn_evt_queue, &evt, 0);
            }
        }
    }
}

void vTaskJoyButtonHandler(void *pv)
{
    btn_evt_t evt;

    for (;;) {
        if (xQueueReceive(g_btn_evt_queue, &evt, portMAX_DELAY) == pdTRUE) {
            if (evt == EVT_SHORT_PRESS) {
                PRINTF("SHORT PRESS\r\n");
                AlarmEvent_t alarmEvt = EVT_JOY_PRESS;
                xQueueSend(alarm_queue, &alarmEvt, portMAX_DELAY);
                sendMessage("JOY:PRESS\n");
            } else if (evt == EVT_LONG_PRESS) {
                PRINTF("LONG PRESS\r\n");
                AlarmEvent_t alarmEvt = EVT_JOY_LONG_PRESS;
                xQueueSend(alarm_queue, &alarmEvt, portMAX_DELAY);
                sendMessage("JOY:LONG_PRESS\n");
            }
        }
    }
}

void vTaskJoystick(void *pv)
{
    PRINTF("[JOY] Task started\r\n");

    for (;;) {
        int x = results[0];
        int y = results[1];
        AlarmEvent_t evt = -1;

        if (x < (JOY_CENTRE - JOY_DEAD)) {
            evt = EVT_JOY_LEFT;
            sendMessage("JOY:LEFT");
        } else if (x > (JOY_CENTRE + JOY_DEAD)) {
            evt = EVT_JOY_RIGHT;
            sendMessage("JOY:RIGHT");
        } else if (y > (JOY_CENTRE + JOY_DEAD)) {
            evt = EVT_JOY_DOWN;
            sendMessage("JOY:DOWN");
        } else if (y < (JOY_CENTRE - JOY_DEAD)) {
            evt = EVT_JOY_UP;
            sendMessage("JOY:UP");
        }

        // Only send if direction changed
        if (evt != -1) {
			PRINTF("[JOY] x=%d, y=%d -> event %d\r\n", x, y, evt);
			xQueueSend(alarm_queue, &evt, pdMS_TO_TICKS(100));
		}

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

static void alarmFSMTask(void *p) {
	AlarmState_t state = STATE_IDLE;
	AlarmEvent_t event;

	while(1) {
		if(xQueueReceive(alarm_queue, (AlarmEvent_t *) &event, portMAX_DELAY) == pdTRUE){

			// EVT_ALARM_FIRE is a global transition — daily alarm can fire from any state
			if (event == EVT_ALARM_FIRE) {
				state = STATE_ALARM_FIRING;
				currentAlarmState = state;
				PRINTF("STATE_ALARM_FIRING");
				continue;
			}

			switch(state)
			{
			case(STATE_IDLE):
				if(event == EVT_JOY_LEFT){
					state = STATE_SET_TIMER;
				} else if (event == EVT_JOY_RIGHT) {
					state = STATE_SET_ALARM;
				}
                break;
			case(STATE_SET_TIMER):
				if(event == EVT_JOY_RIGHT){
					state = STATE_IDLE;
					sendMessage("TIMER_CANCEL\n");
				} else if (event == EVT_JOY_UP){
					// increase timer (ESP32 handles value via JOY:UP)
				} else if (event == EVT_JOY_DOWN) {
					// decrease timer (ESP32 handles value via JOY:DOWN)
				} else if (event == EVT_JOY_PRESS){
					state = STATE_TIMER_RUNNING;
					sendMessage("TIMER_START\n");
				}
                break;
			case(STATE_SET_ALARM):
				if(event == EVT_JOY_RIGHT){
					// cycle to next digit (ESP32 handles via JOY:RIGHT)
				} else if (event == EVT_JOY_PRESS) {
					// confirm one digit (ESP32 handles via JOY:PRESS)
				} else if (event == EVT_JOY_UP) {
					// increase the current digit (ESP32 handles via JOY:UP)
				} else if (event == EVT_JOY_DOWN) {
					// decrease the current digit (ESP32 handles via JOY:DOWN)
				} else if (event == EVT_JOY_LONG_PRESS) {
					state = STATE_IDLE;
					sendMessage("ALARM_SET\n");
				} else if (event == EVT_JOY_LEFT){
					state = STATE_IDLE;
					sendMessage("ALARM_CANCEL\n");
				}
                break;
			case(STATE_TIMER_RUNNING):
				if(event == EVT_JOY_LONG_PRESS){
					state = STATE_SET_TIMER;
					sendMessage("TIMER_CANCEL\n");
				} else if (event == EVT_TIMEOUT) {
					state = STATE_ALARM_FIRING;
				} else if (event == EVT_JOY_RIGHT) {
					state = STATE_IDLE;
					sendMessage("TIMER_CANCEL\n");
				}
                break;
			case(STATE_ALARM_FIRING):
				if (event == EVT_SW2) {
					state = STATE_IDLE;
					sendMessage("ALARM_STOP\n");
				} else if (event == EVT_SNOOZE) {
					state = STATE_IDLE;
				}
                break;
			}
			currentAlarmState = state;
		}
	}
}

static void recvTask(void *p) {
    char plain_msg[64];

    while (1) {
        TMessage msg;
        if (xQueueReceive(recv_queue, &msg, portMAX_DELAY) != pdTRUE) continue;

        int n = onFrameReceived((uint8_t *)msg.message, plain_msg, sizeof(plain_msg));
        if (n < 0) {
            PRINTF("Bad frame dropped\r\n");
            continue;
        }

        PRINTF("Received message: %s\r\n", plain_msg);

        if (strncmp(plain_msg, "TIME:", 5) == 0) {
            TMessage rtcMsg;
            strncpy(rtcMsg.message, plain_msg + 5, MAX_MSG_LEN - 1);
            rtcMsg.message[MAX_MSG_LEN - 1] = '\0';
            xQueueOverwrite(RTC_queue, &rtcMsg);
        } else if (strncmp(plain_msg, "TIMER_DONE", 10) == 0) {
            AlarmEvent_t event = EVT_TIMEOUT;
            xQueueSend(alarm_queue, &event, portMAX_DELAY);
        } else if (strncmp(plain_msg, "ALARM_FIRING", 12) == 0) {
            AlarmEvent_t event = EVT_ALARM_FIRE;
            xQueueSend(alarm_queue, &event, portMAX_DELAY);
        } else if (strncmp(plain_msg, "SNOOZE", 6) == 0) {
            AlarmEvent_t event = EVT_SNOOZE;
            xQueueSend(alarm_queue, &event, portMAX_DELAY);
        }
    }
}

static void sw2Task(void *p) {
    while(1) {
         if (xSemaphoreTake(sw2Sema, portMAX_DELAY) == pdTRUE) {
             PRINTF("SW2 Pressed\r\n");
             rtc_datetime_t now;
            RTC_GetDatetime(RTC, &now);
            char timestamp_string[64];
            snprintf(timestamp_string, sizeof(timestamp_string),
                    "SW2:%04d-%02d-%02d %02d:%02d:%02d\n",
                    now.year, now.month, now.day, now.hour, now.minute, now.second);
            sendMessage(timestamp_string);
             AlarmEvent_t event = EVT_SW2;
            xQueueSend(alarm_queue, &event, portMAX_DELAY);
             vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_DELAY_MS));
             lockSW2 = 0;
             PORTC->PCR[SW2] &= ~PORT_PCR_IRQC_MASK;
             PORTC->PCR[SW2] |= PORT_PCR_IRQC(10);
         }
    }
}

static void sendTask(void *p) {
	while (1) {
		TMessage msg;
		if(xQueueReceive(send_queue, (TMessage *) &msg, portMAX_DELAY) == pdTRUE) {
			/* msg.message is a 64-byte binary ciphertext frame, not a C string.
			 * Pull the plaintext counter out of the header for the log line. */
			uint32_t ctr = ((uint32_t)(uint8_t)msg.message[0] << 24)
			             | ((uint32_t)(uint8_t)msg.message[1] << 16)
			             | ((uint32_t)(uint8_t)msg.message[2] <<  8)
			             | ((uint32_t)(uint8_t)msg.message[3]);
			PRINTF("Sending frame (ctr=%lu)\r\n", (unsigned long)ctr);

			if (xSemaphoreTake(sendSema, portMAX_DELAY) == pdTRUE) {
				/* Binary-safe copy: ciphertext can contain 0x00 anywhere, so
				 * strncpy would truncate at the first null and zero-pad the
				 * rest, corrupting the frame. */
				memcpy(send_buffer, msg.message, MAX_MSG_LEN);
				send_ptr = 0;

        		// enable the transmit interrupt
        		UART2->C2 |= UART_C2_TIE_MASK;

        		// enable the transmitter
        		UART2->C2 |= UART_C2_TE_MASK;
			}
		}
	}
}

static void initRTCTask(void *p) {
	rtc_config_t rtcConfig;
    RTC_GetDefaultConfig(&rtcConfig);
    RTC_Init(RTC, &rtcConfig);

    // Select LPO (1 kHz) as the RTC clock source via SIM_SOPT1
    SIM->SOPT1 = (SIM->SOPT1 & ~SIM_SOPT1_OSC32KSEL_MASK) | SIM_SOPT1_OSC32KSEL(3);


    uint32_t flags = RTC_GetStatusFlags(RTC);
    if (kRTC_TimeInvalidFlag){
    	PRINTF("flag");
    }
    if (flags){
    	PRINTF("flags");
    }
    if (!(flags & kRTC_TimeInvalidFlag)) {
        RTC_StopTimer(RTC);
        // Communicate with ESP32 to update the time
        sendMessage("GET_TIME. Reply in Time: YYYY-MM-DD HH:MM:SS\n");

        TMessage msg;
        if(xQueueReceive(RTC_queue, (TMessage *) &msg, portMAX_DELAY) == pdTRUE) {
            // a valid datetime string in the format "YYYY-MM-DD HH:MM:SS"
            rtc_datetime_t dt;
            dt.year = (msg.message[0] - '0') * 1000 + (msg.message[1] - '0') * 100 + (msg.message[2] - '0') * 10 + (msg.message[3] - '0');
            dt.month = (msg.message[5] - '0') * 10 + (msg.message[6] - '0');
            dt.day = (msg.message[8] - '0') * 10 + (msg.message[9] - '0');
            dt.hour = (msg.message[11] - '0') * 10 + (msg.message[12] - '0');
            dt.minute = (msg.message[14] - '0') * 10 + (msg.message[15] - '0');
            dt.second = (msg.message[17] - '0') * 10 + (msg.message[18] - '0');

            (void)RTC_SetDatetime(RTC, &dt);
            RTC_StartTimer(RTC);
        }
    }
    vTaskSuspend(NULL);
}


int main(void)
{

    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();

#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    BOARD_InitDebugConsole();
#endif

    setTPMClock();
    initSW2Interrupt();
    initButton();
    initADC();
    startADC(ADC_SE0);


    sendSema = xSemaphoreCreateBinary();
    sw2Sema = xSemaphoreCreateBinary();
    xSemaphoreGive(sendSema);
    recv_queue = xQueueCreate(QLEN, sizeof(TMessage));
    send_queue = xQueueCreate(QLEN, sizeof(TMessage));
    alarm_queue = xQueueCreate(QLEN, sizeof(AlarmEvent_t));
    RTC_queue = xQueueCreate(1, sizeof(TMessage));
    g_btn_raw_queue = xQueueCreate(8, sizeof(btn_msg_t));
    g_btn_evt_queue = xQueueCreate(8, sizeof(btn_evt_t));

    initUART2(9600);
    initPWM();
    startPWM();
    PRINTF("HELLO");
    // Priority 5: One-shot init (runs first at boot, then suspends)
    xTaskCreate(initRTCTask, "initRTCTask", configMINIMAL_STACK_SIZE + 300, NULL, 4, NULL);
    // Priority 4: FSM must process events before anything reads currentAlarmState
    xTaskCreate(alarmFSMTask, "alarmFSMTask", configMINIMAL_STACK_SIZE + 300, NULL, 3, NULL);
    // Priority 3: UART I/O and ISR relay (time-critical)
    xTaskCreate(sendTask, "sendTask", configMINIMAL_STACK_SIZE + 300, NULL, 2, NULL);
    xTaskCreate(recvTask, "recvTask", configMINIMAL_STACK_SIZE + 300, NULL, 2, NULL);
    xTaskCreate(vTaskButton, "btn", 256, NULL, 1, NULL);
    // Priority 2: Event producers
    xTaskCreate(sw2Task, "sw2Task", configMINIMAL_STACK_SIZE + 300, NULL, 1, NULL);
    xTaskCreate(vTaskJoyButtonHandler, "btn_hdl", 256, NULL, 1, NULL);
    xTaskCreate(vTaskJoystick, "joy", 256, NULL, 1, NULL);
    // Priority 1: Cosmetic (LED breathing/flashing)
    xTaskCreate(ledIndicatorTask, "ledIndicatorTask", configMINIMAL_STACK_SIZE + 300, NULL, 1, NULL);
    sendMessage("THIS IS A MESSAGE");
    //sendMessage("This is a message\n");
    vTaskStartScheduler();



    while (1) {
    }
}

