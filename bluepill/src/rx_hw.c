#include "stm32f1xx_hal.h"
#include "board.h"
#include "rx_hw.h"

#if BOARD_HAS_RX

#define CAP_LEN   256u /* 2'nin kuvveti */
#define QUEUE_LEN 16u  /* 2'nin kuvveti */
/* Reset modunda sayaç tetikleyici kenardan ~2 tick sonra sıfırlanır; yakalamalar bu kadar kısa çıkar */
#define RESET_LATENCY_TICKS 2u

static volatile uint16_t cap_period[CAP_LEN];
static volatile uint16_t cap_high[CAP_LEN];
static uint16_t start_p, start_h;
static dshot_rx_burst_t queue[QUEUE_LEN];
static volatile uint16_t q_head, q_tail;
static volatile uint32_t dropped;
static uint32_t tick_hz;

static void dma_setup(DMA_Channel_TypeDef *c, volatile uint32_t *periph, volatile uint16_t *mem)
{
    c->CCR = 0;
    c->CPAR = (uint32_t)periph;
    c->CMAR = (uint32_t)mem;
    c->CNDTR = CAP_LEN;
    /* çevre → bellek, dairesel, 16 bit */
    c->CCR = (2u << DMA_CCR_PL_Pos) | (1u << DMA_CCR_MSIZE_Pos) | (1u << DMA_CCR_PSIZE_Pos) | DMA_CCR_MINC |
             DMA_CCR_CIRC;
}

void rx_hw_set_inverted(bool inverted)
{
    uint32_t ccer = TIM4->CCER & ~(TIM_CCER_CC1P | TIM_CCER_CC1NP | TIM_CCER_CC2P | TIM_CCER_CC2NP);
    /* normal: CH1 yükselen (başlangıç), CH2 düşen; ters: tam tersi */
    ccer |= inverted ? TIM_CCER_CC1P : TIM_CCER_CC2P;
    TIM4->CCER = ccer;
}

void rx_hw_init(bool inverted)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* PB6 = TIM4_CH1 (remap yok), 5 V toleranslı giriş, pull yok */
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_6;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    tick_hz = board_tim_apb1_hz();
    TIM4->CR1 = 0;
    TIM4->PSC = 0;
    TIM4->ARR = tick_hz / 100000u; /* 10 µs sessizlik = burst sonu */
    /* IC1 ← TI1, IC2 ← TI1, küçük giriş filtresi */
    TIM4->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_1 | (1u << TIM_CCMR1_IC1F_Pos) | (1u << TIM_CCMR1_IC2F_Pos);
    TIM4->CCER = 0;
    rx_hw_set_inverted(inverted);
    TIM4->CCER |= TIM_CCER_CC1E | TIM_CCER_CC2E;
    /* TI1FP1 tetikleyici, reset modu: her başlangıç kenarında sayaç sıfırlanır */
    TIM4->SMCR = (5u << TIM_SMCR_TS_Pos) | (4u << TIM_SMCR_SMS_Pos);
    TIM4->CR1 = TIM_CR1_URS; /* update yalnız taşmada */

    /* DMA1 Channel1 = TIM4_CH1, Channel4 = TIM4_CH2 */
    DMA1->IFCR = DMA_IFCR_CGIF1 | DMA_IFCR_CGIF4;
    dma_setup(DMA1_Channel1, &TIM4->CCR1, cap_period);
    dma_setup(DMA1_Channel4, &TIM4->CCR2, cap_high);
    DMA1_Channel1->CCR |= DMA_CCR_EN;
    DMA1_Channel4->CCR |= DMA_CCR_EN;

    TIM4->DIER = TIM_DIER_CC1DE | TIM_DIER_CC2DE | TIM_DIER_TIE;
    TIM4->EGR = TIM_EGR_UG;
    TIM4->SR = 0;
    HAL_NVIC_SetPriority(TIM4_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
    TIM4->CR1 |= TIM_CR1_CEN;
}

static void end_burst(void)
{
    const uint16_t mask = CAP_LEN - 1u;
    uint16_t pos_p = (uint16_t)(CAP_LEN - DMA1_Channel1->CNDTR) & mask;
    uint16_t pos_h = (uint16_t)(CAP_LEN - DMA1_Channel4->CNDTR) & mask;
    uint16_t np = (uint16_t)(pos_p - start_p) & mask;
    uint16_t nh = (uint16_t)(pos_h - start_h) & mask;
    uint16_t next = (uint16_t)((q_head + 1u) & (QUEUE_LEN - 1u));
    if (next == q_tail) {
        dropped++;
    } else {
        dshot_rx_burst_t *b = &queue[q_head];
        b->n = np == nh ? (np > 255u ? 255u : (uint8_t)np) : 0u;
        b->idle_high = (GPIOB->IDR & GPIO_PIN_6) != 0u;
        if (b->n == DSHOT_BURST_PULSES) {
            for (unsigned i = 0; i < DSHOT_BURST_PULSES; i++) {
                b->periods[i] = (uint16_t)(cap_period[(start_p + i) & mask] + RESET_LATENCY_TICKS);
                b->highs[i] = (uint16_t)(cap_high[(start_h + i) & mask] + RESET_LATENCY_TICKS);
            }
        }
        __DMB();
        q_head = next;
    }
    start_p = pos_p;
    start_h = pos_h;
}

void TIM4_IRQHandler(void)
{
    uint32_t sr = TIM4->SR;
    uint32_t dier = TIM4->DIER;
    if ((dier & TIM_DIER_TIE) && (sr & TIM_SR_TIF)) {
        /* Burst başladı: sessizlik zaman aşımını izle */
        TIM4->SR = ~(TIM_SR_TIF | TIM_SR_UIF);
        TIM4->DIER = (dier & ~TIM_DIER_TIE) | TIM_DIER_UIE;
    } else if ((dier & TIM_DIER_UIE) && (sr & TIM_SR_UIF)) {
        /* 10 µs kenar yok: burst bitti */
        TIM4->SR = ~(TIM_SR_UIF | TIM_SR_TIF);
        TIM4->DIER = (dier & ~TIM_DIER_UIE) | TIM_DIER_TIE;
        end_burst();
    }
}

bool rx_hw_pop(dshot_rx_burst_t *out)
{
    if (q_tail == q_head) {
        return false;
    }
    *out = queue[q_tail];
    __DMB();
    q_tail = (uint16_t)((q_tail + 1u) & (QUEUE_LEN - 1u));
    return true;
}

uint32_t rx_hw_tick_hz(void)
{
    return tick_hz;
}

uint32_t rx_hw_dropped(void)
{
    return dropped;
}

#endif
