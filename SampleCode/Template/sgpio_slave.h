#ifndef __SGPIO_SLAVE_H__
#define __SGPIO_SLAVE_H__

/*
 * Fixed SPI0 SGPIO wiring:
 *   GP21 SCLK      -> M032 PA2 SPI0_CLK input
 *   GP20 SDATA OUT -> M032 PA0 SPI0_MOSI input
 *   GP19 SLOAD     -> M032 PA3 SPI0_SS input
 */
#define SGPIO_SLAVE_SLOAD_PIN_NAME       "PA3"
#define SGPIO_SLAVE_SDOUT_PIN_NAME       "PA0"
#define SGPIO_SLAVE_SCLK_PIN_NAME        "PA2"

#define SGPIO_SLAVE_MAX_SLOTS            (16U)
#define SGPIO_SLAVE_RX_MAX_BYTES         (8U)

void SGPIO_Init(void);
void SGPIO_Process(void);

#endif
