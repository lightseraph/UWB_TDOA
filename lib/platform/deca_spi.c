/*! ----------------------------------------------------------------------------
 * @file	deca_spi.c
 * @brief	SPI access functions
 *
 * @attention
 *
 * Copyright 2015 (c) DecaWave Ltd, Dublin, Ireland.
 *
 * All rights reserved.
 *
 * @author DecaWave
 */

#include "deca_spi.h"
#include "deca_device_api.h"
#include "port.h"
#include "main.h"

extern 	SPI_HandleTypeDef hspi1;	/*clocked from 72MHz*/

/****************************************************************************//**
 *
 * 								DW1000 SPI section
 *
 *******************************************************************************/
/*! ------------------------------------------------------------------------------------------------------------------
 * Function: openspi()
 *
 * Low level abstract function to open and initialise access to the SPI device.
 * returns 0 for success, or -1 for error
 */
int openspi(/*SPI_TypeDef* SPIx*/)
{
	return 0;
} // end openspi()

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: closespi()
 *
 * Low level abstract function to close the the SPI device.
 * returns 0 for success, or -1 for error
 */
int closespi(void)
{
	return 0;
} // end closespi()

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: writetospi()
 *
 * Low level abstract function to write to the SPI
 * Takes two separate byte buffers for write header and write data
 * returns 0 for success, or -1 for error
 */
#pragma GCC optimize ("O3")
int writetospi(uint16_t headerLength,
			   const	uint8_t *headerBuffer,
			   uint32_t bodyLength,
			   const	uint8_t *bodyBuffer)
{
//    decaIrqStatus_t  stat ;
//    stat = decamutexon() ;

	  while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY){}

    HAL_GPIO_WritePin(DW_NSS_GPIO_Port, DW_NSS_Pin, GPIO_PIN_RESET); /**< Put chip select line low */

    HAL_SPI_Transmit(&hspi1, (uint8_t *)&headerBuffer[0], headerLength, 10);	/* Send header in polling mode */
    HAL_SPI_Transmit(&hspi1, (uint8_t *)&bodyBuffer[0], bodyLength, 10);		/* Send data in polling mode */

    HAL_GPIO_WritePin(DW_NSS_GPIO_Port, DW_NSS_Pin, GPIO_PIN_SET); /**< Put chip select line high */


//    decamutexoff(stat) ;

    return 0;
} // end writetospi()


/*! ------------------------------------------------------------------------------------------------------------------
 * Function: readfromspi()
 *
 * Low level abstract function to read from the SPI
 * Takes two separate byte buffers for write header and read data
 * returns the offset into read buffer where first byte of read data may be found,
 * or returns -1 if there was an error
 */
#pragma GCC optimize ("O3")
int readfromspi(uint16_t headerLength,
				const uint8_t *headerBuffer,
				uint32_t readlength,
				uint8_t *readBuffer)
{
	
	uint8_t spi_TmpBuffer[BUFFLEN];
	assert_param(headerLength+readlength < BUFFLEN );
	
//    decaIrqStatus_t  stat ;
//    stat = decamutexon() ;

	/* Blocking: Check whether previous transfer has been finished */
	   
	while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY);
	

	HAL_GPIO_WritePin(DW_NSS_GPIO_Port, DW_NSS_Pin, GPIO_PIN_RESET); /**< Put chip select line low */
  
	HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)headerBuffer, spi_TmpBuffer, (uint16_t)(headerLength+readlength), 10);

	HAL_GPIO_WritePin(DW_NSS_GPIO_Port, DW_NSS_Pin, GPIO_PIN_SET); /**< Put chip select line high */

	memcpy((uint8_t*)readBuffer , (uint8_t*)&spi_TmpBuffer[headerLength], readlength);

//	decamutexoff(stat);

    return 0;
} // end readfromspi()

/****************************************************************************//**
 *
 * 								END OF DW1000 SPI section
 *
 *******************************************************************************/



