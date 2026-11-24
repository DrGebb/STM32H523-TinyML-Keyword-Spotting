#include "i2s_LL.h"
#include "stm32h523xx.h"

/* Buffers A and B (4-byte aligned for DMA access) */
__IO int16_t buffer_dma_A[DMA_BUF_LEN] __attribute__((aligned(4)));
__IO int16_t buffer_dma_B[DMA_BUF_LEN] __attribute__((aligned(4)));

/* File-scope node descriptors shared between I2S_Init and IRQHandler */
static LL_DMA_LinkNodeTypeDef i2s_dma_nodes[2] __attribute__((aligned(4)));

#define I2S_NODE_UPDATE_MASK  (LL_DMA_UPDATE_CTR1 | LL_DMA_UPDATE_CTR2 | \
                               LL_DMA_UPDATE_CBR1 | LL_DMA_UPDATE_CSAR | \
                               LL_DMA_UPDATE_CDAR | LL_DMA_UPDATE_CLLR)

void I2S_Init(void)
{
    LL_I2S_InitTypeDef  I2S_InitStruct  = {0};
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 1. Configure I2S1 (SPI1) clock source and GPIO pins */
    LL_RCC_SetSPIClockSource(LL_RCC_SPI1_CLKSOURCE_PLL2P);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);

    /** I2S1 GPIO Configuration: PA4 -> WS, PA5 -> CK, PA6 -> SDI */
    GPIO_InitStruct.Pin        = LL_GPIO_PIN_4 | LL_GPIO_PIN_5 | LL_GPIO_PIN_6;
    GPIO_InitStruct.Mode       = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_MEDIUM;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull       = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Alternate  = LL_GPIO_AF_5;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    I2S_InitStruct.Mode          = LL_I2S_MODE_MASTER_RX;
    I2S_InitStruct.Standard      = LL_I2S_STANDARD_PHILIPS;
    I2S_InitStruct.DataFormat    = LL_I2S_DATAFORMAT_16B_EXTENDED;
    I2S_InitStruct.MCLKOutput    = LL_I2S_MCLK_OUTPUT_DISABLE;
    I2S_InitStruct.AudioFreq     = LL_I2S_AUDIOFREQ_16K;
    I2S_InitStruct.ClockPolarity = LL_I2S_POLARITY_LOW;
    LL_I2S_Init(SPI1, &I2S_InitStruct);

    LL_I2S_SetPrescalerLinear(SPI1, 25);                            /* I2SDIV = 25 */
    LL_I2S_SetPrescalerParity(SPI1, LL_I2S_PRESCALER_PARITY_EVEN);  /* ODD = 0 */

    /* Enable SPI1 RX DMA request */
    LL_I2S_EnableDMAReq_RX(SPI1);

    /* 2. Enable clock and reset GPDMA1 Channel 0 */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPDMA1);

    if (LL_DMA_IsEnabledChannel(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_SuspendChannel(GPDMA1, LL_DMA_CHANNEL_0);
        while (!LL_DMA_IsActiveFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0)) { __NOP(); }
        LL_DMA_ResetChannel(GPDMA1, LL_DMA_CHANNEL_0);
    }

    LL_DMA_ClearFlag_TC(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_HT(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_DTE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_ULE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_USE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_SUSP(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_ClearFlag_TO(GPDMA1, LL_DMA_CHANNEL_0);

    /* 3. Configure two linked-list nodes (Node 0 -> buffer_dma_A, Node 1 -> buffer_dma_B) */
    LL_DMA_InitNodeTypeDef node_cfg;
    LL_DMA_NodeStructInit(&node_cfg);

    node_cfg.NodeType          = LL_DMA_GPDMA_LINEAR_NODE;
    node_cfg.Request           = LL_GPDMA1_REQUEST_SPI1_RX;
    node_cfg.BlkHWRequest      = LL_DMA_HWREQUEST_SINGLEBURST;
    node_cfg.Direction         = LL_DMA_DIRECTION_PERIPH_TO_MEMORY;
    node_cfg.Mode              = LL_DMA_NORMAL;

    node_cfg.SrcAddress        = (uint32_t)&(SPI1->RXDR);
    node_cfg.SrcDataWidth      = LL_DMA_SRC_DATAWIDTH_HALFWORD;
    node_cfg.SrcIncMode        = LL_DMA_SRC_FIXED;
    node_cfg.SrcBurstLength    = 1U;
    node_cfg.SrcByteExchange   = LL_DMA_SRC_BYTE_PRESERVE;
    node_cfg.SrcAllocatedPort  = LL_DMA_SRC_ALLOCATED_PORT0;

    node_cfg.DestAddress       = (uint32_t)buffer_dma_A;
    node_cfg.DestDataWidth     = LL_DMA_DEST_DATAWIDTH_HALFWORD;
    node_cfg.DestIncMode       = LL_DMA_DEST_INCREMENT;
    node_cfg.DestBurstLength   = 1U;
    node_cfg.DestByteExchange  = LL_DMA_DEST_BYTE_PRESERVE;
    node_cfg.DestHWordExchange = LL_DMA_DEST_HALFWORD_PRESERVE;
    node_cfg.DestAllocatedPort = LL_DMA_DEST_ALLOCATED_PORT1;

    node_cfg.DataAlignment     = LL_DMA_DATA_ALIGN_ZEROPADD;
    node_cfg.BlkDataLength     = DMA_BUF_LEN * sizeof(int16_t); /* in bytes */

    node_cfg.TriggerMode       = LL_DMA_TRIGM_BLK_TRANSFER;
    node_cfg.TriggerPolarity   = LL_DMA_TRIG_POLARITY_MASKED;
    node_cfg.TriggerSelection  = 0U;

    /* TC triggers immediately after block completion and next node loading */
    node_cfg.TransferEventMode = LL_DMA_TCEM_EACH_LLITEM_TRANSFER;
    node_cfg.UpdateRegisters   = I2S_NODE_UPDATE_MASK;

    /* Node 0 -> buffer_dma_A */
    LL_DMA_CreateLinkNode(&node_cfg, &i2s_dma_nodes[0]);

    /* Node 1 -> buffer_dma_B */
    node_cfg.DestAddress = (uint32_t)buffer_dma_B;
    LL_DMA_CreateLinkNode(&node_cfg, &i2s_dma_nodes[1]);

    /* Circular link: Node 0 -> Node 1 -> Node 0 */
    LL_DMA_ConnectLinkNode(&i2s_dma_nodes[0], LL_DMA_CLLR_OFFSET5,
                           &i2s_dma_nodes[1], LL_DMA_CLLR_OFFSET5);
    LL_DMA_ConnectLinkNode(&i2s_dma_nodes[1], LL_DMA_CLLR_OFFSET5,
                           &i2s_dma_nodes[0], LL_DMA_CLLR_OFFSET5);

    /* 4. Initialize Channel 0 in Linked-List mode */
    LL_DMA_InitLinkedListTypeDef list_init;
    LL_DMA_ListStructInit(&list_init);
    list_init.Priority          = LL_DMA_HIGH_PRIORITY;
    list_init.LinkStepMode      = LL_DMA_LSM_FULL_EXECUTION;
    list_init.LinkAllocatedPort = LL_DMA_LINK_ALLOCATED_PORT1;
    /* NOTE: Use BLK_TRANSFER for LLI_0 to prevent a spurious TC interrupt at startup */
    list_init.TransferEventMode = LL_DMA_TCEM_BLK_TRANSFER;
    LL_DMA_List_Init(GPDMA1, LL_DMA_CHANNEL_0, &list_init);

    LL_DMA_SetBlkDataLength(GPDMA1, LL_DMA_CHANNEL_0, 0U);
    LL_DMA_SetLinkedListBaseAddr(GPDMA1, LL_DMA_CHANNEL_0, (uint32_t)&i2s_dma_nodes[0]);
    LL_DMA_ConfigLinkUpdate(GPDMA1, LL_DMA_CHANNEL_0,
                            I2S_NODE_UPDATE_MASK,
                            (uint32_t)&i2s_dma_nodes[0]);

    /* 5. Interrupt configuration: priority >= 5 required due to osThreadFlagsSet in ISR */
    LL_DMA_EnableIT_TC(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_EnableIT_DTE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_EnableIT_ULE(GPDMA1, LL_DMA_CHANNEL_0);
    LL_DMA_EnableIT_USE(GPDMA1, LL_DMA_CHANNEL_0);

    NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0U);
    NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);

    /* 6. Startup sequence: enable GPDMA first, then I2S1 */
    LL_DMA_EnableChannel(GPDMA1, LL_DMA_CHANNEL_0);
    LL_I2S_Enable(SPI1);
    LL_SPI_StartMasterTransfer(SPI1);
}

/* Verify that GPDMA1_Channel0_IRQHandler is defined in startup/stm32h5xx_it.c */
void GPDMA1_Channel0_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TC(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_ClearFlag_TC(GPDMA1, LL_DMA_CHANNEL_0);

        /* Determine active target buffer via the next-node pointer in CLLR */
        uint32_t next_node_offset = LL_DMA_GetLinkedListAddrOffset(GPDMA1, LL_DMA_CHANNEL_0) << 2U;

        if (next_node_offset == ((uint32_t)&i2s_dma_nodes[0] & 0xFFFCU)) {
            /* Channel switched to Node 1 (writing to buffer B); buffer A is ready */
            osThreadFlagsSet(g_mic_tid, FLAG_BUF_A);
        } else {
            /* Channel switched to Node 0 (writing to buffer A); buffer B is ready */
            osThreadFlagsSet(g_mic_tid, FLAG_BUF_B);
        }
    }

    /* Handle DMA transfer error flags for diagnostics */
    if (LL_DMA_IsActiveFlag_DTE(GPDMA1, LL_DMA_CHANNEL_0) ||
        LL_DMA_IsActiveFlag_ULE(GPDMA1, LL_DMA_CHANNEL_0) ||
        LL_DMA_IsActiveFlag_USE(GPDMA1, LL_DMA_CHANNEL_0)) {
        LL_DMA_ClearFlag_DTE(GPDMA1, LL_DMA_CHANNEL_0);
        LL_DMA_ClearFlag_ULE(GPDMA1, LL_DMA_CHANNEL_0);
        LL_DMA_ClearFlag_USE(GPDMA1, LL_DMA_CHANNEL_0);
        /* Breakpoint or error counter placement */
    }
}