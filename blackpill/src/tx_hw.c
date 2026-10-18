#include "stm32f4xx_hal.h"
#include "board.h"
#include "dshot_tx_buf.h"
#include "tx_hw.h"

static uint16_t dma_buf[DSHOT_TX_BUF_LEN];
static dshot_tx_timing_t timing;
static dshot_txctl_t *ctl;

void tx_hw_apply(const dshot_txctl_t *c)
{
    timing = dshot_tx_timing(board_tim_apb2_hz(), c->speed);
    TIM1->ARR = (uint32_t)timing.period - 1u;
    TIM2->ARR = 1000000u / c->rate_hz - 1u;
}

void tx_hw_init(dshot_txctl_t *c)
{
    ctl = c;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* Timer çalışana kadar hat yüzmesin: önce GPIO low */
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOA, &g);

    /* TIM1: PWM mode 1, CCR1 preload; her update'te DMA bir sonraki bit süresini CCR1'e yazar */
    TIM1->CR1 = TIM_CR1_ARPE;
    TIM1->PSC = 0;
    TIM1->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM1->CCER = TIM_CCER_CC1E;
    TIM1->CCR1 = 0;
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->DIER = TIM_DIER_UDE;

    /* DMA2 Stream5 Channel6 = TIM1_UP, bellek → CCR1, 16 bit */
    DMA2_Stream5->CR = 0;
    while (DMA2_Stream5->CR & DMA_SxCR_EN) {
    }
    DMA2_Stream5->PAR = (uint32_t)&TIM1->CCR1;
    DMA2_Stream5->M0AR = (uint32_t)dma_buf;
    DMA2_Stream5->FCR = 0;
    DMA2_Stream5->CR = (6u << DMA_SxCR_CHSEL_Pos) | (2u << DMA_SxCR_PL_Pos) | (1u << DMA_SxCR_MSIZE_Pos) |
                       (1u << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | (1u << DMA_SxCR_DIR_Pos);

    /* TIM2: 1 MHz tabanlı frame zamanlayıcı */
    TIM2->CR1 = TIM_CR1_ARPE;
    TIM2->PSC = board_tim_apb1_hz() / 1000000u - 1u;
    tx_hw_apply(c);
    TIM1->EGR = TIM_EGR_UG;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM1->CR1 |= TIM_CR1_CEN;

    /* CCR1=0 iken PWM çıkışı low; pini timer'a devret */
    g.Mode = GPIO_MODE_AF_PP;
    g.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &g);

    TIM2->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM2_IRQHandler(void)
{
    TIM2->SR = ~TIM_SR_UIF;
    if (DMA2_Stream5->CR & DMA_SxCR_EN) {
        return; /* önceki frame hâlâ gidiyor (RATE sınırı bunu engeller) */
    }
    uint16_t frame;
    if (!dshot_txctl_next_frame(ctl, &frame)) {
        return;
    }
    dshot_tx_fill(frame, &timing, dma_buf);
    /* Önceki frame'den askıda kalan update DMA isteğini düşür; yoksa stream açılır açılmaz
       iki transfer olup ilk bitin üstüne yazılabiliyor (frame 15 pals çıkıyor) */
    TIM1->DIER = 0;
    DMA2->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5;
    DMA2_Stream5->NDTR = DSHOT_TX_BUF_LEN;
    DMA2_Stream5->CR |= DMA_SxCR_EN;
    TIM1->DIER = TIM_DIER_UDE;
}
