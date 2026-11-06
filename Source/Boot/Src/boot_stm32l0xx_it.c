#include "main.h"
#include "stm32l0xx_it.h"
#include "boot_udp_dl.h"

void NMI_Handler(void)
{
    while (1)
    {
    }
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void LPUART1_IRQHandler(void)
{
    /* [FW-UPD] 관리서버 UDP 다운로드 수신 (boot_udp_dl.c 내부 HAL 처리 위임) */
    BootUdpDlUartIrq();
}