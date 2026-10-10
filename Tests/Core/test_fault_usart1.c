#include "stm32h7xx.h"
#include "System/fault_usart1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
TestRCC test_rcc;
TestGPIO test_gpio;
TestUSART test_usart;
int main(int argc,char **argv) {
    assert(argc==2);
    test_gpio.MODER=0x13579bdf;test_gpio.AFR[1]=0xaabbccdd;
    SRNOS_FaultUSART1Flush();assert(test_rcc.CR==0); /* No normal-boot takeover. */
    if(!strcmp(argv[1],"clock_timeout")) {
        assert(!SRNOS_FaultUSART1WriteByte('X'));
        test_rcc.CR|=RCC_CR_HSIRDY;
        assert(!SRNOS_FaultUSART1WriteByte('Y'));assert(test_usart.TDR==0);
    } else {
        test_rcc.CR=RCC_CR_HSIRDY;
        if(!strcmp(argv[1],"divided"))test_rcc.CR|=1u<<RCC_CR_HSIDIV_Pos;
        test_usart.ISR=USART_ISR_TEACK|USART_ISR_TXE_TXFNF|USART_ISR_TC;
        if(!strcmp(argv[1],"tx_timeout"))test_usart.ISR&=~USART_ISR_TXE_TXFNF;
        if(!strcmp(argv[1],"tx_timeout"))assert(!SRNOS_FaultUSART1WriteByte('X'));
        else {
            assert(SRNOS_FaultUSART1WriteByte('X'));assert(test_usart.TDR=='X');
            assert(test_usart.BRR==(!strcmp(argv[1],"divided")?278:556));
            assert(test_usart.CR1==(USART_CR1_TE|USART_CR1_UE));
            assert((test_gpio.MODER & ~(3u<<28))==(0x13579bdfu & ~(3u<<28)));
            assert(test_gpio.AFR[1]==((0xaabbccddu & ~(15u<<24))|(4u<<24)));
            if(!strcmp(argv[1],"flush_timeout"))test_usart.ISR&=~USART_ISR_TC;
            SRNOS_FaultUSART1Flush();
        }
        if(strstr(argv[1],"timeout")) {
            test_usart.ISR=USART_ISR_TEACK|USART_ISR_TXE_TXFNF|USART_ISR_TC;
            assert(!SRNOS_FaultUSART1WriteByte('Y')); /* Failure stays bounded. */
        }
    }
    puts(argv[1]);return 0;
}
