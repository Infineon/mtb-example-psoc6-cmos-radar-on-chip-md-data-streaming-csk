/******************************************************************************
 * File Name: resource_map.h
 *
 * Description: This file defines the SPI and GPIO pin map for all the supported kits.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2026-YEAR Cypress Semiconductor $
 *******************************************************************************/


#ifndef RESOURCE_MAP_H_
#define RESOURCE_MAP_H_

#include "cybsp.h"

#ifdef TARGET_APP_CYSBSYSKIT_DEV_01

/** LED pins for radar shield feather adapter board*/
#define SHIELD_XENSIV_RADAR_LED_RED           CYBSP_GPIOA0
/** LED state definitions need to be inverted */
#define SHIELD_XENSIV_RADAR_LED_RED_ON        CYBSP_LED_STATE_OFF
/** LED state definitions need to be inverted */
#define SHIELD_XENSIV_RADAR_LED_RED_OFF       CYBSP_LED_STATE_ON

#define SHIELD_XENSIV_RADAR_LED_GREEN         CYBSP_GPIOA1
/** LED state definitions need to be inverted */
#define SHIELD_XENSIV_RADAR_LED_GREEN_ON     CYBSP_LED_STATE_OFF
/** LED state definitions need to be inverted */
#define SHIELD_XENSIV_RADAR_LED_GREEN_OFF    CYBSP_LED_STATE_ON

#define SHIELD_XENSIV_USER_SWITCH             CYBSP_GPIO6
#define SHIELD_XENSIV_DIR4_EN                 CYBSP_GPIOA2

/** Radar Sensor pins */
#define PIN_XENSIV_BGT60CUTRXX_OPEN_DRAIN     CYBSP_GPIO9
#define PIN_XENSIV_BGT60CUTRXX_PHASE_DETECT   CYBSP_GPIO12 // DIO5
#define PIN_XENSIV_BGT60CUTRXX_TARGET_DETECT  CYBSP_GPIO13

#define PIN_XENSIV_BGT60CUTRXX_IFI            CYBSP_GPIOA3
#define PIN_XENSIV_BGT60CUTRXX_IFQ            CYBSP_GPIOA4

#define PIN_XENSIV_BGT60CUTRXX_DIO2           PIN_XENSIV_BGT60CUTRXX_TARGET_DETECT
#define PIN_XENSIV_BGT60CUTRXX_DIO3           CYBSP_GPIO11


/** Radar sensor SPI pins */
#define PIN_XENSIV_BGT60CUTRXX_SPI_SCLK       CYBSP_SPI_CLK
#define PIN_XENSIV_BGT60CUTRXX_SPI_MOSI       CYBSP_SPI_MOSI
#define PIN_XENSIV_BGT60CUTRXX_SPI_MISO       CYBSP_SPI_MISO
#define PIN_XENSIV_BGT60CUTRXX_SPI_CSN        CYBSP_SPI_CS
#define PIN_XENSIV_BGT60CUTRXX_IRQ            CYBSP_GPIO10
#define PIN_XENSIV_BGT60CUTRXX_RSTN           CYBSP_GPIOA5

#define PIN_XENSIV_BGT60CUTRXX_LDO_EN         CYBSP_GPIO5

#endif // ifdef TARGET_APP_CYSBSYSKIT_DEV_01



#ifdef TARGET_APP_CY8CKIT_062S2_43012

#define PIN_XENSIV_BGT60CUTRXX_SPI_SCLK       CYBSP_SPI_CLK
#define PIN_XENSIV_BGT60CUTRXX_SPI_MOSI       CYBSP_SPI_MOSI
#define PIN_XENSIV_BGT60CUTRXX_SPI_MISO       CYBSP_SPI_MISO
#define PIN_XENSIV_BGT60CUTRXX_SPI_CSN        CYBSP_SPI_CS

#define PIN_XENSIV_BGT60CUTRXX_IRQ            CYBSP_A1
#define PIN_XENSIV_BGT60CUTRXX_RSTN           CYBSP_D2

/** Pin for the radar sensor selection signal */
#define RS_SEL                                CYBSP_D6
/** Pin for the SPI CS selection signal */
#define SHIELD_XENSIV_A_PIN_SPI_CS_SEL0       CYBSP_D9

/** Pin for the sensor interrupt signals */
#define SHIELD_XENSIV_A_PIN_SEN_INT           CYBSP_D7


#define SHIELD_XENSIV_A_PIN_RADAR_GPIO1       CYBSP_D5
/** Pin for the radar GPIO2 or T_DET signal */
#define SHIELD_XENSIV_A_PIN_RADAR_GPIO2       CYBSP_D4
#endif // ifdef TARGET_APP_CY8CKIT_062S2_43012


#endif /* RESOURCE_MAP_H_ */
