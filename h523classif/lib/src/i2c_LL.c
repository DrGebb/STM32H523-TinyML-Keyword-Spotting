#include "i2c_LL.h"

#define SSD1306_ONLY

static __IO i2c_handle_t i2c_handle = {0};

void I2C_LL_Init_test() {

  // I2C conf h523
  LL_I2C_InitTypeDef I2C_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_RCC_SetI2CClockSource(LL_RCC_I2C1_CLKSOURCE_HSI);

  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);
  LL_I2C_Disable(I2C1);
  /**I2C1 GPIO Configuration
  PB7   ------> I2C1_SDA
  PB8   ------> I2C1_SCL
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_7 | LL_GPIO_PIN_8;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_4;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Peripheral clock enable
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

  // I2C Initializatio
  LL_I2C_EnableAutoEndMode(I2C1);
  LL_I2C_DisableOwnAddress2(I2C1);
  LL_I2C_DisableGeneralCall(I2C1);
  LL_I2C_EnableClockStretching(I2C1);
  I2C_InitStruct.PeripheralMode = LL_I2C_MODE_I2C;
  I2C_InitStruct.Timing = 0x00900F2E;
  I2C_InitStruct.AnalogFilter = LL_I2C_ANALOGFILTER_ENABLE;
  I2C_InitStruct.DigitalFilter = 1;
  I2C_InitStruct.OwnAddress1 = 0;
  I2C_InitStruct.TypeAcknowledge = LL_I2C_ACK;
  I2C_InitStruct.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT;
  LL_I2C_Init(I2C1, &I2C_InitStruct);
  LL_I2C_SetOwnAddress2(I2C1, 0, LL_I2C_OWNADDRESS2_NOMASK);

  LL_I2C_Enable(I2C1);
}

/****** for OLED ssd1306 */

Error_I2C I2C_LL_Master_Transmit_SSD1306(I2C_TypeDef *I2Cx, uint8_t SLAVE_ADDR,
                                         uint8_t control, uint8_t *pData,
                                         uint32_t len) {
  uint32_t start_tick;
  uint32_t remaining = 1U + len; // 1 control byte + len data bytes
  uint32_t idx = 0;
  uint8_t control_sent = 0;

  /* 1. Wait for bus idle */
  start_tick = GetSysTick();
  while (LL_I2C_IsActiveFlag_BUSY(I2Cx)) {
    if ((GetSysTick() - start_tick) > SSD1306_I2C_TIMEOUT)
      return ERROR_I2C_BUSY;
  }

  /* Clear residual STOP / NACK flags */
  LL_I2C_ClearFlag_STOP(I2Cx);
  LL_I2C_ClearFlag_NACK(I2Cx);

  /* 2. First block (up to 255 bytes), START generation, and address
     transmission. Note: 7-bit address field SADD[7:1] requires the address
     left-shifted by 1 bit */
  uint32_t chunk = (remaining > 255U) ? 255U : remaining;
  uint32_t end_mode =
      (remaining > 255U) ? LL_I2C_MODE_RELOAD : LL_I2C_MODE_AUTOEND;

  LL_I2C_HandleTransfer(I2Cx, (uint32_t)(SLAVE_ADDR << 1),
                        LL_I2C_ADDRSLAVE_7BIT, chunk, end_mode,
                        LL_I2C_GENERATE_START_WRITE);

  /* 3. Block transmission loop */
  while (remaining > 0U) {
    for (uint32_t i = 0; i < chunk; i++) {
      start_tick = GetSysTick();
      while (!LL_I2C_IsActiveFlag_TXIS(I2Cx)) {
        if (LL_I2C_IsActiveFlag_NACK(I2Cx)) {
          LL_I2C_ClearFlag_NACK(I2Cx);
          LL_I2C_ClearFlag_STOP(I2Cx);
          LL_I2C_ClearFlag_TXE(I2Cx); // flush TXDR if not empty
          return ERROR_I2C_TIMEOUT;   // or dedicated NACK error code if defined
        }
        if ((GetSysTick() - start_tick) > SSD1306_I2C_TIMEOUT) {
          LL_I2C_GenerateStopCondition(I2Cx);
          LL_I2C_ClearFlag_TXE(I2Cx);
          return ERROR_I2C_TIMEOUT;
        }
      }

      if (!control_sent) {
        LL_I2C_TransmitData8(I2Cx, control);
        control_sent = 1;
      } else {
        LL_I2C_TransmitData8(I2Cx, pData[idx++]);
      }
    }

    remaining -= chunk;

    /* If data remains (> 255 bytes total), wait for TCR and configure next
     * block without START/STOP */
    if (remaining > 0U) {
      start_tick = GetSysTick();
      while (!LL_I2C_IsActiveFlag_TCR(I2Cx)) {
        if (LL_I2C_IsActiveFlag_NACK(I2Cx)) {
          LL_I2C_ClearFlag_NACK(I2Cx);
          LL_I2C_ClearFlag_STOP(I2Cx);
          return ERROR_I2C_TIMEOUT;
        }
        if ((GetSysTick() - start_tick) > SSD1306_I2C_TIMEOUT) {
          LL_I2C_GenerateStopCondition(I2Cx);
          return ERROR_I2C_TIMEOUT;
        }
      }

      chunk = (remaining > 255U) ? 255U : remaining;
      end_mode = (remaining > 255U) ? LL_I2C_MODE_RELOAD : LL_I2C_MODE_AUTOEND;

      LL_I2C_HandleTransfer(I2Cx, (uint32_t)(SLAVE_ADDR << 1),
                            LL_I2C_ADDRSLAVE_7BIT, chunk, end_mode,
                            LL_I2C_GENERATE_NOSTARTSTOP);
    }
  }

  /* 4. Wait for automatic STOP condition (AUTOEND is enabled for the final
   * block) */
  start_tick = GetSysTick();
  while (!LL_I2C_IsActiveFlag_STOP(I2Cx)) {
    if (LL_I2C_IsActiveFlag_NACK(I2Cx)) {
      LL_I2C_ClearFlag_NACK(I2Cx);
      LL_I2C_ClearFlag_STOP(I2Cx);
      return ERROR_I2C_TIMEOUT;
    }
    if ((GetSysTick() - start_tick) > SSD1306_I2C_TIMEOUT) {
      return ERROR_I2C_TIMEOUT;
    }
  }

  LL_I2C_ClearFlag_STOP(I2Cx);

  return SUCCESS_I2C;
}

void I2C_ResetBus(I2C_TypeDef *I2Cx) {

  if (I2Cx == I2C1) {

    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_I2C1);
  }

  if (I2Cx == I2C2) {

    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_I2C2);
  }

  __NOP();
  __NOP();
  __NOP();
  __NOP();

  if (I2Cx == I2C1) {

    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_I2C1);
  }

  if (I2Cx == I2C2) {

    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_I2C1);
  }

  I2C_LL_Init_test();
}

#ifndef SSD1306_ONLY

Error_I2C I2C_LL_Master_Transmit(I2C_TypeDef *I2Cx, uint8_t SLAVE_ADDR,
                                 uint8_t *pData, uint32_t len) {

  // star cond
  LL_I2C_GenerateStartCondition(I2Cx);
  while (!LL_I2C_IsActiveFlag_SB(I2Cx))
    __NOP();

  // send addr
  LL_I2C_TransmitData8(I2Cx, (SLAVE_ADDR << 1) | 0x00);
  while (!LL_I2C_IsActiveFlag_ADDR(I2Cx))
    __NOP();
  LL_I2C_ClearFlag_ADDR(I2Cx); // clear flag addr

  // send data
  for (uint32_t i = 0; i < len; i++) {

    while (!LL_I2C_IsActiveFlag_TXE(I2Cx))
      __NOP();
    LL_I2C_TransmitData8(I2Cx, pData[i]);
  }

  while (!LL_I2C_IsActiveFlag_TXE(I2Cx))
    __NOP();
  while (!LL_I2C_IsActiveFlag_BTF(I2Cx))
    __NOP();

  LL_I2C_GenerateStopCondition(I2Cx);

  return SUCCESS_I2C;
}
// 7bit add, max 2^16 size data_arr
Status_I2C_t I2C_LL_TransmitIT(I2C_TypeDef *I2Cx, uint8_t SLAVE_ADDR,
                               uint8_t *pData, uint16_t len) {

  // exit if not wait state
  if (i2c_handle.status != I2C_STATUS_WAIT) {
    return i2c_handle.status;
  }

  // fill handle
  i2c_handle.status =
      I2C_STATUS_START; // start status != start_condition in i2c
  i2c_handle.txBuffer = pData;
  i2c_handle.txBufferSize = len;
  i2c_handle.txAddress = SLAVE_ADDR;
  i2c_handle.txCount = 0;

  // clear all flags
  LL_I2C_ClearFlag_ADDR(I2Cx);
  LL_I2C_ClearFlag_AF(I2Cx);
  LL_I2C_ClearFlag_ARLO(I2Cx);
  LL_I2C_ClearFlag_BERR(I2Cx);
  LL_I2C_ClearFlag_OVR(I2Cx);
  LL_I2C_ClearFlag_STOP(I2Cx);

  // en IT
  LL_I2C_EnableIT_TX(I2Cx);  // en tx empty IT
  LL_I2C_EnableIT_ERR(I2Cx); // en err event IT
  LL_I2C_EnableIT_EVT(I2Cx); // en varios event IT
  NVIC_EnableIRQ(I2C1_EV_IRQn);
  NVIC_EnableIRQ(I2C1_ER_IRQn);

  // start cond
  LL_I2C_GenerateStartCondition(I2Cx);

  return I2C_STATUS_START;
}

void I2C_LL_DisableTX_IT(I2C_TypeDef *I2Cx) {

  LL_I2C_DisableIT_TX(I2Cx);
  LL_I2C_DisableIT_EVT(I2Cx);
  LL_I2C_DisableIT_ERR(I2Cx);
  LL_I2C_Disable(I2Cx);
  LL_I2C_Enable(I2Cx);
}

void I2C1_EV_IRQHandler(void) {

  // after start condition
  if (i2c_handle.status == I2C_STATUS_START && (LL_I2C_IsActiveFlag_SB(I2C1))) {

    uint8_t temp = (i2c_handle.txAddress << 1) | 0x00;
    LL_I2C_TransmitData8(I2C1, temp);
    i2c_handle.status = I2C_STATUS_SEND_ADDR;
  }

  // after sending address
  else if (i2c_handle.status == I2C_STATUS_SEND_ADDR &&
           (LL_I2C_IsActiveFlag_ADDR(I2C1))) {

    LL_I2C_ClearFlag_ADDR(I2C1); // clear flag ADDR
    i2c_handle.status = I2C_STATUS_TRANSMIT;

  }

  // send data
  else if (i2c_handle.status == I2C_STATUS_TRANSMIT &&
           (LL_I2C_IsActiveFlag_TXE(I2C1))) {

    if (i2c_handle.txCount < i2c_handle.txBufferSize) {

      LL_I2C_TransmitData8(I2C1, i2c_handle.txBuffer[i2c_handle.txCount++]);

    } else
      i2c_handle.status = I2C_STATUS_WAIT_BTF;

  }

  // end transmit and disble IT
  else if (i2c_handle.status == I2C_STATUS_WAIT_BTF &&
           (LL_I2C_IsActiveFlag_BTF(I2C1))) {

    LL_I2C_GenerateStopCondition(I2C1);
    LL_I2C_DisableIT_TX(I2C1);
    LL_I2C_DisableIT_EVT(I2C1);
    LL_I2C_DisableIT_ERR(I2C1);
    i2c_handle.status = I2C_STATUS_COMLETE;
    if (i2c_handle.CmplCallBack)
      i2c_handle.CmplCallBack(); // call callback function;
  }
}

void I2C1_ER_IRQHandler(void) {

  if (LL_I2C_IsActiveFlag_AF(I2C1)) {

    LL_I2C_GenerateStopCondition(I2C1);
    LL_I2C_ClearFlag_AF(I2C1);
    I2C_LL_DisableTX_IT(I2C1);
    i2c_handle.status = I2C_STATUS_ERR_AF;
    if (i2c_handle.ErrCallBack)
      i2c_handle.ErrCallBack(I2C_STATUS_ERR_AF);
  }

  if (LL_I2C_IsActiveFlag_BERR(I2C1)) {

    LL_I2C_ClearFlag_BERR(I2C1);
    I2C_LL_DisableTX_IT(I2C1);
    i2c_handle.status = I2C_STATUS_ERR_BER;
    if (i2c_handle.ErrCallBack)
      i2c_handle.ErrCallBack(I2C_STATUS_ERR_BER);
  }

  if (LL_I2C_IsActiveFlag_ARLO(I2C1)) {

    LL_I2C_ClearFlag_ARLO(I2C1);
    I2C_LL_DisableTX_IT(I2C1);
    i2c_handle.status = I2C_STATUS_ERR_ARLO;
    if (i2c_handle.ErrCallBack)
      i2c_handle.ErrCallBack(I2C_STATUS_ERR_ARLO);
  }

  if (LL_I2C_IsActiveFlag_OVR(I2C1)) {

    LL_I2C_ClearFlag_OVR(I2C1);
    I2C_LL_DisableTX_IT(I2C1);
    i2c_handle.status = I2C_STATUS_ERR_OVR;
    if (i2c_handle.ErrCallBack)
      i2c_handle.ErrCallBack(I2C_STATUS_ERR_OVR);
  }
}

/**
 * @brief  Returns the generalized state of the I2C driver, suitable for use by
 * application code.
 * @retval I2C_ApiStatus_t: current generalized status (READY, BUSY, COMPLETE,
 * ERROR)
 */
Status_api_i2c_t I2C_LL_GetStatus(void) {

  // get curent status
  Status_I2C_t internal_status = i2c_handle.status;

  // groups
  switch (internal_status) {
  // BUSY
  case I2C_STATUS_START:
  case I2C_STATUS_SEND_ADDR:
  case I2C_STATUS_TRANSMIT:
  case I2C_STATUS_WAIT_BTF:
  case I2C_STATUS_DMA_ACTIVE:
    return I2C_API_STATUS_BUSY;

  // COMPLETE
  case I2C_STATUS_COMLETE:
    return I2C_API_STATUS_COMPLETE;

  // ERROR
  case I2C_STATUS_ERR_AF:
  case I2C_STATUS_ERR_BER:
  case I2C_STATUS_ERR_ARLO:
  case I2C_STATUS_ERR_OVR:
  case I2C_STATUS_DMA_ERR:
    return I2C_API_STATUS_ERROR;

  // status "wait" - ready
  case I2C_STATUS_WAIT:
  default:
    return I2C_API_STATUS_READY;
  }
}

/**
 * @brief  Returns a detailed status/error code.
 *         Useful for debugging after I2C_LL_GetStatus() has returned ERROR.
 * @retval I2C_Status_t: the current detailed status from the handler.
 */
Status_I2C_t I2C_LL_GetDetailedStatus(void) { return i2c_handle.status; }

/**
 * @brief  Resets the driver state to I2C_STATUS_WAIT.
 */
void I2C_LL_ResetReadyState(void) {
  //
  i2c_handle.status = I2C_STATUS_WAIT;
}

Status_I2C_t I2C_LL_TransmitDMA(I2C_TypeDef *I2Cx, uint8_t slave_addr,
                                uint8_t *pData, uint16_t len) {

  if (i2c_handle.status != I2C_STATUS_WAIT) {
    return i2c_handle.status;
  }

  uint32_t start_tick = 0; // for timeout

  // en I2C
  LL_I2C_Enable(I2Cx);

  // fill handle
  i2c_handle.status = I2C_STATUS_START;
  i2c_handle.txBuffer = pData;
  i2c_handle.txBufferSize = len;
  i2c_handle.txAddress = slave_addr;
  i2c_handle.txCount = 0;

  // DMA conf
  LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_6);
  while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_6))
    __NOP();
  LL_DMA_SetMemoryAddress(DMA1, LL_DMA_STREAM_6, (uint32_t)pData);
  LL_DMA_SetDataLength(DMA1, LL_DMA_STREAM_6, len);

  // en IT for ERR
  LL_I2C_EnableIT_ERR(I2Cx);
  NVIC_EnableIRQ(I2C1_ER_IRQn);

  // start
  LL_I2C_GenerateStartCondition(I2Cx);
  start_tick = msTick;
  while (!LL_I2C_IsActiveFlag_SB(I2Cx)) {

    // if BERR
    if (LL_I2C_IsActiveFlag_BERR(I2Cx)) {

      LL_I2C_ClearFlag_BERR(I2Cx);
      LL_I2C_GenerateStopCondition(I2Cx); // sent STOP
      LL_I2C_DisableIT_ERR(I2Cx);
      LL_I2C_Disable(I2Cx);
      LL_I2C_Enable(I2Cx);
      return (i2c_handle.status = I2C_STATUS_ERR_BER);
    }
    // if timeout
    if ((msTick - start_tick) > TIMEOUT_I2C) {

      LL_I2C_GenerateStopCondition(I2Cx); // sent STOP
      LL_I2C_DisableIT_ERR(I2Cx);
      LL_I2C_Disable(I2Cx);
      LL_I2C_Enable(I2Cx);
      return (i2c_handle.status = I2C_STATUS_TIMEOUT);
    }
  }

  // ADDR phase
  i2c_handle.status = I2C_STATUS_SEND_ADDR;
  LL_I2C_TransmitData8(I2Cx, (slave_addr << 1) | 0x00);
  start_tick = msTick;
  while (!LL_I2C_IsActiveFlag_ADDR(I2Cx)) {

    // if AF
    if (LL_I2C_IsActiveFlag_AF(I2Cx)) {

      LL_I2C_ClearFlag_AF(I2Cx);
      LL_I2C_GenerateStopCondition(I2Cx); // sent STOP
      LL_I2C_DisableIT_ERR(I2Cx);
      LL_I2C_Disable(I2Cx);
      LL_I2C_Enable(I2Cx);
      return (i2c_handle.status = I2C_STATUS_ERR_AF);
    }

    // if BERR
    if (LL_I2C_IsActiveFlag_BERR(I2Cx)) {

      LL_I2C_ClearFlag_BERR(I2Cx);
      LL_I2C_GenerateStopCondition(I2Cx); // sent STOP
      LL_I2C_DisableIT_ERR(I2Cx);
      LL_I2C_Disable(I2Cx);
      LL_I2C_Enable(I2Cx);
      return (i2c_handle.status = I2C_STATUS_ERR_BER);
    }

    // if timeout
    if ((msTick - start_tick) > TIMEOUT_I2C) {

      LL_I2C_GenerateStopCondition(I2Cx); // sent STOP
      LL_I2C_DisableIT_ERR(I2Cx);
      LL_I2C_Disable(I2Cx);
      LL_I2C_Enable(I2Cx);
      return (i2c_handle.status = I2C_STATUS_TIMEOUT);
    }
  }

  // en i2c to dma req TX
  i2c_handle.status = I2C_STATUS_DMA_ACTIVE;
  LL_I2C_EnableDMAReq_TX(I2Cx);

  // en dma stream6
  LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_6);

  LL_I2C_ClearFlag_ADDR(
      I2Cx); // clear flag addr and I2C send req to DMA - because TXE

  return i2c_handle.status;
}

// i2c1_tx DMA1 Stream6 ch1
void I2C1_DMA_LL_Init(void) {

  // CLK en DMA1
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

  // DMA conf
  LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_6);
  // wait if stream en
  while (LL_DMA_IsEnabledStream(DMA1, LL_DMA_STREAM_6))
    __NOP();
  // reset flags for stream6
  LL_DMA_ClearFlag_HT6(DMA1);
  LL_DMA_ClearFlag_TC6(DMA1);
  LL_DMA_ClearFlag_FE6(DMA1);
  LL_DMA_ClearFlag_TE6(DMA1);
  LL_DMA_ClearFlag_DME6(DMA1);
  // fill sruct
  LL_DMA_InitTypeDef dma_init = {0};
  dma_init.Channel = LL_DMA_CHANNEL_1;
  dma_init.Mode = LL_DMA_MODE_NORMAL;
  dma_init.Direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
  dma_init.PeriphOrM2MSrcAddress = (uint32_t)LL_I2C_DMA_GetRegAddr(I2C1);
  dma_init.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE;
  dma_init.MemoryOrM2MDstAddress = 0;
  dma_init.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE;
  dma_init.NbData = 0;
  dma_init.PeriphBurst = LL_DMA_PBURST_SINGLE;
  dma_init.MemBurst = LL_DMA_MBURST_SINGLE;
  dma_init.FIFOMode = LL_DMA_FIFOMODE_ENABLE;
  dma_init.FIFOThreshold = LL_DMA_FIFOTHRESHOLD_1_4;
  dma_init.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;
  dma_init.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
  dma_init.Priority = LL_DMA_PRIORITY_HIGH;
  LL_DMA_Init(DMA1, LL_DMA_STREAM_6, &dma_init);

  LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_6); // en IT for transfer compl
  NVIC_EnableIRQ(DMA1_Stream6_IRQn);

  // LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_6);
}

void DMA1_Stream6_IRQHandler(void) {

  // transfer compl
  if (LL_DMA_IsActiveFlag_TC6(DMA1)) {

    LL_DMA_ClearFlag_TC6(DMA1);
    LL_I2C_DisableDMAReq_TX(I2C1);
    while (!LL_I2C_IsActiveFlag_BTF(I2C1))
      __NOP();                                   // wait Byte Transfer Finished
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_6); //
    LL_I2C_GenerateStopCondition(I2C1);
    LL_I2C_DisableIT_ERR(I2C1);
    i2c_handle.status = I2C_STATUS_COMLETE;
    if (i2c_handle.CmplCallBack)
      i2c_handle.CmplCallBack(); // call callback function;

  }
  // err
  else if (LL_DMA_IsActiveFlag_FE6(DMA1) || LL_DMA_IsActiveFlag_DME6(DMA1) ||
           LL_DMA_IsActiveFlag_FE6(DMA1)) {

    LL_I2C_DisableDMAReq_TX(I2C1);               // dis for I2C1
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_6); //

    LL_DMA_ClearFlag_FE6(DMA1);
    LL_DMA_ClearFlag_TE6(DMA1);
    LL_DMA_ClearFlag_DME6(DMA1);

    LL_I2C_DisableIT_ERR(I2C1);
    LL_I2C_Disable(I2C1);
    LL_I2C_Enable(I2C1);

    i2c_handle.status = I2C_STATUS_DMA_ERR;
    if (i2c_handle.ErrCallBack)
      i2c_handle.ErrCallBack(I2C_STATUS_DMA_ERR);
  }
}

void I2C_RegisterCallBacks(I2C_CmplCallBack_t CmplCallBack,
                           I2C_ErrCallBack_t ErrCallBack) {

  i2c_handle.CmplCallBack = CmplCallBack;
  i2c_handle.ErrCallBack = ErrCallBack;
}

#endif

/*
Error_I2C I2C_LL_Master_Transmit_SSD1306(I2C_TypeDef *I2Cx, uint8_t SLAVE_ADDR,
uint8_t control, uint8_t *pData, uint32_t len){

    //star cond
    LL_I2C_GenerateStartCondition(I2Cx);
    while (!LL_I2C_IsActiveFlag_SB(I2Cx)) __NOP();

    //send addr
    LL_I2C_TransmitData8(I2Cx, (SLAVE_ADDR << 1 ) | 0x00);
    while(!LL_I2C_IsActiveFlag_ADDR(I2Cx)) __NOP();
    LL_I2C_ClearFlag_ADDR(I2Cx); //clear flag addr

    //send control
    while(!LL_I2C_IsActiveFlag_TXE(I2Cx)) __NOP();
    LL_I2C_TransmitData8(I2Cx, control);


    //send data
    for(uint32_t i = 0; i < len; i++){

        while(!LL_I2C_IsActiveFlag_TXE(I2Cx)) __NOP();
        LL_I2C_TransmitData8(I2Cx, pData[i]);

    }

    while(!LL_I2C_IsActiveFlag_TXE(I2Cx)) __NOP();
    while(!LL_I2C_IsActiveFlag_BTF(I2Cx)) __NOP();

    LL_I2C_GenerateStopCondition(I2Cx);

    return SUCCESS_I2C;

}
 */