#include <rtthread.h>
#include "hal_base.h"

static void gpio_toggle_entry(void *parameter)
{
    /* 把 GPIO3_C4 配成输出口，具体 API 名称以 hal_gpio.h 中为准 */
    HAL_GPIO_SetPinDirection(GPIO3, GPIO_PIN_C4, GPIO_OUT);

    while (1)
    {
        HAL_GPIO_SetPinLevel(GPIO3, GPIO_PIN_C4, GPIO_HIGH);
        rt_thread_mdelay(500);
        HAL_GPIO_SetPinLevel(GPIO3, GPIO_PIN_C4, GPIO_LOW);
        rt_thread_mdelay(500);
    }
}

int gpio_toggle_init(void)
{
    rt_thread_t tid = rt_thread_create("gtoggle",
                                       gpio_toggle_entry,
                                       RT_NULL,
                                       1024,
                                       20,
                                       10);
    if (tid)
        rt_thread_startup(tid);

    return 0;
}
/* 把初始化挂到应用初始化阶段 */
INIT_APP_EXPORT(gpio_toggle_init);
