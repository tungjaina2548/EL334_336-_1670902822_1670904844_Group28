/*
 * Project: BOMB MISSION - Simon Memory Challenge (Shield + LCD 16x2 I2C + PuTTY)
 * Course:  EL334 Microprocessor and Microcontroller System Laboratory
 * Board:   NXP FRDM-K64F (MK64FN1M0VLL12) + Arduino Multi-Function Shield
 * IDE:     Keil uVision (Register-Level C Programming)
 *
 * Pin Mapping:
 *   [Arduino Multi-Function Shield]
 *   Switch S1 (Select / Scan): PTB3  (A1) - Active-Low, Short Press & 700ms Long Press
 *   Switch S2 (Confirm)      : PTB10 (A2) - Active-Low, Port B Interrupt
 *   Switch S3 (Start/Reset)  : PTB11 (A3) - Active-Low, Port B Interrupt
 *   LED D1..D4               : PTD1, PTD3, PTD2, PTD0 - Active-Low
 *   Piezo Buzzer             : PTA1  (D3)  - Active-Low via PNP Q1 (+5V)
 *   7-Segment (74HC595)      : PTB23 (Latch), PTC3 (Clock), PTC12 (Data)
 *
 *   [External Peripherals]
 *   UART0 RX / TX (OpenSDA)  : PTB16 / PTB17 (9600 bps, 8N1 -> PuTTY)
 *   LCD 16x2 I2C (PCF8574)   : PTD7 (SCL - Top-Right Outer Pin 1), PTD6 (SDA - Outer Pin 2)
 */

#include "MK64F12.h"
#include <stdint.h>

/* ============================================================================
 * 1. PIN DEFINITIONS & BIT MASKS
 * ============================================================================ */
#define MASK(x)             (1UL << (x))

/* Switches on Port B (Active-Low) */
#define SW1_PIN             3U      /* PTB3  - Short: Select LED | Long (0.7s): Scanner */
#define SW2_PIN             10U     /* PTB10 - Confirm Answer */
#define SW3_PIN             11U     /* PTB11 - Start / Restart Game */

/* LEDs on Port D (Active-Low: 0 = ON, 1 = OFF) */
#define LED1_PIN            1U      /* PTD1 (Shield D1) */
#define LED2_PIN            3U      /* PTD3 (Shield D2) */
#define LED3_PIN            2U      /* PTD2 (Shield D3) */
#define LED4_PIN            0U      /* PTD0 (Shield D4) */
#define ALL_LEDS_MASK       (MASK(LED1_PIN) | MASK(LED2_PIN) | MASK(LED3_PIN) | MASK(LED4_PIN))

/* Piezo Buzzer on Port A (Active-Low via 5V PNP Transistor Q1) */
#define BUZZER_PIN          1U      /* PTA1 (Shield D3) */

/* 7-Segment Shift Register (74HC595) Pins */
#define SEG_LATCH_PIN       23U     /* PTB23 (Shield D4) */
#define SEG_CLK_PIN         3U      /* PTC3  (Shield D7) */
#define SEG_DATA_PIN        12U     /* PTC12 (Shield D8) */

/* UART0 Pins on Port B (OpenSDA USB Virtual COM Port) */
#define UART_RX_PIN         16U     /* PTB16 (UART0_RX) */
#define UART_TX_PIN         17U     /* PTB17 (UART0_TX) */
#define UART_BAUD_RATE      9600U

/* LCD 16x2 I2C Pins on Port D (Top-Right Exposed Outer Header Pins) */
#define LCD_SCL_PIN         7U      /* PTD7 (Top-most outer hole on right header) */
#define LCD_SDA_PIN         6U      /* PTD6 (2nd from top outer hole on right header) */

/* PCF8574 LCD Backpack Bit Masks */
#define LCD_RS              0x01U   /* P0: Register Select (0=Cmd, 1=Data) */
#define LCD_EN              0x04U   /* P2: Enable Pulse */
#define LCD_BL              0x08U   /* P3: Backlight ON */

/* Button Event Codes */
#define BTN_NONE            0U
#define BTN_S1_SELECT       1U
#define BTN_S2_CONFIRM      2U
#define BTN_S3_START        3U
#define BTN_S1_LONG         4U      /* Hold S1 >= 700 ms for Emergency Scanner */

/* Maximum sequence length (Level 3 = 7 steps) */
#define MAX_SEQ_LEN         7U

/* ============================================================================
 * 2. GAME STATES & MUSICAL NOTE DEFINITIONS (Hz)
 * ============================================================================ */
typedef enum {
    STATE_IDLE = 0,         /* Waiting for player to press S3 */
    STATE_LEVEL_INIT,       /* Setup level parameters, reset timer & ready signal */
    STATE_SHOW_SEQ,         /* Blink LED sequence for player to memorize */
    STATE_PLAYER_INPUT,     /* Countdown running, player enters sequence */
    STATE_VICTORY,          /* Passed Level 3! Show PASS & alternating Score */
    STATE_GAME_OVER         /* Wrong input or Time == 0 -> Game Over */
} GameState_t;

/* Musical Note Frequencies (Hz) */
#define NOTE_REST           0U
#define NOTE_A3             220U
#define NOTE_G4             392U
#define NOTE_GS4            415U
#define NOTE_A4             440U
#define NOTE_AS4            466U
#define NOTE_B4             494U
#define NOTE_CS5            554U
#define NOTE_D5             587U
#define NOTE_DS5            622U
#define NOTE_E5             659U
#define NOTE_FS5            740U
#define NOTE_G5             784U
#define NOTE_GS5            831U
#define NOTE_A5             880U
#define NOTE_B5             988U
#define NOTE_C6             1047U
#define NOTE_CS6            1109U
#define NOTE_D6             1175U
#define NOTE_DS6            1245U
#define NOTE_E6             1319U
#define NOTE_FS6            1480U
#define NOTE_GS6            1661U

/* E-Minor 7th Note Mapping for LED1..LED4 */
static const uint16_t LED_NOTES[4] = { NOTE_E5, NOTE_G5, NOTE_B5, NOTE_D6 };

/* 7-Segment Active-Low Patterns (Bit order: DP, G, F, E, D, C, B, A) */
static const uint8_t SEG_NUMBERS[10] = {
    0xC0, /* 0 */
    0xF9, /* 1 */
    0xA4, /* 2 */
    0xB0, /* 3 */
    0x99, /* 4 */
    0x92, /* 5 */
    0x82, /* 6 */
    0xF8, /* 7 */
    0x80, /* 8 */
    0x90  /* 9 */
};

/* Custom 7-Segment Characters (Active-Low) */
#define SEG_BLANK           0xFFU
#define SEG_DASH            0xBFU   /* '-' */
#define SEG_CHAR_r          0xAFU   /* 'r' */
#define SEG_CHAR_E          0x86U   /* 'E' */
#define SEG_CHAR_d          0xA1U   /* 'd' */
#define SEG_CHAR_Y          0x91U   /* 'Y' */
#define SEG_CHAR_P          0x8CU   /* 'P' */
#define SEG_CHAR_A          0x88U   /* 'A' */
#define SEG_CHAR_S          0x92U   /* 'S' */
#define SEG_CHAR_C          0xC6U   /* 'C' */
#define SEG_CHAR_F          0x8EU   /* 'F' */
#define SEG_CHAR_I          0xF9U   /* 'I' */
#define SEG_CHAR_L          0xC7U   /* 'L' */
#define SEG_CHAR_v          0xE3U   /* 'v' */
#define SEG_CHAR_b          0x83U   /* 'b' */
#define SEG_CHAR_n          0xABU   /* 'n' */

/* Digit Enable Masks (Left to Right: Digit 0, 1, 2, 3) */
static const uint8_t DIGIT_SELECT[4] = { 0xF1U, 0xF2U, 0xF4U, 0xF8U };

/* LED Pin Lookup Table (Index 0..3 -> LED1..LED4) */
static const uint8_t LED_PINS[4] = { LED1_PIN, LED2_PIN, LED3_PIN, LED4_PIN };

/* ---------- Extended Victory Full Song (~15s Heroic Theme + Grand Finale) ---------- */
static const uint16_t VIC_FULL_NOTES[] = {
    NOTE_B4, NOTE_E5, NOTE_FS5, NOTE_G5, NOTE_FS5, NOTE_E5, NOTE_B5, NOTE_A5,
    NOTE_A5, NOTE_G5, NOTE_FS5, NOTE_G5, NOTE_B5, NOTE_D6, NOTE_B5, NOTE_G5,
    NOTE_E5, NOTE_G5, NOTE_B5, NOTE_E6, NOTE_D6, NOTE_B5, NOTE_D5, NOTE_FS5, NOTE_A5, NOTE_D6, NOTE_B5, NOTE_A5,
    NOTE_G5, NOTE_B5, NOTE_D6, NOTE_B5, NOTE_D6, NOTE_E6, NOTE_D6, NOTE_B5, NOTE_E6, NOTE_REST,

    NOTE_E5, NOTE_GS5, NOTE_B5, NOTE_E6, NOTE_DS6, NOTE_CS6, NOTE_B5, NOTE_GS5,
    NOTE_A5, NOTE_CS6, NOTE_E6, NOTE_CS6, NOTE_B5, NOTE_GS5, NOTE_E5, NOTE_REST,
    NOTE_FS5, NOTE_A5, NOTE_CS6, NOTE_E6, NOTE_DS6, NOTE_B5, NOTE_FS5, NOTE_B5,
    NOTE_GS5, NOTE_B5, NOTE_E6, NOTE_DS6, NOTE_E6, NOTE_FS6, NOTE_GS6, NOTE_REST,

    NOTE_E6, NOTE_DS6, NOTE_CS6, NOTE_B5, NOTE_CS6, NOTE_B5, NOTE_GS5, NOTE_E5,
    NOTE_A5, NOTE_B5, NOTE_CS6, NOTE_E6, NOTE_DS6, NOTE_CS6, NOTE_B5, NOTE_REST,
    NOTE_B5, NOTE_E6, NOTE_DS6, NOTE_CS6, NOTE_B5, NOTE_GS5, NOTE_A5, NOTE_B5,
    NOTE_E5, NOTE_GS5, NOTE_B5, NOTE_E6, NOTE_B5, NOTE_GS5, NOTE_E6
};
static const uint8_t VIC_FULL_BEATS[] = {
    2, 2, 1, 2, 1, 2, 2, 4,
    2, 1, 1, 2, 2, 4, 2, 2,
    1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1, 2,
    1, 1, 2, 2, 2, 2, 1, 1, 5, 1,

    1, 1, 2, 2, 1, 1, 2, 2,
    1, 1, 2, 2, 2, 2, 3, 1,
    1, 1, 2, 2, 1, 1, 2, 2,
    1, 1, 2, 1, 1, 2, 4, 1,

    2, 1, 1, 2, 1, 1, 2, 2,
    1, 1, 2, 2, 1, 1, 4, 1,
    1, 2, 1, 1, 2, 1, 1, 2,
    1, 1, 1, 2, 1, 1, 8
};

/* ---------- Victory Short Song Arrays (Stage Clear Fanfare, 1 beat = 95 ms) ---------- */
static const uint16_t VIC_SHORT_NOTES[] = {
    NOTE_E5, NOTE_GS5, NOTE_B5, NOTE_E6, NOTE_B5, NOTE_CS6, NOTE_DS6, NOTE_E6,
    NOTE_B5, NOTE_GS5, NOTE_E6
};
static const uint8_t VIC_SHORT_BEATS[] = {
    1, 1, 1, 2, 1, 1, 1, 3,
    1, 1, 6
};

/* ---------- Game Over Spy-Tango Arrays (24 notes, 1 beat = 115 ms) ---------- */
static const uint16_t FAIL_NOTES[] = {
    NOTE_B4, NOTE_E5, NOTE_G5, NOTE_FS5, NOTE_E5, NOTE_REST,
    NOTE_E5, NOTE_FS5, NOTE_G5, NOTE_A5, NOTE_B5, NOTE_REST,
    NOTE_C6, NOTE_B5, NOTE_A5, NOTE_G5, NOTE_FS5, NOTE_A5, NOTE_DS5,
    NOTE_B4, NOTE_DS5, NOTE_FS5, NOTE_B5, NOTE_E5
};
static const uint8_t FAIL_BEATS[] = {
    1, 3, 1, 1, 4, 1,
    1, 1, 2, 2, 4, 1,
    3, 1, 2, 2, 2, 2, 4,
    1, 1, 1, 1, 8
};

/* Volatile variables shared with Interrupt Service Routines (ISRs) */
static volatile uint32_t ms_ticks = 0U;          /* 1 ms system tick counter */
static volatile uint8_t  seg_buffer[4] = { SEG_CHAR_r, SEG_CHAR_E, SEG_CHAR_d, SEG_CHAR_Y };
static volatile uint8_t  pending_btn = BTN_NONE; /* Raw button detected in PORTB IRQ */
static volatile uint8_t  debounce_cnt = 0U;      /* 25 ms debounce countdown */
static volatile uint16_t s1_hold_ms = 0U;        /* Hold timer for S1 long-press */
static volatile uint8_t  s1_long_fired = 0U;     /* Prevents duplicate long-press fire */
static volatile uint8_t  btn_event = BTN_NONE;   /* Confirmed button event for Main */
static volatile uint8_t  timer_active = 0U;      /* 1 = countdown enabled, 0 = paused */
static volatile uint8_t  reset_sec_counter = 0U; /* Flag to reset 1000 ms sub-counter */
static volatile uint8_t  time_left = 30U;        /* Remaining mission seconds */
static uint32_t          pit_bus_clock = 0U;     /* Bus clock frequency for PIT1 audio */

/* Game State & Telemetry Variables */
static GameState_t current_state = STATE_IDLE;
static uint8_t  current_level = 1U;
static uint8_t  seq_length = 3U;
static uint8_t  led_sequence[MAX_SEQ_LEN];
static uint8_t  player_step = 0U;
static uint8_t  selected_led = 0U;
static uint8_t  scanner_available = 1U;          /* 1 = Ready, 0 = Already used */
static uint8_t  fail_reason = 0U;                /* 1 = Wrong Wire, 2 = Timeout */
static uint16_t current_score = 0U;              /* Accumulated mission score */
static uint32_t prng_seed = 20261123U;

/* LCD I2C State Variables */
static uint8_t  lcd_i2c_addr = 0x27U;            /* Auto-detected: 0x27 or 0x3F */
static uint8_t  lcd_connected = 0U;              /* 1 = LCD detected on I2C bus */

/* ============================================================================
 * 3. FUNCTION PROTOTYPES
 * ============================================================================ */
void GPIO_Init(void);
void LED_Init(void);
void LED_AllOff(void);
void LED_AllOn(void);
void LED_SelectSingle(uint8_t led_index);
void Switch_Init(void);
uint8_t Switch_Read(void);
void Buzzer_Init(void);
void Buzzer_StopTone(void);
void Buzzer_PlayTone(uint32_t freq_hz, uint32_t duration_ms);
void Play_Song(const uint16_t *notes, const uint8_t *beats, uint8_t count, uint32_t unit_ms, uint8_t led_fx);
void Victory_Melody(uint8_t is_final);
void GameOver_Melody(void);
void SevenSegment_Init(void);
void SevenSegment_ShiftOut(uint8_t data);
void SevenSegment_RefreshDigit(void);
void Display_Time(uint8_t level, uint8_t seconds);
void Display_Text(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);
void Display_Score(uint16_t score);
void UART0_Init(void);
void UART0_PutChar(char ch);
void UART0_Print(const char *str);
void UART0_PrintNum(uint32_t num);
void I2C_BusInit(void);
uint8_t I2C_ProbeAddress(uint8_t addr);
void I2C_WriteByte(uint8_t addr, uint8_t data);
void LCD_Init(void);
void LCD_SendNibble(uint8_t nibble, uint8_t rs);
void LCD_SendByte(uint8_t byte, uint8_t rs);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t col, uint8_t row);
void LCD_Print(const char *str);
void LCD_PrintNum(uint32_t num);
void LCD_ShowStatus(const char *line1, const char *line2);
void LCD_UpdatePlayScreen(uint8_t level, uint8_t seconds, uint8_t step, uint8_t total, uint16_t score);
void Timer_Init(void);
void Delay_ms(uint32_t ms);
uint8_t Random_LED(void);
void Generate_Sequence(uint8_t length);
void Level_Ready_Signal(uint8_t level, uint8_t start_seconds);
void Play_Sequence(uint8_t length, uint8_t level);
void Trigger_EmergencyScanner(void);
void Game_Control(void);

/* ============================================================================
 * 4. INITIALIZATION FUNCTIONS
 * ============================================================================ */

/* Enable Clock Gates for GPIO Ports A, B, C, D */
void GPIO_Init(void) {
    SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK |
                  SIM_SCGC5_PORTB_MASK |
                  SIM_SCGC5_PORTC_MASK |
                  SIM_SCGC5_PORTD_MASK;
}

/* Configure LEDs D1-D4 (PTD1, PTD3, PTD2, PTD0) as GPIO Outputs */
void LED_Init(void) {
    PORTD->PCR[LED1_PIN] = PORT_PCR_MUX(1);
    PORTD->PCR[LED2_PIN] = PORT_PCR_MUX(1);
    PORTD->PCR[LED3_PIN] = PORT_PCR_MUX(1);
    PORTD->PCR[LED4_PIN] = PORT_PCR_MUX(1);

    PTD->PDDR |= ALL_LEDS_MASK;
    LED_AllOff();
}

/* Configure Switches S1-S3 (PTB3, PTB10, PTB11) with Pull-Up & Falling-Edge IRQ */
void Switch_Init(void) {
    uint32_t pcr_cfg = PORT_PCR_MUX(1) |
                       PORT_PCR_PE_MASK |
                       PORT_PCR_PS_MASK |
                       PORT_PCR_IRQC(0xA);

    PORTB->PCR[SW1_PIN] = pcr_cfg;
    PORTB->PCR[SW2_PIN] = pcr_cfg;
    PORTB->PCR[SW3_PIN] = pcr_cfg;

    PTB->PDDR &= ~(MASK(SW1_PIN) | MASK(SW2_PIN) | MASK(SW3_PIN));
    PORTB->ISFR = MASK(SW1_PIN) | MASK(SW2_PIN) | MASK(SW3_PIN);

    NVIC_ClearPendingIRQ(PORTB_IRQn);
    NVIC_EnableIRQ(PORTB_IRQn);
}

/* Configure Piezo Buzzer (PTA1) for clean High-Z / Low switching */
void Buzzer_Init(void) {
    PORTA->PCR[BUZZER_PIN] = PORT_PCR_MUX(1);
    PTA->PCOR = MASK(BUZZER_PIN);
    PTA->PDDR &= ~MASK(BUZZER_PIN);
}

/* Configure 74HC595 Control Pins (PTB23, PTC3, PTC12) as GPIO Outputs */
void SevenSegment_Init(void) {
    PORTB->PCR[SEG_LATCH_PIN] = PORT_PCR_MUX(1);
    PORTC->PCR[SEG_CLK_PIN]   = PORT_PCR_MUX(1);
    PORTC->PCR[SEG_DATA_PIN]  = PORT_PCR_MUX(1);

    PTB->PDDR |= MASK(SEG_LATCH_PIN);
    PTC->PDDR |= MASK(SEG_CLK_PIN) | MASK(SEG_DATA_PIN);

    PTB->PCOR = MASK(SEG_LATCH_PIN);
    PTC->PCOR = MASK(SEG_CLK_PIN) | MASK(SEG_DATA_PIN);
}

/* Configure UART0 (PTB16=RX, PTB17=TX) at 9600 bps, 8-N-1 for PuTTY */
void UART0_Init(void) {
    uint32_t sbr, brfa;

    SystemCoreClockUpdate();

    SIM->SCGC4 |= SIM_SCGC4_UART0_MASK;
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;

    PORTB->PCR[UART_RX_PIN] = PORT_PCR_MUX(3);
    PORTB->PCR[UART_TX_PIN] = PORT_PCR_MUX(3);

    UART0->C2 &= ~(UART_C2_TE_MASK | UART_C2_RE_MASK);
    UART0->C1  = 0x00U;

    sbr  = SystemCoreClock / (16U * UART_BAUD_RATE);
    brfa = ((SystemCoreClock * 2U) / UART_BAUD_RATE) - (sbr * 32U);

    UART0->BDH = (uint8_t)((sbr >> 8) & UART_BDH_SBR_MASK);
    UART0->BDL = (uint8_t)(sbr & UART_BDL_SBR_MASK);
    UART0->C4  = (uint8_t)(brfa & UART_C4_BRFA_MASK);

    UART0->C2 |= (UART_C2_TE_MASK | UART_C2_RE_MASK);
}

/* Configure PTD7 (SCL) & PTD6 (SDA) as Open-Drain I2C Bus with Internal Pull-Ups */
void I2C_BusInit(void) {
    SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK;

    PORTD->PCR[LCD_SCL_PIN] = PORT_PCR_MUX(1) | PORT_PCR_ODE_MASK | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK;
    PORTD->PCR[LCD_SDA_PIN] = PORT_PCR_MUX(1) | PORT_PCR_ODE_MASK | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK;

    /* Set output state HIGH (High-Z in Open-Drain mode -> pulled high) */
    PTD->PSOR = MASK(LCD_SCL_PIN) | MASK(LCD_SDA_PIN);
    PTD->PDDR |= MASK(LCD_SCL_PIN) | MASK(LCD_SDA_PIN);
}

/* Configure PIT0 (1 ms System Tick) and PIT1 (Hardware Audio Tone Generator) */
void Timer_Init(void) {
    uint32_t outdiv1, outdiv2, mcgoutclk;

    SystemCoreClockUpdate();

    outdiv1 = ((SIM->CLKDIV1 & SIM_CLKDIV1_OUTDIV1_MASK) >> SIM_CLKDIV1_OUTDIV1_SHIFT) + 1U;
    outdiv2 = ((SIM->CLKDIV1 & SIM_CLKDIV1_OUTDIV2_MASK) >> SIM_CLKDIV1_OUTDIV2_SHIFT) + 1U;
    mcgoutclk = SystemCoreClock * outdiv1;
    pit_bus_clock = mcgoutclk / outdiv2;

    SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;
    PIT->MCR = 0x00U;

    /* PIT Channel 0: 1 ms interrupt (1000 Hz) */
    PIT->CHANNEL[0].LDVAL = (pit_bus_clock / 1000U) - 1U;
    PIT->CHANNEL[0].TFLG  = PIT_TFLG_TIF_MASK;
    PIT->CHANNEL[0].TCTRL = PIT_TCTRL_TIE_MASK | PIT_TCTRL_TEN_MASK;

    /* PIT Channel 1: Disabled initially (enabled during Buzzer_PlayTone) */
    PIT->CHANNEL[1].TCTRL = 0U;
    PIT->CHANNEL[1].TFLG  = PIT_TFLG_TIF_MASK;

    NVIC_ClearPendingIRQ(PIT0_IRQn);
    NVIC_EnableIRQ(PIT0_IRQn);

    NVIC_ClearPendingIRQ(PIT1_IRQn);
    NVIC_EnableIRQ(PIT1_IRQn);
}

/* ============================================================================
 * 5. HARDWARE INTERRUPT SERVICE ROUTINES (ISRs)
 * ============================================================================ */

/* Port B ISR: Detects S1, S2, or S3 press and triggers 25 ms debounce */
void PORTB_IRQHandler(void) {
    uint32_t flags = PORTB->ISFR;

    if ((debounce_cnt == 0U) && (s1_hold_ms == 0U)) {
        if (flags & MASK(SW1_PIN)) {
            pending_btn = BTN_S1_SELECT;
            debounce_cnt = 25U;
        } else if (flags & MASK(SW2_PIN)) {
            pending_btn = BTN_S2_CONFIRM;
            debounce_cnt = 25U;
        } else if (flags & MASK(SW3_PIN)) {
            pending_btn = BTN_S3_START;
            debounce_cnt = 25U;
        }
    }

    PORTB->ISFR = flags;
}

/* PIT Channel 0 ISR: System Tick, Short/Long Press Debounce, 7-Seg Mux & Timer */
void PIT0_IRQHandler(void) {
    static uint8_t  mux_counter = 0U;
    static uint16_t sec_counter = 0U;
    uint32_t pdir = PTB->PDIR;

    PIT->CHANNEL[0].TFLG = PIT_TFLG_TIF_MASK;

    /* 1. Increment 1 ms system tick */
    ms_ticks++;

    /* 2. Button Debounce Handler (25 ms confirmation) */
    if (debounce_cnt > 0U) {
        debounce_cnt--;
        if (debounce_cnt == 0U) {
            if ((pending_btn == BTN_S1_SELECT) && ((pdir & MASK(SW1_PIN)) == 0U)) {
                s1_hold_ms = 25U;
                s1_long_fired = 0U;
            } else if ((pending_btn == BTN_S2_CONFIRM) && ((pdir & MASK(SW2_PIN)) == 0U)) {
                btn_event = BTN_S2_CONFIRM;
            } else if ((pending_btn == BTN_S3_START) && ((pdir & MASK(SW3_PIN)) == 0U)) {
                btn_event = BTN_S3_START;
            }
            pending_btn = BTN_NONE;
        }
    }

    /* 3. S1 Short-Press vs Long-Press (700 ms hold) Discrimination */
    if (s1_hold_ms > 0U) {
        if ((pdir & MASK(SW1_PIN)) == 0U) {
            s1_hold_ms++;
            if ((s1_hold_ms >= 700U) && (s1_long_fired == 0U)) {
                s1_long_fired = 1U;
                btn_event = BTN_S1_LONG;
            }
        } else {
            if (s1_long_fired == 0U) {
                btn_event = BTN_S1_SELECT;
            }
            s1_hold_ms = 0U;
            s1_long_fired = 0U;
        }
    }

    /* 4. Multiplex 7-Segment Display every 2 ms (125 Hz refresh rate) */
    mux_counter++;
    if (mux_counter >= 2U) {
        mux_counter = 0U;
        SevenSegment_RefreshDigit();
    }

    /* 5. Mission Countdown Timer (resets cleanly on every new level) */
    if (reset_sec_counter != 0U) {
        sec_counter = 0U;
        reset_sec_counter = 0U;
    }

    if (timer_active != 0U) {
        sec_counter++;
        if (sec_counter >= 1000U) {
            sec_counter = 0U;
            if (time_left > 0U) {
                time_left--;
            }
        }
    } else {
        sec_counter = 0U;
    }
}

/* PIT Channel 1 ISR: Toggles PTA1 between Output-Low & High-Z for pure audio pitch */
void PIT1_IRQHandler(void) {
    PIT->CHANNEL[1].TFLG = PIT_TFLG_TIF_MASK;
    PTA->PDDR ^= MASK(BUZZER_PIN);
}

/* ============================================================================
 * 6. UART0 SERIAL TELEMETRY FUNCTIONS (PUTTY DISPLAY)
 * ============================================================================ */

void UART0_PutChar(char ch) {
    while ((UART0->S1 & UART_S1_TDRE_MASK) == 0U) {
        __NOP();
    }
    UART0->D = (uint8_t)ch;
}

void UART0_Print(const char *str) {
    while (*str != '\0') {
        UART0_PutChar(*str);
        str++;
    }
}

void UART0_PrintNum(uint32_t num) {
    char buf[10];
    uint8_t idx = 0U;

    if (num == 0U) {
        UART0_PutChar('0');
        return;
    }

    while (num > 0U) {
        buf[idx++] = (char)('0' + (num % 10U));
        num /= 10U;
    }

    while (idx > 0U) {
        idx--;
        UART0_PutChar(buf[idx]);
    }
}

/* ============================================================================
 * 7. I2C & LCD 16x2 (PCF8574) DRIVER FUNCTIONS (PTD7=SCL, PTD6=SDA)
 * ============================================================================ */

static void I2C_HalfBitDelay(void) {
    uint32_t i;
    for (i = 0U; i < 350U; i++) {
        __NOP();
    }
}

static void I2C_Start(void) {
    PTD->PSOR = MASK(LCD_SDA_PIN);
    PTD->PSOR = MASK(LCD_SCL_PIN);
    I2C_HalfBitDelay();
    PTD->PCOR = MASK(LCD_SDA_PIN);
    I2C_HalfBitDelay();
    PTD->PCOR = MASK(LCD_SCL_PIN);
    I2C_HalfBitDelay();
}

static void I2C_Stop(void) {
    PTD->PCOR = MASK(LCD_SDA_PIN);
    I2C_HalfBitDelay();
    PTD->PSOR = MASK(LCD_SCL_PIN);
    I2C_HalfBitDelay();
    PTD->PSOR = MASK(LCD_SDA_PIN);
    I2C_HalfBitDelay();
}

static uint8_t I2C_SendRawByte(uint8_t byte) {
    uint8_t bit, ack;

    for (bit = 0U; bit < 8U; bit++) {
        if ((byte & 0x80U) != 0U) {
            PTD->PSOR = MASK(LCD_SDA_PIN);
        } else {
            PTD->PCOR = MASK(LCD_SDA_PIN);
        }
        I2C_HalfBitDelay();
        PTD->PSOR = MASK(LCD_SCL_PIN);
        I2C_HalfBitDelay();
        PTD->PCOR = MASK(LCD_SCL_PIN);
        byte <<= 1;
    }

    /* Release SDA and read 9th-clock ACK bit from PCF8574 */
    PTD->PSOR = MASK(LCD_SDA_PIN);
    I2C_HalfBitDelay();
    PTD->PSOR = MASK(LCD_SCL_PIN);
    I2C_HalfBitDelay();
    ack = ((PTD->PDIR & MASK(LCD_SDA_PIN)) == 0U) ? 1U : 0U;
    PTD->PCOR = MASK(LCD_SCL_PIN);
    I2C_HalfBitDelay();

    return ack;
}

uint8_t I2C_ProbeAddress(uint8_t addr) {
    uint8_t ack;
    I2C_Start();
    ack = I2C_SendRawByte((uint8_t)(addr << 1));
    I2C_Stop();
    return ack;
}

void I2C_WriteByte(uint8_t addr, uint8_t data) {
    if (lcd_connected == 0U) {
        return;
    }
    I2C_Start();
    if (I2C_SendRawByte((uint8_t)(addr << 1)) != 0U) {
        (void)I2C_SendRawByte(data);
    }
    I2C_Stop();
}

void LCD_SendNibble(uint8_t nibble, uint8_t rs) {
    uint8_t base = (uint8_t)((nibble & 0xF0U) | LCD_BL | (rs ? LCD_RS : 0x00U));
    I2C_WriteByte(lcd_i2c_addr, (uint8_t)(base | LCD_EN));
    I2C_HalfBitDelay();
    I2C_WriteByte(lcd_i2c_addr, base);
    I2C_HalfBitDelay();
}

void LCD_SendByte(uint8_t byte, uint8_t rs) {
    LCD_SendNibble((uint8_t)(byte & 0xF0U), rs);
    LCD_SendNibble((uint8_t)((byte << 4) & 0xF0U), rs);
}

/* Auto-scan all PCF8574 addresses (0x20..0x3F) and initialize HD44780 LCD */
void LCD_Init(void) {
    uint8_t addr;

    I2C_BusInit();
    Delay_ms(60U);

    lcd_connected = 0U;
    if (I2C_ProbeAddress(0x27U) != 0U) {
        lcd_i2c_addr  = 0x27U;
        lcd_connected = 1U;
    } else if (I2C_ProbeAddress(0x3FU) != 0U) {
        lcd_i2c_addr  = 0x3FU;
        lcd_connected = 1U;
    } else {
        for (addr = 0x20U; addr <= 0x3FU; addr++) {
            if (I2C_ProbeAddress(addr) != 0U) {
                lcd_i2c_addr  = addr;
                lcd_connected = 1U;
                break;
            }
        }
    }

    if (lcd_connected == 0U) {
        UART0_Print("[LCD] Not detected on PTD7(SCL)/PTD6(SDA) - check wiring!\r\n");
        return;
    }

    UART0_Print("[LCD] Connected on PTD7/PTD6 (Address: 0x");
    UART0_PutChar((char)(((lcd_i2c_addr >> 4) < 10U) ? ('0' + (lcd_i2c_addr >> 4)) : ('A' + (lcd_i2c_addr >> 4) - 10U)));
    UART0_PutChar((char)(((lcd_i2c_addr & 0x0FU) < 10U) ? ('0' + (lcd_i2c_addr & 0x0FU)) : ('A' + (lcd_i2c_addr & 0x0FU) - 10U)));
    UART0_Print(")\r\n");

    /* Standard HD44780 4-bit initialization sequence */
    LCD_SendNibble(0x30U, 0U);
    Delay_ms(5U);
    LCD_SendNibble(0x30U, 0U);
    Delay_ms(2U);
    LCD_SendNibble(0x30U, 0U);
    Delay_ms(2U);
    LCD_SendNibble(0x20U, 0U); /* Switch to 4-bit interface */
    Delay_ms(2U);

    LCD_SendByte(0x28U, 0U);   /* 4-bit, 2 lines, 5x8 font */
    LCD_SendByte(0x0CU, 0U);   /* Display ON, Cursor OFF, Blink OFF */
    LCD_SendByte(0x06U, 0U);   /* Entry mode: Auto-increment cursor */
    LCD_Clear();
}

void LCD_Clear(void) {
    if (lcd_connected == 0U) { return; }
    LCD_SendByte(0x01U, 0U);
    Delay_ms(3U);
}

void LCD_SetCursor(uint8_t col, uint8_t row) {
    uint8_t addr = (row == 0U) ? (0x80U + col) : (0xC0U + col);
    LCD_SendByte(addr, 0U);
}

void LCD_Print(const char *str) {
    if (lcd_connected == 0U) { return; }
    while (*str != '\0') {
        LCD_SendByte((uint8_t)(*str), 1U);
        str++;
    }
}

void LCD_PrintNum(uint32_t num) {
    char buf[10];
    uint8_t idx = 0U;

    if (lcd_connected == 0U) { return; }

    if (num == 0U) {
        LCD_SendByte((uint8_t)'0', 1U);
        return;
    }

    while (num > 0U) {
        buf[idx++] = (char)('0' + (num % 10U));
        num /= 10U;
    }

    while (idx > 0U) {
        idx--;
        LCD_SendByte((uint8_t)buf[idx], 1U);
    }
}

void LCD_ShowStatus(const char *line1, const char *line2) {
    if (lcd_connected == 0U) { return; }
    LCD_Clear();
    LCD_SetCursor(0U, 0U);
    LCD_Print(line1);
    LCD_SetCursor(0U, 1U);
    LCD_Print(line2);
}

void LCD_UpdatePlayScreen(uint8_t level, uint8_t seconds, uint8_t step, uint8_t total, uint16_t score) {
    if (lcd_connected == 0U) { return; }

    LCD_SetCursor(0U, 0U);
    LCD_Print("LV:");
    LCD_PrintNum(level);
    LCD_Print("   TIME: ");
    if (seconds < 10U) {
        LCD_Print(" ");
    }
    LCD_PrintNum(seconds);
    LCD_Print("s ");

    LCD_SetCursor(0U, 1U);
    LCD_Print("STEP:");
    LCD_PrintNum(step);
    LCD_Print("/");
    LCD_PrintNum(total);
    LCD_Print("  SC:");
    LCD_PrintNum(score);
    LCD_Print("   ");
}

/* ============================================================================
 * 8. DEVICE CONTROL FUNCTIONS (LED, SWITCH, BUZZER, 7-SEGMENT)
 * ============================================================================ */

void LED_AllOff(void) {
    PTD->PSOR = ALL_LEDS_MASK;
}

void LED_AllOn(void) {
    PTD->PCOR = ALL_LEDS_MASK;
}

void LED_SelectSingle(uint8_t led_index) {
    LED_AllOff();
    if (led_index < 4U) {
        PTD->PCOR = MASK(LED_PINS[led_index]);
    }
}

uint8_t Switch_Read(void) {
    uint8_t event = btn_event;
    if (event != BTN_NONE) {
        btn_event = BTN_NONE;
    }
    return event;
}

void Delay_ms(uint32_t ms) {
    uint32_t start = ms_ticks;
    while ((ms_ticks - start) < ms) {
        __NOP();
    }
}

void Buzzer_StopTone(void) {
    PIT->CHANNEL[1].TCTRL = 0U;
    PIT->CHANNEL[1].TFLG  = PIT_TFLG_TIF_MASK;
    PTA->PDDR &= ~MASK(BUZZER_PIN);
}

void Buzzer_PlayTone(uint32_t freq_hz, uint32_t duration_ms) {
    uint32_t ldval;

    if ((freq_hz == NOTE_REST) || (duration_ms == 0U)) {
        Buzzer_StopTone();
        if (duration_ms > 0U) {
            Delay_ms(duration_ms);
        }
        return;
    }

    ldval = (pit_bus_clock / (2U * freq_hz)) - 1U;

    PIT->CHANNEL[1].TCTRL = 0U;
    PIT->CHANNEL[1].LDVAL = ldval;
    PIT->CHANNEL[1].TFLG  = PIT_TFLG_TIF_MASK;
    PIT->CHANNEL[1].TCTRL = PIT_TCTRL_TIE_MASK | PIT_TCTRL_TEN_MASK;

    Delay_ms(duration_ms);
    Buzzer_StopTone();
}

void Play_Song(const uint16_t *notes, const uint8_t *beats, uint8_t count, uint32_t unit_ms, uint8_t led_fx) {
    uint8_t  i, k, lit;
    uint32_t dur, gate;

    for (i = 0U; i < count; i++) {
        dur = (uint32_t)beats[i] * unit_ms;

        if (notes[i] == NOTE_REST) {
            Delay_ms(dur);
            continue;
        }

        if (led_fx == 1U) {
            LED_SelectSingle(i & 0x03U);
        } else if (led_fx == 2U) {
            lit = (uint8_t)(4U - ((uint32_t)i * 4U) / count);
            PTD->PSOR = ALL_LEDS_MASK;
            for (k = 0U; k < lit; k++) {
                PTD->PCOR = MASK(LED_PINS[k]);
            }
        }

        gate = (dur * 92U) / 100U;
        Buzzer_PlayTone(notes[i], gate);
        Delay_ms(dur - gate);
    }
    Buzzer_StopTone();
}

void Victory_Melody(uint8_t is_final) {
    uint8_t count;

    if (is_final == 0U) {
        count = (uint8_t)(sizeof(VIC_SHORT_NOTES) / sizeof(VIC_SHORT_NOTES[0]));
        Play_Song(VIC_SHORT_NOTES, VIC_SHORT_BEATS, count, 95U, 1U);
    } else {
        count = (uint8_t)(sizeof(VIC_FULL_NOTES) / sizeof(VIC_FULL_NOTES[0]));
        Play_Song(VIC_FULL_NOTES, VIC_FULL_BEATS, count, 125U, 1U);
        LED_AllOn();
    }
}

void GameOver_Melody(void) {
    Display_Text(SEG_CHAR_b, SEG_NUMBERS[0], SEG_CHAR_n, SEG_CHAR_b);

    PTD->PSOR = ALL_LEDS_MASK;
    PTD->PCOR = MASK(LED1_PIN) | MASK(LED4_PIN);
    Buzzer_PlayTone(NOTE_E5, 110U);
    LED_AllOff();
    Delay_ms(55U);

    PTD->PCOR = MASK(LED2_PIN) | MASK(LED3_PIN);
    Buzzer_PlayTone(NOTE_G5, 110U);
    LED_AllOff();
    Delay_ms(55U);

    PTD->PCOR = MASK(LED1_PIN) | MASK(LED4_PIN);
    Buzzer_PlayTone(NOTE_B5, 110U);
    LED_AllOff();
    Delay_ms(55U);

    LED_AllOn();
    Buzzer_PlayTone(NOTE_E6, 360U);
    LED_AllOff();
    Delay_ms(140U);

    LED_SelectSingle(0U);
    Buzzer_PlayTone(NOTE_B4, 210U);
    Delay_ms(30U);

    LED_SelectSingle(1U);
    Buzzer_PlayTone(NOTE_AS4, 210U);
    Delay_ms(30U);

    LED_SelectSingle(2U);
    Buzzer_PlayTone(NOTE_A4, 210U);
    Delay_ms(30U);

    LED_SelectSingle(3U);
    Buzzer_PlayTone(NOTE_GS4, 580U);
    LED_AllOff();
    Delay_ms(180U);

    Display_Text(SEG_CHAR_F, SEG_CHAR_A, SEG_CHAR_I, SEG_CHAR_L);
    Play_Song(FAIL_NOTES, FAIL_BEATS, 24U, 115U, 2U);
    LED_AllOff();
}

void SevenSegment_ShiftOut(uint8_t data) {
    int8_t bit;
    for (bit = 7; bit >= 0; bit--) {
        if ((data & (1U << bit)) != 0U) {
            PTC->PSOR = MASK(SEG_DATA_PIN);
        } else {
            PTC->PCOR = MASK(SEG_DATA_PIN);
        }
        PTC->PSOR = MASK(SEG_CLK_PIN);
        __NOP();
        PTC->PCOR = MASK(SEG_CLK_PIN);
    }
}

void SevenSegment_RefreshDigit(void) {
    static uint8_t current_digit = 0U;

    PTB->PCOR = MASK(SEG_LATCH_PIN);
    SevenSegment_ShiftOut(seg_buffer[current_digit]);
    SevenSegment_ShiftOut(DIGIT_SELECT[current_digit]);
    PTB->PSOR = MASK(SEG_LATCH_PIN);

    current_digit = (current_digit + 1U) & 0x03U;
}

void Display_Time(uint8_t level, uint8_t seconds) {
    if (level > 9U) { level = 9U; }
    if (seconds > 99U) { seconds = 99U; }

    seg_buffer[0] = SEG_NUMBERS[level];
    seg_buffer[1] = SEG_DASH;
    seg_buffer[2] = SEG_NUMBERS[seconds / 10U];
    seg_buffer[3] = SEG_NUMBERS[seconds % 10U];
}

void Display_Text(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3) {
    seg_buffer[0] = d0;
    seg_buffer[1] = d1;
    seg_buffer[2] = d2;
    seg_buffer[3] = d3;
}

void Display_Score(uint16_t score) {
    if (score > 9999U) {
        score = 9999U;
    }

    seg_buffer[0] = (score >= 1000U) ? SEG_NUMBERS[(score / 1000U) % 10U] : SEG_BLANK;
    seg_buffer[1] = (score >= 100U)  ? SEG_NUMBERS[(score / 100U)  % 10U] : SEG_BLANK;
    seg_buffer[2] = (score >= 10U)   ? SEG_NUMBERS[(score / 10U)   % 10U] : SEG_BLANK;
    seg_buffer[3] = SEG_NUMBERS[score % 10U];
}

/* ============================================================================
 * 9. GAME LOGIC FUNCTIONS
 * ============================================================================ */

uint8_t Random_LED(void) {
    prng_seed = prng_seed * 1103515245U + 12345U;
    return (uint8_t)((prng_seed >> 16) % 4U);
}

void Generate_Sequence(uint8_t length) {
    uint8_t i;
    for (i = 0U; i < length; i++) {
        led_sequence[i] = Random_LED();
    }
}

void Level_Ready_Signal(uint8_t level, uint8_t start_seconds) {
    const uint16_t cue_notes[3] = { NOTE_B5, NOTE_G5, NOTE_E5 };
    uint8_t count;

    Display_Text(SEG_CHAR_L, SEG_CHAR_v, SEG_DASH, SEG_NUMBERS[level]);
    LED_AllOn();
    Buzzer_PlayTone(NOTE_G5, 80U);
    Buzzer_PlayTone(NOTE_B5, 110U);
    LED_AllOff();
    Delay_ms(700U);

    Display_Time(level, start_seconds);
    Delay_ms(800U);

    for (count = 3U; count >= 1U; count--) {
        Display_Text(SEG_NUMBERS[level], SEG_DASH, SEG_DASH, SEG_NUMBERS[count]);

        if (count == 3U) {
            PTD->PCOR = MASK(LED1_PIN) | MASK(LED4_PIN);
        } else if (count == 2U) {
            PTD->PCOR = MASK(LED2_PIN) | MASK(LED3_PIN);
        } else {
            LED_AllOn();
        }

        Buzzer_PlayTone(cue_notes[count - 1U], 105U);
        LED_AllOff();
        Delay_ms(420U);
    }
}

void Play_Sequence(uint8_t length, uint8_t level) {
    uint32_t on_time  = (level == 3U) ? 380U : 460U;
    uint32_t off_time = (level == 3U) ? 240U : 280U;
    uint8_t i;
    uint8_t idx;

    LED_AllOff();
    Delay_ms(550U);

    for (i = 0U; i < length; i++) {
        idx = led_sequence[i];
        LED_SelectSingle(idx);
        Buzzer_PlayTone(LED_NOTES[idx], on_time);
        LED_AllOff();
        Delay_ms(off_time);
    }
}

void Trigger_EmergencyScanner(void) {
    uint8_t target_led;
    uint8_t i;

    if ((scanner_available == 0U) || (time_left <= 6U)) {
        Buzzer_PlayTone(NOTE_A3, 70U);
        Delay_ms(40U);
        Buzzer_PlayTone(NOTE_A3, 70U);
        UART0_Print("[SCANNER] Unavailable (Already used or Time <= 6s)\r\n");
        return;
    }

    scanner_available = 0U;
    time_left = (uint8_t)(time_left - 5U);
    target_led = led_sequence[player_step];

    UART0_Print("[SCANNER] Activated (-5s penalty)! Answer is Wire D");
    UART0_PrintNum((uint32_t)(target_led + 1U));
    UART0_Print("\r\n");

    if (lcd_connected != 0U) {
        LCD_Clear();
        LCD_SetCursor(0U, 0U);
        LCD_Print("SCANNER ACTIVE! ");
        LCD_SetCursor(0U, 1U);
        LCD_Print("Answer: Wire D");
        LCD_PrintNum((uint32_t)(target_led + 1U));
    }

    Display_Text(SEG_CHAR_S, SEG_CHAR_C, SEG_CHAR_A, SEG_CHAR_n);
    for (i = 0U; i < 4U; i++) {
        LED_SelectSingle(i);
        Buzzer_PlayTone(LED_NOTES[i], 45U);
    }
    LED_AllOff();
    Delay_ms(120U);

    Display_Text(SEG_CHAR_L, SEG_DASH, SEG_BLANK, SEG_NUMBERS[target_led + 1U]);
    for (i = 0U; i < 3U; i++) {
        LED_SelectSingle(target_led);
        Buzzer_PlayTone(LED_NOTES[target_led], 260U);
        LED_AllOff();
        Delay_ms(150U);
    }

    selected_led = target_led;
    LED_SelectSingle(selected_led);
    Display_Time(current_level, time_left);
    LCD_UpdatePlayScreen(current_level, time_left, (uint8_t)(player_step + 1U), seq_length, current_score);
    btn_event = BTN_NONE;
}

/* Main State Machine controlling BOMB MISSION */
void Game_Control(void) {
    static uint8_t  last_tick_sec = 0xFFU;
    static uint8_t  idle_printed  = 0U;
    static uint32_t last_idle_msg = 0U;
    uint16_t stage_pts;
    uint8_t btn = Switch_Read();

    switch (current_state) {
        case STATE_IDLE:
            Display_Text(SEG_CHAR_r, SEG_CHAR_E, SEG_CHAR_d, SEG_CHAR_Y); /* "rEdY" */

            if (idle_printed == 0U) {
                last_idle_msg = ms_ticks;
                LCD_ShowStatus("  BOMB MISSION  ", "Press S3 to Play");

                UART0_Print("\r\n========================================\r\n");
                UART0_Print("  BOMB MISSION - SIMON MEMORY CHALLENGE\r\n");
                UART0_Print("  Status : READY (Press S3 to Start)\r\n");
                UART0_Print("========================================\r\n");
                idle_printed = 1U;
            } else if ((ms_ticks - last_idle_msg) >= 3000U) {
                last_idle_msg = ms_ticks;
                UART0_Print("[READY] Press S3 on Shield to Start (Last Score: ");
                UART0_PrintNum(current_score);
                UART0_Print(")\r\n");
            }

            if (btn == BTN_S3_START) {
                prng_seed ^= ms_ticks;
                current_level = 1U;
                current_score = 0U;
                scanner_available = 1U;
                idle_printed = 0U;

                UART0_Print("\r\n>>> MISSION STARTED! <<<\r\n");

                Buzzer_PlayTone(NOTE_E5, 65U);
                Buzzer_PlayTone(NOTE_G5, 65U);
                Buzzer_PlayTone(NOTE_B5, 65U);
                Buzzer_PlayTone(NOTE_E6, 140U);
                current_state = STATE_LEVEL_INIT;
            }
            break;

        case STATE_LEVEL_INIT:
            timer_active = 0U;
            reset_sec_counter = 1U;
            LED_AllOff();

            if (current_level == 1U) {
                seq_length = 3U;
                time_left  = 30U;
            } else if (current_level == 2U) {
                seq_length = 5U;
                time_left  = 25U;
            } else {
                seq_length = 7U;
                time_left  = 20U;
            }

            if (lcd_connected != 0U) {
                LCD_Clear();
                LCD_SetCursor(0U, 0U);
                LCD_Print("STAGE ");
                LCD_PrintNum(current_level);
                LCD_Print("  [");
                LCD_PrintNum(seq_length);
                LCD_Print(" LED]");
                LCD_SetCursor(0U, 1U);
                LCD_Print("Get Ready... ");
                LCD_PrintNum(time_left);
                LCD_Print("s");
            }

            UART0_Print("\r\n[STAGE ");
            UART0_PrintNum(current_level);
            UART0_Print("] Wires: ");
            UART0_PrintNum(seq_length);
            UART0_Print(" | Time Limit: ");
            UART0_PrintNum(time_left);
            UART0_Print("s | Current Score: ");
            UART0_PrintNum(current_score);
            UART0_Print("\r\n");

            Level_Ready_Signal(current_level, time_left);

            Display_Time(current_level, time_left);
            Generate_Sequence(seq_length);
            current_state = STATE_SHOW_SEQ;
            break;

        case STATE_SHOW_SEQ:
            if (lcd_connected != 0U) {
                LCD_Clear();
                LCD_SetCursor(0U, 0U);
                LCD_Print("STAGE ");
                LCD_PrintNum(current_level);
                LCD_Print(" - WATCH");
                LCD_SetCursor(0U, 1U);
                LCD_Print("Memorize Pattern");
            }

            UART0_Print("[WATCH] Memorize the LED sequence...\r\n");
            Play_Sequence(seq_length, current_level);

            player_step       = 0U;
            selected_led      = 0U;
            last_tick_sec     = 0xFFU;
            LED_SelectSingle(selected_led);
            btn_event         = BTN_NONE;
            reset_sec_counter = 1U;
            timer_active      = 1U;

            LCD_UpdatePlayScreen(current_level, time_left, 1U, seq_length, current_score);
            UART0_Print("[INPUT] Your turn! (S1:Select, Hold S1:Scan, S2:Confirm)\r\n");
            current_state     = STATE_PLAYER_INPUT;
            break;

        case STATE_PLAYER_INPUT:
            Display_Time(current_level, time_left);

            if (time_left != last_tick_sec) {
                last_tick_sec = time_left;
                LCD_UpdatePlayScreen(current_level, time_left, (uint8_t)(player_step + 1U), seq_length, current_score);

                if ((time_left > 0U) && (time_left <= 5U)) {
                    Buzzer_PlayTone(NOTE_E6, 25U);
                    UART0_Print("[WARNING] ");
                    UART0_PrintNum(time_left);
                    UART0_Print("s remaining!\r\n");
                }
            }

            if (time_left == 0U) {
                timer_active = 0U;
                fail_reason = 2U;
                LED_AllOff();
                current_state = STATE_GAME_OVER;
                break;
            }

            if (btn == BTN_S1_SELECT) {
                selected_led = (selected_led + 1U) % 4U;
                LED_SelectSingle(selected_led);
                Buzzer_PlayTone(LED_NOTES[selected_led], 50U);
            } else if (btn == BTN_S1_LONG) {
                Trigger_EmergencyScanner();
            } else if (btn == BTN_S2_CONFIRM) {
                Buzzer_PlayTone(NOTE_B5, 35U);
                Buzzer_PlayTone(NOTE_E6, 55U);

                if (selected_led == led_sequence[player_step]) {
                    player_step++;
                    UART0_Print("  -> Step ");
                    UART0_PrintNum(player_step);
                    UART0_Print("/");
                    UART0_PrintNum(seq_length);
                    UART0_Print(" : Wire D");
                    UART0_PrintNum((uint32_t)(selected_led + 1U));
                    UART0_Print(" [OK]\r\n");

                    if (player_step >= seq_length) {
                        timer_active = 0U;
                        LED_AllOff();

                        stage_pts = (uint16_t)(time_left * 25U * current_level);
                        current_score += stage_pts;

                        if (lcd_connected != 0U) {
                            LCD_Clear();
                            LCD_SetCursor(0U, 0U);
                            LCD_Print("STAGE ");
                            LCD_PrintNum(current_level);
                            LCD_Print(" CLEARED!");
                            LCD_SetCursor(0U, 1U);
                            LCD_Print("Score: ");
                            LCD_PrintNum(current_score);
                            LCD_Print(" pts");
                        }

                        UART0_Print("[STAGE ");
                        UART0_PrintNum(current_level);
                        UART0_Print(" CLEARED] +");
                        UART0_PrintNum(stage_pts);
                        UART0_Print(" pts | Total Score: ");
                        UART0_PrintNum(current_score);
                        UART0_Print("\r\n");

                        if (current_level < 3U) {
                            Display_Text(SEG_CHAR_P, SEG_CHAR_A, SEG_CHAR_S, SEG_CHAR_S);
                            Victory_Melody(0U);
                            Delay_ms(350U);

                            current_level++;
                            current_state = STATE_LEVEL_INIT;
                        } else {
                            if (scanner_available != 0U) {
                                current_score += 200U;
                            }
                            if (current_score > 9999U) {
                                current_score = 9999U;
                            }
                            current_state = STATE_VICTORY;
                        }
                    } else {
                        LCD_UpdatePlayScreen(current_level, time_left, (uint8_t)(player_step + 1U), seq_length, current_score);
                    }
                } else {
                    timer_active = 0U;
                    fail_reason = 1U;
                    LED_AllOff();
                    current_state = STATE_GAME_OVER;
                }
            }
            break;

        case STATE_VICTORY: {
            uint32_t toggle_t0;
            uint8_t  show_score = 0U;

            if (lcd_connected != 0U) {
                LCD_Clear();
                LCD_SetCursor(0U, 0U);
                LCD_Print("MISSION PASSED! ");
                LCD_SetCursor(0U, 1U);
                LCD_Print("FINAL: ");
                LCD_PrintNum(current_score);
                LCD_Print(" PTS");
            }

            UART0_Print("\r\n****************************************\r\n");
            UART0_Print("  MISSION ACCOMPLISHED - BOMB DEFUSED!\r\n");
            UART0_Print("  No-Scanner Bonus : ");
            UART0_Print((scanner_available != 0U) ? "+200 pts\r\n" : "0 pts (Scanner used)\r\n");
            UART0_Print("  FINAL SCORE      : ");
            UART0_PrintNum(current_score);
            UART0_Print(" PTS\r\n");
            UART0_Print("  Press S3 on Shield to Play Again\r\n");
            UART0_Print("****************************************\r\n");

            Display_Text(SEG_CHAR_P, SEG_CHAR_A, SEG_CHAR_S, SEG_CHAR_S);
            LED_AllOn();
            Victory_Melody(1U);
            LED_AllOff();

            btn_event = BTN_NONE;
            toggle_t0 = ms_ticks;

            while (Switch_Read() != BTN_S3_START) {
                if ((ms_ticks - toggle_t0) >= 1200U) {
                    toggle_t0 = ms_ticks;
                    show_score ^= 1U;
                    if (show_score != 0U) {
                        Display_Score(current_score);
                    } else {
                        Display_Text(SEG_CHAR_P, SEG_CHAR_A, SEG_CHAR_S, SEG_CHAR_S);
                    }
                }
                __NOP();
            }
            idle_printed = 0U;
            current_state = STATE_IDLE;
            break;
        }

        case STATE_GAME_OVER:
            if (lcd_connected != 0U) {
                LCD_Clear();
                LCD_SetCursor(0U, 0U);
                LCD_Print("BOOM! GAME OVER ");
                LCD_SetCursor(0U, 1U);
                if (fail_reason == 2U) {
                    LCD_Print("Time Expired!   ");
                } else {
                    LCD_Print("Wrong Wire! D");
                    LCD_PrintNum((uint32_t)(selected_led + 1U));
                }
            }

            UART0_Print("\r\nXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\r\n");
            UART0_Print("  GAME OVER - BOMB DETONATED!\r\n");
            if (fail_reason == 2U) {
                UART0_Print("  Reason      : Time Expired (0s)\r\n");
            } else {
                UART0_Print("  Reason      : Wrong Wire (Picked D");
                UART0_PrintNum((uint32_t)(selected_led + 1U));
                UART0_Print(", Expected D");
                UART0_PrintNum((uint32_t)(led_sequence[player_step] + 1U));
                UART0_Print(")\r\n");
            }
            UART0_Print("  Final Score : ");
            UART0_PrintNum(current_score);
            UART0_Print(" PTS\r\n");
            UART0_Print("  Press S3 on Shield to Retry\r\n");
            UART0_Print("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\r\n");

            GameOver_Melody();

            btn_event = BTN_NONE;
            while (Switch_Read() != BTN_S3_START) {
                __NOP();
            }
            idle_printed = 0U;
            current_state = STATE_IDLE;
            break;
    }
}

/* ============================================================================
 * 10. MAIN ENTRY POINT
 * ============================================================================ */
int main(void) {
    GPIO_Init();
    LED_Init();
    Switch_Init();
    SevenSegment_Init();
    Buzzer_Init();
    Timer_Init();
    UART0_Init();
    LCD_Init();

    while (1) {
        Game_Control();
    }
}