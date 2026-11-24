#ifndef GPDMA_LL_H
#define GPDMA_LL_H

#include "RTE_Components.h"
#include CMSIS_device_header

#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"
#include "stm32h5xx_ll_rcc.h"
#include "stm32h5xx_ll_usart.h"
#include "stm32h5xx_ll_dma.h"

#include <string.h>
#include <stdio.h>


#include <stddef.h>

void USART_for_DMA_test_Init(void);

void USART_send_polling(const char *pWord);

//// no link list
void USART1_DMA_TX_Init(void);

void USART1_DMA_TX_struct_Init(void);

void USART1_DMA_Send(const uint8_t *data, uint16_t len);
/////////////////


//1 node link list sindle send
void USART1_DMA_TX_1node_Init(void);

void USART1_DMA_1node_Send(const uint8_t *data, uint16_t len);

//1 node link list circ
void USART1_DMA_TX_Circular_Start(void);

//double buf 2 node LLI
void USART1_DMA_TX_DoubleBuf_Start(void);


#endif