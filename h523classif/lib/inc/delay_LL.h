#ifndef DELAY_LL_H
#define  DELAY_LL_H

#include "RTE_Components.h"
#include CMSIS_device_header

extern __IO uint32_t msTick; //msec tick

void Systick_LL_Init(void);

uint32_t GetSysTick(void);

void DelayMs(uint32_t delay_ms);







#endif