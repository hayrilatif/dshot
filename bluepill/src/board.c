#include "stm32f1xx_hal.h"
#include "board.h"

void board_fatal(void)
{
    __disable_irq();
    for (;;) {
        GPIOC->ODR ^= GPIO_PIN_13;
        for (volatile uint32_t i = 0; i < 400000u; i++) {
        }
    }
}

/* HSE 8 MHz × 9 → SYSCLK 72 MHz; USB = PLL / 1.5 = 48 MHz. APB1 36 MHz (timer 72), APB2 72 MHz. */
static void clock_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    RCC_PeriphCLKInitTypeDef per = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        board_fatal();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        board_fatal();
    }

    per.PeriphClockSelection = RCC_PERIPHCLK_USB;
    per.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
    if (HAL_RCCEx_PeriphCLKConfig(&per) != HAL_OK) {
        board_fatal();
    }
}

void board_init(void)
{
    HAL_Init();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_13;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_GPIO_Init(GPIOC, &g);
    clock_init();
}

void board_led(bool on)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static uint32_t tim_clk(uint32_t pclk, bool div1)
{
    return div1 ? pclk : 2u * pclk;
}

uint32_t board_tim_apb1_hz(void)
{
    return tim_clk(HAL_RCC_GetPCLK1Freq(), (RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1);
}

uint32_t board_tim_apb2_hz(void)
{
    return tim_clk(HAL_RCC_GetPCLK2Freq(), (RCC->CFGR & RCC_CFGR_PPRE2) == RCC_CFGR_PPRE2_DIV1);
}
