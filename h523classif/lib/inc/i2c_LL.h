#ifndef LL_I2C_H
#define LL_I2C_H

#include "app_main.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_i2c.h"
#include "stm32h5xx_ll_gpio.h"
#include "stm32h5xx_ll_rcc.h"
#include "delay_LL.h"



#define SSD1306_I2C_TIMEOUT  10

#define I2C_PERIPH              I2C1
#define I2C_GPIO_PORT           GPIOB
#define I2C_SCL_PIN             LL_GPIO_PIN_6
#define I2C_SDA_PIN             LL_GPIO_PIN_7
#define I2C_GPIO_AF             LL_GPIO_AF_4

#define TIMEOUT_I2C 20 //in msec

typedef enum __attribute__((packed))  {
    I2C_STATUS_WAIT = 0,
    I2C_STATUS_SEND_ADDR,
    I2C_STATUS_START,
    I2C_STATUS_TRANSMIT,
    I2C_STATUS_DMA_ACTIVE,
    I2C_STATUS_WAIT_BTF,
    I2C_STATUS_COMLETE,
    I2C_STATUS_ERR_AF,
    I2C_STATUS_ERR_BER,
    I2C_STATUS_ERR_OVR,
    I2C_STATUS_ERR_ARLO,
    I2C_STATUS_DMA_ERR,
    I2C_STATUS_TIMEOUT
} Status_I2C_t;

//callbacks t
typedef void (*I2C_CmplCallBack_t)(void);
typedef void (*I2C_ErrCallBack_t)(Status_I2C_t);

typedef enum {
    SUCCESS_I2C = 0,
    ERROR_I2C_TIMEOUT = 1,
    ERROR_I2C_BUSY 
} Error_I2C;



// API
typedef enum {
    I2C_API_STATUS_READY,    
    I2C_API_STATUS_BUSY,      
    I2C_API_STATUS_COMPLETE,  
    I2C_API_STATUS_ERROR      
} Status_api_i2c_t;

typedef struct {

    uint8_t * txBuffer; // pointer to Data arr
    uint16_t txBufferSize;
    uint8_t txAddress;
    uint16_t txCount; // how match was sended yet
    Status_I2C_t status;
    I2C_CmplCallBack_t CmplCallBack;
    I2C_ErrCallBack_t ErrCallBack;

} i2c_handle_t;

void I2C_RegisterCallBacks(I2C_CmplCallBack_t CmplCallBack, I2C_ErrCallBack_t ErrCallBack);

void I2C_LL_Init_test(void);

Error_I2C I2C_LL_Master_Transmit(I2C_TypeDef *I2Cx, uint8_t SLAVE_ADDR, uint8_t *pData, uint32_t len);

Status_I2C_t I2C_LL_TransmitIT(I2C_TypeDef * I2Cx, uint8_t SLAVE_ADDR, uint8_t *pData, uint16_t len );

Status_api_i2c_t I2C_LL_GetStatus(void);

Status_I2C_t I2C_LL_GetDetailedStatus(void);

void I2C_LL_ResetReadyState(void);

void I2C1_DMA_LL_Init(void);

Status_I2C_t I2C_LL_TransmitDMA(I2C_TypeDef * I2Cx, uint8_t slave_addr, uint8_t * pData, uint16_t len);

Error_I2C I2C_LL_Master_Transmit_SSD1306(I2C_TypeDef *I2Cx, uint8_t SLAVE_ADDR, uint8_t control, uint8_t *pData, uint32_t len);

void I2C_ResetBus(I2C_TypeDef * I2Cx);



#endif