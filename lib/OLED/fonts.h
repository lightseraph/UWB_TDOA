#ifndef FONTS_H
#define FONTS_H

#ifdef __cplusplus
extern C
{
#endif

#include "stm32f1xx_hal.h"
#include "string.h"

	/**
	 * @defgroup LIB_Typedefs
	 * @brief    Library Typedefs
	 * @{
	 */

	/**
	 * @brief  Font structure used on my LCD libraries
	 */
	typedef struct
	{
		uint8_t FontWidth;	  /*!< Font width in pixels */
		uint8_t FontHeight;	  /*!< Font height in pixels */
		const uint16_t *data; /*!< Pointer to data font data array */
	} FontDef_t;

	/**
	 * @brief  String length and height
	 */
	typedef struct
	{
		uint16_t Length; /*!< String length in units of pixels */
		uint16_t Height; /*!< String height in units of pixels */
	} FONTS_SIZE_t;

	extern FontDef_t Font_7x10;
	extern FontDef_t Font_8x16;
	extern FontDef_t Font_7x14;

	char *FONTS_GetStringSize(char *str, FONTS_SIZE_t *SizeStruct, FontDef_t *Font);

#ifdef __cplusplus
}
#endif

#endif
