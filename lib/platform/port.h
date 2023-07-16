/*! ----------------------------------------------------------------------------
 * @file	port.h
 * @brief	HW specific definitions and functions for portability
 *
 * @attention
 *
 * Copyright 2015 (c) DecaWave Ltd, Dublin, Ireland.
 *
 * All rights reserved.
 *
 * @author DecaWave
 */

#ifndef PORT_H_
#define PORT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <string.h>
#include "../compiler/compiler.h"

#include "stm32f1xx.h"
#include "stm32f1xx_hal.h"

#define BUFFLEN (64) //(4096+128)
#define BUF_SIZE (64)

    typedef uint64_t uint64;
    typedef int64_t int64;

    typedef enum
    {
        LED_PC6, // LED5
        LED_PC7, // LED6
        LED_PC8, // LED7
        LED_PC9, // LED8
        LED_ALL,
        LEDn
    } led_t;

#define DECAIRQ_EXTI_IRQn (EXTI9_5_IRQn)

#define DW1000_RSTn DW_RESET_Pin
#define DW1000_RSTn_GPIO DW_RESET_GPIO_Port

#define DECAIRQ DW_IRQn_Pin
#define DECAIRQ_GPIO DW_IRQn_GPIO_Port

#define TA_SW1_1 GPIO_PIN_3
#define TA_SW1_2 GPIO_PIN_2
#define TA_SW1_GPIO2 GPIOB

#define TA_SW1_3 GPIO_PIN_0
#define TA_SW1_4 GPIO_PIN_1
#define TA_SW1_5 GPIO_PIN_2
#define TA_SW1_6 GPIO_PIN_3
#define TA_SW1_7 GPIO_PIN_4
#define TA_SW1_8 GPIO_PIN_5
#define TA_SW1_GPIO GPIOC

    /****************************************************************************/ /**
                                                                                    *
                                                                                    * 								MACRO function
                                                                                    *
                                                                                    *******************************************************************************/

#define GPIO_ResetBits(x, y) HAL_GPIO_WritePin(x, y, RESET)
#define GPIO_SetBits(x, y) HAL_GPIO_WritePin(x, y, SET)
#define GPIO_ReadInputDataBit(x, y) HAL_GPIO_ReadPin(x, y)

/* NSS pin is SW controllable */
#define port_SPIx_set_chip_select() HAL_GPIO_WritePin(DW_NSS_GPIO_Port, DW_NSS_Pin, GPIO_PIN_SET)
#define port_SPIx_clear_chip_select() HAL_GPIO_WritePin(DW_NSS_GPIO_Port, DW_NSS_Pin, GPIO_PIN_RESET)

    /****************************************************************************/ /**
                                                                                    *
                                                                                    * 								port function prototypes
                                                                                    *
                                                                                    *******************************************************************************/

    void Sleep(uint32_t Delay);
    unsigned long portGetTickCnt(void);

    int portC_is_switch_on(uint16_t GPIOpin);
    int portB_is_switch_on(uint16_t GPIOpin);

    void port_wakeup_dw1000(void);
    void port_wakeup_dw1000_fast(void);

    void port_set_dw1000_slowrate(void);
    void port_set_dw1000_fastrate(void);

    void process_dwRSTn_irq(void);
    void process_deca_irq(void);

    void led_on(led_t led);
    void led_off(led_t led);
    void led_toggle(led_t led);

    int peripherals_init(void);
    void spi_peripheral_init(void);
    void setup_DW1000RSTnIRQ(int enable);
    void reset_DW1000(void);

    ITStatus EXTI_GetITEnStatus(uint32_t x);

    uint32_t port_GetEXT_IRQStatus(void);
    uint32_t port_CheckEXT_IRQ(void);
    void port_DisableEXT_IRQ(void);
    void port_EnableEXT_IRQ(void);
    extern uint32_t HAL_GetTick(void);
    HAL_StatusTypeDef flush_report_buff(void);

#ifdef __cplusplus
}
#endif

#endif
