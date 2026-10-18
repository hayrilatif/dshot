#include "stm32f4xx_hal.h"
#include "board.h"
#include "dshot_tx_buf.h"
#include "tx_hw.h"

#define REPLY_QUEUE_LEN 8u /* 2'nin kuvveti */

static uint16_t dma_buf[DSHOT_TX_BUF_LEN];
static dshot_tx_timing_t timing;
static dshot_txctl_t *ctl;
static volatile bool bidir;
static volatile bool capturing;
static uint32_t tim_hz;

static volatile uint16_t cap_buf[DSHOT_REPLY_MAX_EDGES];
static dshot_reply_t replies[REPLY_QUEUE_LEN];
static volatile uint16_t r_head, r_tail;

static void set_pull(bool up)
{
    uint32_t p = GPIOA->PUPDR & ~GPIO_PUPDR_PUPD8;
    GPIOA->PUPDR = p | (up ? GPIO_PUPDR_PUPD8_0 : 0u);
}

/* TIM1 CH1'i PWM çıkışına al. bidir: aktif low (boşta high). */
static void output_mode(void)
{
    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM1->DIER = 0;
    TIM1->CCER = 0;
    TIM1->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM1->CCR1 = 0;
    TIM1->ARR = (uint32_t)timing.period - 1u;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CCER = TIM_CCER_CC1E | (bidir ? TIM_CCER_CC1P : 0u);
    TIM1->CR1 |= TIM_CR1_CEN;
}

/* Frame bitti: PA8'i girişe al, ESC cevabının her iki kenarını zaman damgasıyla topla */
static void start_capture(void)
{
    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM1->DIER = 0;
    TIM1->CCER = 0;
    TIM1->CCMR1 = TIM_CCMR1_CC1S_0 | (2u << TIM_CCMR1_IC1F_Pos);
    TIM1->ARR = 0xFFFFu;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP;

    DMA2_Stream1->CR = 0;
    while (DMA2_Stream1->CR & DMA_SxCR_EN) {
    }
    DMA2->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CFEIF1;
    DMA2_Stream1->PAR = (uint32_t)&TIM1->CCR1;
    DMA2_Stream1->M0AR = (uint32_t)cap_buf;
    DMA2_Stream1->NDTR = DSHOT_REPLY_MAX_EDGES;
    DMA2_Stream1->FCR = 0;
    /* Channel 6 = TIM1_CH1, çevre → bellek, 16 bit */
    DMA2_Stream1->CR = (6u << DMA_SxCR_CHSEL_Pos) | (2u << DMA_SxCR_PL_Pos) | (1u << DMA_SxCR_MSIZE_Pos) |
                       (1u << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | DMA_SxCR_EN;
    TIM1->SR = 0;
    TIM1->DIER = TIM_DIER_CC1DE;
    capturing = true;
    TIM1->CR1 |= TIM_CR1_CEN;
}

static void finish_capture(void)
{
    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM1->DIER = 0;
    uint16_t n = (uint16_t)(DSHOT_REPLY_MAX_EDGES - DMA2_Stream1->NDTR);
    DMA2_Stream1->CR = 0;
    capturing = false;

    uint16_t next = (uint16_t)((r_head + 1u) & (REPLY_QUEUE_LEN - 1u));
    if (next != r_tail) {
        dshot_reply_t *r = &replies[r_head];
        r->count = (uint8_t)n;
        for (unsigned i = 0; i < n; i++) {
            r->edges[i] = cap_buf[i];
        }
        __DMB();
        r_head = next;
    }
    output_mode();
}

void tx_hw_apply(const dshot_txctl_t *c)
{
    timing = dshot_tx_timing(tim_hz, c->speed);
    TIM2->ARR = 1000000u / c->rate_hz - 1u;
    if (bidir != c->bidir) {
        bidir = c->bidir;
        set_pull(bidir); /* bidir'de hat boşta high, ESC cevap verirken de pull-up tutar */
    }
    if (!capturing) {
        output_mode();
    }
}

void tx_hw_init(dshot_txctl_t *c)
{
    ctl = c;
    tim_hz = board_tim_apb2_hz();
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

    TIM1->CR1 = TIM_CR1_ARPE;
    TIM1->PSC = 0;
    TIM1->BDTR = TIM_BDTR_MOE;

    /* DMA2 Stream5 Channel6 = TIM1_UP, bellek → CCR1, 16 bit; TC kesmesi bidir'de cevap yakalamayı başlatır */
    DMA2_Stream5->CR = 0;
    while (DMA2_Stream5->CR & DMA_SxCR_EN) {
    }
    DMA2_Stream5->PAR = (uint32_t)&TIM1->CCR1;
    DMA2_Stream5->M0AR = (uint32_t)dma_buf;
    DMA2_Stream5->FCR = 0;
    DMA2_Stream5->CR = (6u << DMA_SxCR_CHSEL_Pos) | (2u << DMA_SxCR_PL_Pos) | (1u << DMA_SxCR_MSIZE_Pos) |
                       (1u << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | (1u << DMA_SxCR_DIR_Pos);
    HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);

    /* TIM2: 1 MHz tabanlı frame zamanlayıcı */
    TIM2->CR1 = TIM_CR1_ARPE;
    TIM2->PSC = board_tim_apb1_hz() / 1000000u - 1u;
    bidir = c->bidir;
    tx_hw_apply(c);
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;

    /* CCR1=0 iken PWM çıkışı boşta seviyede; pini timer'a devret */
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = bidir ? GPIO_PULLUP : GPIO_NOPULL;
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
    if (capturing) {
        finish_capture();
    }
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
    DMA2_Stream5->CR = (DMA2_Stream5->CR & ~DMA_SxCR_TCIE) | (bidir ? DMA_SxCR_TCIE : 0u) | DMA_SxCR_EN;
    TIM1->DIER = TIM_DIER_UDE;
}

/* Son DMA transferi = son bitin bittiği update: hat artık boşta */
void DMA2_Stream5_IRQHandler(void)
{
    DMA2->HIFCR = DMA_HIFCR_CTCIF5;
    if (bidir) {
        start_capture();
    }
}

bool tx_hw_pop_reply(dshot_reply_t *out)
{
    if (r_tail == r_head) {
        return false;
    }
    *out = replies[r_tail];
    __DMB();
    r_tail = (uint16_t)((r_tail + 1u) & (REPLY_QUEUE_LEN - 1u));
    return true;
}

uint32_t tx_hw_tick_hz(void)
{
    return tim_hz;
}
