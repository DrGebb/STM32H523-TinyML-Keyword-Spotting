#include "gpdma_LL.h"





void USART_for_DMA_test_Init(void) {

  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_RCC_SetUSARTClockSource(LL_RCC_USART1_CLKSOURCE_HSI);

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);
  /**USART1 GPIO Configuration
  PB14   ------> USART1_TX
  PB15   ------> USART1_RX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_14 | LL_GPIO_PIN_15;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_MEDIUM;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_4;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  USART_InitStruct.PrescalerValue = LL_USART_PRESCALER_DIV1;
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART1, &USART_InitStruct);
  LL_USART_SetTXFIFOThreshold(USART1, LL_USART_FIFOTHRESHOLD_1_8);
  LL_USART_SetRXFIFOThreshold(USART1, LL_USART_FIFOTHRESHOLD_1_8);
  LL_USART_EnableFIFO(USART1);
  LL_USART_ConfigAsyncMode(USART1);
  LL_USART_Enable(USART1);
}

void USART_send_polling(const char *pWord) {

  if (pWord == NULL)
    return;

  while (*pWord) {

    while (!LL_USART_IsActiveFlag_TXE_TXFNF(USART1)) __NOP();

    LL_USART_TransmitData8(USART1, (uint8_t)(*pWord++));
  }
}

//// Non-linked-list mode /////

void USART1_DMA_TX_Init(void)
{
    /* 1. Enable GPDMA1 clock (mapped to AHB1 bus on STM32H5) */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPDMA1);

    /* Suspend and reset channel if currently enabled */
    if (LL_DMA_IsEnabledChannel(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_SuspendChannel(GPDMA1, LL_DMA_CHANNEL_0);
        while (!LL_DMA_IsActiveFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0)) {}
        LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);
        LL_DMA_ClearFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0);
    }

    /* 2. General channel control (CCR): priority and disable linked-list mode */
    LL_DMA_SetChannelPriorityLevel(GPDMA1, LL_DMA_CHANNEL_0, LL_DMA_LOW_PRIORITY_MID_WEIGHT);
    LL_DMA_SetLinkStepMode(GPDMA1, LL_DMA_CHANNEL_0, LL_DMA_LSM_FULL_EXECUTION);
    LL_DMA_ConfigLinkUpdate(GPDMA1, LL_DMA_CHANNEL_0, 0U, 0U); /* CxLLR = 0: linked-list disabled */

    /* 3. Data parameters (CTR1): 1-byte -> 1-byte, increment source, fixed destination */
    LL_DMA_ConfigTransfer(GPDMA1, LL_DMA_CHANNEL_0,
                          LL_DMA_SRC_ALLOCATED_PORT0    |
                          LL_DMA_DEST_ALLOCATED_PORT1   |
                          LL_DMA_SRC_INCREMENT          |
                          LL_DMA_DEST_FIXED             |
                          LL_DMA_SRC_DATAWIDTH_BYTE     |
                          LL_DMA_DEST_DATAWIDTH_BYTE    |
                          LL_DMA_DATA_ALIGN_ZEROPADD);
    LL_DMA_ConfigBurstLength(GPDMA1, LL_DMA_CHANNEL_0, 1U, 1U); /* Single 1-byte transfers */

    /* 4. Channel and request parameters (CTR2): Memory -> Peripheral, no hardware triggers */
    LL_DMA_ConfigChannelTransfer(GPDMA1, LL_DMA_CHANNEL_0,
                                 LL_DMA_TCEM_BLK_TRANSFER          |
                                 LL_DMA_HWREQUEST_SINGLEBURST      |
                                 LL_DMA_TRIG_POLARITY_MASKED       |
                                 LL_DMA_TRIGM_BLK_TRANSFER         |
                                 LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
                                 LL_DMA_NORMAL);

    LL_DMA_SetPeriphRequest(GPDMA1, LL_DMA_CHANNEL_0, LL_GPDMA1_REQUEST_USART1_TX);

    /* 5. Enable USART1 TX DMA request (configured once and kept enabled) */
    LL_USART_EnableDMAReq_TX(USART1);
}

  // LL_DMA_InitTypeDef struct initialization method
void USART1_DMA_TX_struct_Init(void)
{
    LL_DMA_InitTypeDef dma_init;

    /* 1. Enable GPDMA1 clock */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPDMA1);

    /* For reinitialization: safely stop channel if active */
    if (LL_DMA_IsEnabledChannel(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_SuspendChannel(GPDMA1, LL_DMA_CHANNEL_0);
        while (!LL_DMA_IsActiveFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0)) {}
        LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);
        LL_DMA_ClearFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0);
    }

    /* 2. Reset struct to default values.
          LL_DMA_InitTypeDef contains 37 fields (including 2D and linked-list fields);
          omitting StructInit() leaves uninitialized stack data and triggers USEF. */
    LL_DMA_StructInit(&dma_init);

    /* Direction and mode */
    dma_init.Direction         = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    dma_init.Mode              = LL_DMA_NORMAL;
    dma_init.Request           = LL_GPDMA1_REQUEST_USART1_TX;
    dma_init.BlkHWRequest      = LL_DMA_HWREQUEST_SINGLEBURST;

    /* Source (memory) */
    dma_init.SrcDataWidth      = LL_DMA_SRC_DATAWIDTH_BYTE;
    dma_init.SrcIncMode        = LL_DMA_SRC_INCREMENT;
    dma_init.SrcBurstLength    = 1U;
    dma_init.SrcAllocatedPort  = LL_DMA_SRC_ALLOCATED_PORT0;

    /* Destination (USART1->TDR) */
    dma_init.DestDataWidth     = LL_DMA_DEST_DATAWIDTH_BYTE;
    dma_init.DestIncMode       = LL_DMA_DEST_FIXED;
    dma_init.DestBurstLength   = 1U;
    dma_init.DestAllocatedPort = LL_DMA_DEST_ALLOCATED_PORT1;

    /* Priority and events */
    dma_init.Priority          = LL_DMA_LOW_PRIORITY_MID_WEIGHT;
    dma_init.TransferEventMode = LL_DMA_TCEM_BLK_TRANSFER;

    /* Disable linked-list mode (direct transfer) */
    dma_init.LinkStepMode      = LL_DMA_LSM_FULL_EXECUTION;
    dma_init.LinkedListBaseAddr   = 0U;
    dma_init.LinkedListAddrOffset = 0U;

    /* Source address and block length are configured directly in USART1_DMA_Send() */
    dma_init.SrcAddress        = 0U;
    dma_init.DestAddress       = LL_USART_DMA_GetRegAddr(USART1, LL_USART_DMA_REG_DATA_TRANSMIT);
    dma_init.BlkDataLength     = 0U;

    /* Write configuration to channel registers (channel remains disabled, EN=0) */
    LL_DMA_Init(GPDMA1, LL_DMA_CHANNEL_0, &dma_init);

    /* 3. Enable USART1 TX DMA request permanently */
    LL_USART_EnableDMAReq_TX(USART1);
}



void USART1_DMA_Send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U) {
        return; /* Length 0 with CLLR=0 immediately triggers USEF */
    }

    /* Wait for previous transfer completion (hardware clears EN=0 automatically) */
    while (LL_DMA_IsEnabledChannel(GPDMA1, LL_DMA_CHANNEL_0)) {
        /* Optional: add timeout or USEF/DTEF error check */
    }

    /* Clear status flags from previous transfer */
    LL_DMA_ClearFlag_TC(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_HT(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_DTE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_USE(GPDMA1, LL_DMA_CHANNEL_0);

    /* Reconfigure both addresses and block length before each transfer */
    LL_DMA_ConfigAddresses(GPDMA1, LL_DMA_CHANNEL_0,
                           (uint32_t)data,
                           LL_USART_DMA_GetRegAddr(USART1, LL_USART_DMA_REG_DATA_TRANSMIT));
                           /* (equivalent to (uint32_t)&USART1->TDR) */

    LL_DMA_SetBlkDataLength(GPDMA1, LL_DMA_CHANNEL_0, len);

    /* Start transfer */
    LL_DMA_EnableChannel(GPDMA1, LL_DMA_CHANNEL_0);
}




//// Single-node linked-list mode /////////////

#define TX_NODE_UPDATE_MASK  (LL_DMA_UPDATE_CTR1 | LL_DMA_UPDATE_CTR2 | \
                              LL_DMA_UPDATE_CBR1 | LL_DMA_UPDATE_CSAR | \
                              LL_DMA_UPDATE_CDAR | LL_DMA_UPDATE_CLLR)

/* List node and its configuration template must have static/global lifetime in RAM */
static LL_DMA_LinkNodeTypeDef tx_node;
static LL_DMA_InitNodeTypeDef tx_node_cfg;

void USART1_DMA_TX_1node_Init(void)
{
    LL_DMA_InitLinkedListTypeDef list_cfg;

    /* 1. Enable GPDMA1 clock */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPDMA1);

    /* For reinitialization: safely stop channel if active */
    if (LL_DMA_IsEnabledChannel(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_SuspendChannel(GPDMA1, LL_DMA_CHANNEL_0);
        while (!LL_DMA_IsActiveFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0)) {}
        LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);
        LL_DMA_ClearFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0);
    }

    /* 2. Initialize channel in Linked-List mode */
    LL_DMA_ListStructInit(&list_cfg);
    list_cfg.Priority          = LL_DMA_LOW_PRIORITY_MID_WEIGHT;
    list_cfg.LinkStepMode      = LL_DMA_LSM_FULL_EXECUTION;      /* LSM = 0: execute entire list and stop */
    list_cfg.LinkAllocatedPort = LL_DMA_LINK_ALLOCATED_PORT0;    /* Port used to fetch node from RAM */
    list_cfg.TransferEventMode = LL_DMA_TCEM_BLK_TRANSFER;       /* NOTE: avoid EACH_LLITEM to prevent spurious TC event from LLI_0 */
    LL_DMA_List_Init(GPDMA1, LL_DMA_CHANNEL_0, &list_cfg);

    /* 3. Set 64 KB window base address in CxLBAR (upper 16 bits of tx_node address) */
    LL_DMA_SetLinkedListBaseAddr(GPDMA1, LL_DMA_CHANNEL_0, (uint32_t)&tx_node);

    /* 4. Prepare node template (transfer descriptor) */
    LL_DMA_NodeStructInit(&tx_node_cfg);
    tx_node_cfg.NodeType          = LL_DMA_GPDMA_LINEAR_NODE;    /* Channel 0 is linear */
    tx_node_cfg.UpdateRegisters   = TX_NODE_UPDATE_MASK;         /* Node contains all 6 words (indices 0..5) */

    tx_node_cfg.Direction         = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    tx_node_cfg.Mode              = LL_DMA_NORMAL;
    tx_node_cfg.Request           = LL_GPDMA1_REQUEST_USART1_TX;
    tx_node_cfg.BlkHWRequest      = LL_DMA_HWREQUEST_SINGLEBURST;
    tx_node_cfg.TransferEventMode = LL_DMA_TCEM_BLK_TRANSFER;

    tx_node_cfg.SrcDataWidth      = LL_DMA_SRC_DATAWIDTH_BYTE;
    tx_node_cfg.SrcIncMode        = LL_DMA_SRC_INCREMENT;
    tx_node_cfg.SrcBurstLength    = 1U;
    tx_node_cfg.SrcAllocatedPort  = LL_DMA_SRC_ALLOCATED_PORT0;

    tx_node_cfg.DestDataWidth     = LL_DMA_DEST_DATAWIDTH_BYTE;
    tx_node_cfg.DestIncMode       = LL_DMA_DEST_FIXED;
    tx_node_cfg.DestBurstLength   = 1U;
    tx_node_cfg.DestAllocatedPort = LL_DMA_DEST_ALLOCATED_PORT1;
    tx_node_cfg.DestAddress       = LL_USART_DMA_GetRegAddr(USART1, LL_USART_DMA_REG_DATA_TRANSMIT);

    /* Source address and block length are updated in USART1_DMA_Send() */
    tx_node_cfg.SrcAddress        = 0U;
    tx_node_cfg.BlkDataLength     = 0U;

    /* 5. Enable USART1 TX DMA request permanently */
    LL_USART_EnableDMAReq_TX(USART1);
}

void USART1_DMA_1node_Send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U) {
        return;
    }

    /* Wait for previous transfer completion */
    while (LL_DMA_IsEnabledChannel(GPDMA1, LL_DMA_CHANNEL_0)) {}

    /* Clear status flags (including ULEF for linked-list update errors) */
    LL_DMA_ClearFlag_TC(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_HT(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_DTE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_ULE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_USE(GPDMA1, LL_DMA_CHANNEL_0);

    /* Update node in RAM and mark it as final (CLLR = 0) */
    tx_node_cfg.SrcAddress    = (uint32_t)data;
    tx_node_cfg.BlkDataLength = len;
    LL_DMA_CreateLinkNode(&tx_node_cfg, &tx_node);
    LL_DMA_DisconnectNextLinkNode(&tx_node, LL_DMA_CLLR_OFFSET5);

    /* Reload channel CxLLR to point to tx_node (LLI_0 -> tx_node).
       Note: previous transfer cleared channel CxLLR upon completion,
       so CxLLR must be reconfigured prior to each transfer. */
    LL_DMA_ConfigLinkUpdate(GPDMA1, LL_DMA_CHANNEL_0,
                            TX_NODE_UPDATE_MASK,
                            (uint32_t)&tx_node);

    /* Start transfer (channel BNDT=0 causes GPDMA to fetch tx_node immediately) */
    LL_DMA_EnableChannel(GPDMA1, LL_DMA_CHANNEL_0);
}


//////// Single-node circular linked-list mode ////////////
static uint8_t tx_circ_buf[] = "Circular GPDMA tick!\r\n";


void USART1_DMA_TX_Circular_Start(void)
{
    LL_DMA_InitLinkedListTypeDef list_cfg;
    LL_DMA_InitNodeTypeDef       tx_node_cfg;

    /* 1. Enable GPDMA1 clock */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPDMA1);

    /* For restart: safely stop channel if active */
    if (LL_DMA_IsEnabledChannel(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_SuspendChannel(GPDMA1, LL_DMA_CHANNEL_0);
        while (!LL_DMA_IsActiveFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0)) {}
        LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);
        LL_DMA_ClearFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0);
    }

    /* Clear status flags */
    LL_DMA_ClearFlag_TC(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_HT(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_DTE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_ULE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_USE(GPDMA1, LL_DMA_CHANNEL_0);

    /* 2. Configure channel (list-level parameters) */
    LL_DMA_ListStructInit(&list_cfg);
    list_cfg.Priority          = LL_DMA_LOW_PRIORITY_MID_WEIGHT;
    list_cfg.LinkStepMode      = LL_DMA_LSM_FULL_EXECUTION;      /* Continuous circular execution */
    list_cfg.LinkAllocatedPort = LL_DMA_LINK_ALLOCATED_PORT0;
    list_cfg.TransferEventMode = LL_DMA_TCEM_BLK_TRANSFER;       /* NOTE: do not use LAST_LLITEM in circular mode */
    LL_DMA_List_Init(GPDMA1, LL_DMA_CHANNEL_0, &list_cfg);

    /* 3. Build tx_node (stack-allocated tx_node_cfg is valid here) */
    LL_DMA_NodeStructInit(&tx_node_cfg);
    tx_node_cfg.NodeType          = LL_DMA_GPDMA_LINEAR_NODE;
    tx_node_cfg.UpdateRegisters   = TX_NODE_UPDATE_MASK;

    tx_node_cfg.Direction         = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    tx_node_cfg.Mode              = LL_DMA_NORMAL;
    tx_node_cfg.Request           = LL_GPDMA1_REQUEST_USART1_TX;
    tx_node_cfg.BlkHWRequest      = LL_DMA_HWREQUEST_SINGLEBURST;
    tx_node_cfg.TransferEventMode = LL_DMA_TCEM_BLK_TRANSFER;    /* TC/HT events generated on each iteration */

    tx_node_cfg.SrcDataWidth      = LL_DMA_SRC_DATAWIDTH_BYTE;
    tx_node_cfg.SrcIncMode        = LL_DMA_SRC_INCREMENT;
    tx_node_cfg.SrcBurstLength    = 1U;
    tx_node_cfg.SrcAllocatedPort  = LL_DMA_SRC_ALLOCATED_PORT0;

    tx_node_cfg.DestDataWidth     = LL_DMA_DEST_DATAWIDTH_BYTE;
    tx_node_cfg.DestIncMode       = LL_DMA_DEST_FIXED;
    tx_node_cfg.DestBurstLength   = 1U;
    tx_node_cfg.DestAllocatedPort = LL_DMA_DEST_ALLOCATED_PORT1;

    tx_node_cfg.SrcAddress        = (uint32_t)tx_circ_buf;
    tx_node_cfg.DestAddress       = LL_USART_DMA_GetRegAddr(USART1, LL_USART_DMA_REG_DATA_TRANSMIT);
    tx_node_cfg.BlkDataLength     = sizeof(tx_circ_buf) - 1U;

    LL_DMA_CreateLinkNode(&tx_node_cfg, &tx_node);

    /* 4. Connect node to itself to form a circular list */
    LL_DMA_ConnectLinkNode(&tx_node, LL_DMA_CLLR_OFFSET5,
                           &tx_node, LL_DMA_CLLR_OFFSET5);

    /* 5. Bind channel to initial node (dummy start with BNDT=0 -> tx_node) */
    LL_DMA_SetBlkDataLength(GPDMA1, LL_DMA_CHANNEL_0, 0U);
    LL_DMA_SetLinkedListBaseAddr(GPDMA1, LL_DMA_CHANNEL_0, (uint32_t)&tx_node);
    LL_DMA_ConfigLinkUpdate(GPDMA1, LL_DMA_CHANNEL_0,
                            TX_NODE_UPDATE_MASK,
                            (uint32_t)&tx_node);

    /* 6. Enable USART1 DMA request and start continuous transfer */
    LL_USART_EnableDMAReq_TX(USART1);
    LL_DMA_EnableChannel(GPDMA1, LL_DMA_CHANNEL_0);
}


///// Double-buffer 2-node linked-list mode
/* Two nodes in a single array (guaranteed to reside within the same 64 KB window) */
static LL_DMA_LinkNodeTypeDef tx_nodes[2];

/* Two independent buffers (intentionally different lengths for demonstration) */
static uint8_t tx_buf0[64] = "PING-0 (first buffer)\r\n";
static uint8_t tx_buf1[64] = "PONG-1 (second)\r\n";

static volatile uint32_t msg_counter = 1;

void USART1_DMA_TX_DoubleBuf_Start(void)
{
    LL_DMA_InitLinkedListTypeDef list_cfg;
    LL_DMA_InitNodeTypeDef       node_cfg;

    /* 1. Enable GPDMA1 clock */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPDMA1);

    /* For reinitialization: safely stop channel if active */
    if (LL_DMA_IsEnabledChannel(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_SuspendChannel(GPDMA1, LL_DMA_CHANNEL_0);
        while (!LL_DMA_IsActiveFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0)) {}
        LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);
        LL_DMA_ClearFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0);
    }

    LL_DMA_ClearFlag_TC(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_HT(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_DTE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_ULE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_USE(GPDMA1, LL_DMA_CHANNEL_0);

    /* 2. Configure channel (list-level rules) */
    LL_DMA_ListStructInit(&list_cfg);
    list_cfg.Priority          = LL_DMA_LOW_PRIORITY_MID_WEIGHT;
    list_cfg.LinkStepMode      = LL_DMA_LSM_FULL_EXECUTION;
    list_cfg.LinkAllocatedPort = LL_DMA_LINK_ALLOCATED_PORT0;
    /* Set BLK_TRANSFER for dummy LLI_0 to prevent a spurious interrupt at startup */
    list_cfg.TransferEventMode = LL_DMA_TCEM_BLK_TRANSFER;
    LL_DMA_List_Init(GPDMA1, LL_DMA_CHANNEL_0, &list_cfg);

    /* 3. Populate common node configuration template */
    LL_DMA_NodeStructInit(&node_cfg);
    node_cfg.NodeType          = LL_DMA_GPDMA_LINEAR_NODE;
    node_cfg.UpdateRegisters   = TX_NODE_UPDATE_MASK;
    node_cfg.Direction         = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    node_cfg.Mode              = LL_DMA_NORMAL;
    node_cfg.Request           = LL_GPDMA1_REQUEST_USART1_TX;
    node_cfg.BlkHWRequest      = LL_DMA_HWREQUEST_SINGLEBURST;

    /* Set EACH_LLITEM on nodes: TC interrupt fires after block completion
       and after the next node descriptor is loaded into channel registers */
    node_cfg.TransferEventMode = LL_DMA_TCEM_EACH_LLITEM_TRANSFER;

    node_cfg.SrcDataWidth      = LL_DMA_SRC_DATAWIDTH_BYTE;
    node_cfg.SrcIncMode        = LL_DMA_SRC_INCREMENT;
    node_cfg.SrcBurstLength    = 1U;
    node_cfg.SrcAllocatedPort  = LL_DMA_SRC_ALLOCATED_PORT0;

    node_cfg.DestDataWidth     = LL_DMA_DEST_DATAWIDTH_BYTE;
    node_cfg.DestIncMode       = LL_DMA_DEST_FIXED;
    node_cfg.DestBurstLength   = 1U;
    node_cfg.DestAllocatedPort = LL_DMA_DEST_ALLOCATED_PORT1;
    node_cfg.DestAddress       = LL_USART_DMA_GetRegAddr(USART1, LL_USART_DMA_REG_DATA_TRANSMIT);

    /* 3a. Create Node 0 (bound to buf0) */
    node_cfg.SrcAddress    = (uint32_t)tx_buf0;
    node_cfg.BlkDataLength = strlen((char *)tx_buf0);
    LL_DMA_CreateLinkNode(&node_cfg, &tx_nodes[0]);

    /* 3b. Create Node 1 (bound to buf1) */
    node_cfg.SrcAddress    = (uint32_t)tx_buf1;
    node_cfg.BlkDataLength = strlen((char *)tx_buf1);
    LL_DMA_CreateLinkNode(&node_cfg, &tx_nodes[1]);

    /* 4. Link nodes in a ping-pong ring: node[0] -> node[1] -> node[0] */
    LL_DMA_ConnectLinkNode(&tx_nodes[0], LL_DMA_CLLR_OFFSET5,
                           &tx_nodes[1], LL_DMA_CLLR_OFFSET5);
    LL_DMA_ConnectLinkNode(&tx_nodes[1], LL_DMA_CLLR_OFFSET5,
                           &tx_nodes[0], LL_DMA_CLLR_OFFSET5);

    /* 5. Bind channel to initial node tx_nodes[0] */
    LL_DMA_SetBlkDataLength(GPDMA1, LL_DMA_CHANNEL_0, 0U);
    LL_DMA_SetLinkedListBaseAddr(GPDMA1, LL_DMA_CHANNEL_0, (uint32_t)&tx_nodes[0]);
    LL_DMA_ConfigLinkUpdate(GPDMA1, LL_DMA_CHANNEL_0,
                            TX_NODE_UPDATE_MASK,
                            (uint32_t)&tx_nodes[0]);

    /* 6. Enable TC interrupt (HT is not required) */
    LL_DMA_EnableIT_TC(GPDMA1, LL_DMA_CHANNEL_0);
    NVIC_SetPriority(GPDMA1_Channel0_IRQn, 1);
    NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);

    /* 7. Start transfer */
    LL_USART_EnableDMAReq_TX(USART1);
    LL_DMA_EnableChannel(GPDMA1, LL_DMA_CHANNEL_0);
}

void GPDMA1_Channel0_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TC(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_ClearFlag_TC(GPDMA1, LL_DMA_CHANNEL_0);

        /* Note on LL_DMA_GetLinkedListAddrOffset():
           it returns the offset in 32-bit words (shifted >> 2),
           so shift left by 2 (multiply by 4) to compare against byte addresses */
        uint32_t next_node_offset = LL_DMA_GetLinkedListAddrOffset(GPDMA1, LL_DMA_CHANNEL_0) << 2U;

        if (next_node_offset == ((uint32_t)&tx_nodes[0] & 0xFFFCU)) {
            /* DMA is currently transmitting tx_buf1; tx_buf0 is idle and ready for refill */
            int n = snprintf((char *)tx_buf0, sizeof(tx_buf0),
                             "Packet #%lu in BUF0\r\n", msg_counter++);
            if (n > 0) {
              if ((size_t)n >= sizeof(tx_buf0)) {
                n = sizeof(tx_buf0) - 1U;
              }
              tx_nodes[0].LinkRegisters[2] = (uint32_t)n;
            }
           
        } else {
            /* DMA is currently transmitting tx_buf0; tx_buf1 is idle and ready for refill */
            int n = snprintf((char *)tx_buf1, sizeof(tx_buf1),
                             "Packet #%lu in BUF1 (diff len!)\r\n", msg_counter++);
            if (n > 0) {
               if ((size_t)n >= sizeof(tx_buf1)) {
                 n = sizeof(tx_buf1) - 1U;
               }
               tx_nodes[1].LinkRegisters[2] = (uint32_t)n;
             }
        }
    }
}