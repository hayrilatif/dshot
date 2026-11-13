#include "stm32f1xx_hal.h"
#include "board.h"
#include "dshot_tx_buf.h"
#include "tx_hw.h"

#if BOARD_HAS_TX

#define REPLY_QUEUE_LEN 8u /* 2'nin kuvveti */
#define CAP_EDGES       (DSHOT_REPLY_MAX_EDGES / 2u) /* kenar yönü başına */
#define TRIG_PIN        GPIO_PIN_0 /* PB0 */
/* PA8'in GPIOA->CRH alanı: AF push-pull 50 MHz / pull'lu giriş (yön ODR8 ile) */
#define PA8_AF_PP   0xBu
#define PA8_IN_PULL 0x8u

static uint16_t dma_buf[DSHOT_TX_BUF_LEN];
static dshot_tx_timing_t timing;
static dshot_txctl_t *ctl;
static volatile bool bidir;
static volatile bool capturing;
static uint32_t tim_hz;

static volatile uint16_t cap_fall[CAP_EDGES];
static volatile uint16_t cap_rise[CAP_EDGES];
static dshot_reply_t replies[REPLY_QUEUE_LEN];
static volatile uint16_t r_head, r_tail;

static void pa8_mode(uint32_t mode)
{
    GPIOA->CRH = (GPIOA->CRH & ~0xFu) | mode;
}

static void set_pull(bool up)
{
    GPIOA->BSRR = up ? GPIO_PIN_8 : (uint32_t)GPIO_PIN_8 << 16u;
}

/* TIM1 CH1'i PWM çıkışına al. bidir: aktif low (boşta high). Pin, çıkış seviyesi hazır olunca timer'a verilir. */
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
    pa8_mode(PA8_AF_PP);
}

static void cap_dma(DMA_Channel_TypeDef *c, volatile uint32_t *periph, volatile uint16_t *mem)
{
    c->CCR = 0;
    c->CPAR = (uint32_t)periph;
    c->CMAR = (uint32_t)mem;
    c->CNDTR = CAP_EDGES;
    /* çevre → bellek, 16 bit */
    c->CCR = (2u << DMA_CCR_PL_Pos) | (1u << DMA_CCR_MSIZE_Pos) | (1u << DMA_CCR_PSIZE_Pos) | DMA_CCR_MINC |
             DMA_CCR_EN;
}

/* Frame bitti: PA8'i (pull-up'lı) girişe al, ESC cevabının kenarlarını zaman damgasıyla topla.
   F103 iki kenarı tek kanalda yakalayamaz: IC1 ← TI1 düşen (DMA1 Ch2), IC2 ← TI1 yükselen (DMA1 Ch3). */
static void start_capture(void)
{
    pa8_mode(PA8_IN_PULL);
    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM1->DIER = 0;
    TIM1->CCER = 0;
    TIM1->CCMR1 = TIM_CCMR1_CC1S_0 | (2u << TIM_CCMR1_IC1F_Pos) | TIM_CCMR1_CC2S_1 | (2u << TIM_CCMR1_IC2F_Pos);
    TIM1->ARR = 0xFFFFu;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC2E;

    DMA1->IFCR = DMA_IFCR_CGIF2 | DMA_IFCR_CGIF3;
    cap_dma(DMA1_Channel2, &TIM1->CCR1, cap_fall);
    cap_dma(DMA1_Channel3, &TIM1->CCR2, cap_rise);
    TIM1->SR = 0;
    TIM1->DIER = TIM_DIER_CC1DE | TIM_DIER_CC2DE;
    capturing = true;
    TIM1->CR1 |= TIM_CR1_CEN;
}

static void finish_capture(void)
{
    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM1->DIER = 0;
    unsigned nf = CAP_EDGES - DMA1_Channel2->CNDTR;
    unsigned nr = CAP_EDGES - DMA1_Channel3->CNDTR;
    DMA1_Channel2->CCR = 0;
    DMA1_Channel3->CCR = 0;
    capturing = false;

    uint16_t next = (uint16_t)((r_head + 1u) & (REPLY_QUEUE_LEN - 1u));
    if (next != r_tail) {
        /* DMA durdu, tamponlar artık değişmez */
        dshot_reply_merge(&replies[r_head], (const uint16_t *)cap_fall, nf, (const uint16_t *)cap_rise, nr);
        __DMB();
        r_head = next;
    }
    output_mode();
}

/* TIM2 16 bit: düşük frame hızlarında daha kaba zaman tabanı */
static void frame_timer_set(uint32_t rate_hz)
{
    uint32_t base = rate_hz >= 20u ? 1000000u : 10000u;
    TIM2->PSC = board_tim_apb1_hz() / base - 1u;
    TIM2->ARR = base / rate_hz - 1u;
}

void tx_hw_apply(const dshot_txctl_t *c)
{
    timing = dshot_tx_timing(tim_hz, c->speed);
    frame_timer_set(c->rate_hz);
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
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* Timer çalışana kadar hat yüzmesin: önce GPIO low */
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOA, &g);

    /* Tetik çıkışı: host motor komutu veren ilk frame'in başında high, bir frame periyodu sürer */
    g.Pin = TRIG_PIN;
    HAL_GPIO_WritePin(GPIOB, TRIG_PIN, GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOB, &g);

    TIM1->CR1 = TIM_CR1_ARPE;
    TIM1->PSC = 0;
    TIM1->BDTR = TIM_BDTR_MOE;

    /* DMA1 Channel5 = TIM1_UP, bellek → CCR1, 16 bit; TC kesmesi bidir'de cevap yakalamayı başlatır.
       RX ve cevap yakalama da DMA1'de: eşit öncelikte düşük numaralı kanal kazanır, geciken CCR1 yazımı
       ESC'ye bozuk frame demek. Bu yüzden en yüksek öncelik. */
    DMA1_Channel5->CCR = 0;
    DMA1_Channel5->CPAR = (uint32_t)&TIM1->CCR1;
    DMA1_Channel5->CMAR = (uint32_t)dma_buf;
    DMA1_Channel5->CCR = (3u << DMA_CCR_PL_Pos) | (1u << DMA_CCR_MSIZE_Pos) | (1u << DMA_CCR_PSIZE_Pos) |
                         DMA_CCR_MINC | DMA_CCR_DIR;
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

    /* TIM2: frame zamanlayıcı */
    TIM2->CR1 = TIM_CR1_ARPE;
    bidir = c->bidir;
    set_pull(bidir);
    tx_hw_apply(c); /* CCR1=0 iken PWM çıkışı boşta seviyede; pini timer'a devreder */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;

    TIM2->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM2_IRQHandler(void)
{
    TIM2->SR = ~TIM_SR_UIF;
    GPIOB->BSRR = (uint32_t)TRIG_PIN << 16u;
    if (capturing) {
        finish_capture();
    }
    /* F103'te kanal bitince EN kendiliğinden düşmez: bitip bitmediğini CNDTR söyler */
    if ((DMA1_Channel5->CCR & DMA_CCR_EN) && DMA1_Channel5->CNDTR != 0u) {
        return; /* önceki frame hâlâ gidiyor (RATE sınırı bunu engeller) */
    }
    uint16_t frame;
    if (!dshot_txctl_next_frame(ctl, &frame)) {
        return;
    }
    dshot_tx_fill(frame, &timing, dma_buf);
    bool trig = dshot_txctl_take_trigger(ctl);
    /* Önceki frame'den askıda kalan update DMA isteğini düşür; yoksa kanal açılır açılmaz
       iki transfer olup ilk bitin üstüne yazılabiliyor (frame 15 pals çıkıyor) */
    TIM1->DIER = 0;
    DMA1_Channel5->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF5;
    DMA1_Channel5->CNDTR = DSHOT_TX_BUF_LEN;
    DMA1_Channel5->CCR = (DMA1_Channel5->CCR & ~DMA_CCR_TCIE) | (bidir ? DMA_CCR_TCIE : 0u) | DMA_CCR_EN;
    if (trig) {
        GPIOB->BSRR = TRIG_PIN;
    }
    TIM1->DIER = TIM_DIER_UDE;
}

/* Son DMA transferi = son bitin bittiği update: hat artık boşta */
void DMA1_Channel5_IRQHandler(void)
{
    DMA1->IFCR = DMA_IFCR_CTCIF5;
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

#endif
