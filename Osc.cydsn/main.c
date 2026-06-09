#include "project.h"
#include <stdlib.h>
#include <stdio.h>
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DEFINES
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
#define USB_PACKET_SIZE        64

#define BYTES_PER_SAMPLE       (USB_PACKET_SIZE - 1)
#define BYTES_PER_BURST        1
#define REQUEST_PER_BURST      1
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GLOBALS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
uint8 usbPacketSignal1[USB_PACKET_SIZE];
volatile uint8 flagComplete1 = 0;

uint8 usbPacketSignal2[USB_PACKET_SIZE];
volatile uint8 flagComplete2 = 0;

uint8 dmaChannel[2];
uint8 dmaTd[2];

char mensaje[100];
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ISRs
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
CY_ISR(DMA1_ISR){
    flagComplete1 = 1;
    CyDmaClearPendingDrq(dmaChannel[0]);
}

CY_ISR(DMA2_ISR){
    flagComplete2 = 1;
    CyDmaClearPendingDrq(dmaChannel[1]);
}
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FUNCTIONS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
void dma_config(uint8 i){
    switch(i){
        case 1:
            dmaChannel[0] = DMA_1_DmaInitialize(
                BYTES_PER_BURST,
                REQUEST_PER_BURST,
                HI16(CYDEV_PERIPH_BASE),
                HI16(CYDEV_SRAM_BASE)
            );
            
            dmaTd[0] = CyDmaTdAllocate();
            
            CyDmaTdSetConfiguration( 
                dmaTd[0],
                BYTES_PER_SAMPLE,
                dmaTd[0],
                TD_INC_DST_ADR | TD_AUTO_EXEC_NEXT | DMA_1__TD_TERMOUT_EN
            );

            CyDmaTdSetAddress(
                dmaTd[0],
                LO16((uint32)ADC_SAR_1_SAR_WRK_PTR),
                LO16((uint32)&usbPacketSignal1[1])
            );

            CyDmaChSetInitialTd(dmaChannel[0], dmaTd[0]);
    
            isr_1_StartEx(DMA1_ISR);

            CyDmaChEnable(dmaChannel[0], 1);
            break;
        case 2:
            dmaChannel[1] = DMA_2_DmaInitialize(
                BYTES_PER_BURST,
                REQUEST_PER_BURST,
                HI16(CYDEV_PERIPH_BASE),
                HI16(CYDEV_SRAM_BASE)
            );
            
            dmaTd[1] = CyDmaTdAllocate();
            
            CyDmaTdSetConfiguration( 
                dmaTd[1],
                BYTES_PER_SAMPLE,
                dmaTd[1],
                TD_INC_DST_ADR | TD_AUTO_EXEC_NEXT | DMA_1__TD_TERMOUT_EN
            );

            CyDmaTdSetAddress(
                dmaTd[1],
                LO16((uint32)ADC_SAR_2_SAR_WRK_PTR),
                LO16((uint32)&usbPacketSignal2[1])
            );

            CyDmaChSetInitialTd(dmaChannel[1], dmaTd[1]);
    
            isr_2_StartEx(DMA2_ISR);

            CyDmaChEnable(dmaChannel[1], 1);
            break;
        default:
            break;
    }
}

void signals_to_zero(uint8* data, uint8 num){
    data[0] = num;
    for(uint8_t i = 1; i < USB_PACKET_SIZE; i++){
        data[i] = 0x00;
    }
}
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MAIN
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
int main(void){
    CyGlobalIntEnable;
    
    signals_to_zero(usbPacketSignal1, 1);
    signals_to_zero(usbPacketSignal2, 2);
    
    dma_config(1);
    dma_config(2);

    USBUART_Start(0, USBUART_DWR_VDDD_OPERATION);
    //while(){};
    
    WaveDAC8_Start();

    ADC_SAR_1_Start();
    ADC_SAR_1_StartConvert();
    
    ADC_SAR_2_Start();
    ADC_SAR_2_StartConvert();
    
    uint8 usbInitialized = 0;

    for(;;){
        if(USBUART_GetConfiguration() != 0){
            if(!usbInitialized){
                USBUART_CDC_Init();
                usbInitialized = 1;
            }
            if(USBUART_CDCIsReady()){
                if(flagComplete1){
                    USBUART_PutData(usbPacketSignal1, USB_PACKET_SIZE);
                    flagComplete1 = 0;
                }
            }
            if(USBUART_CDCIsReady()){
                if(flagComplete2){
                    USBUART_PutData(usbPacketSignal2, USB_PACKET_SIZE);
                    flagComplete2 = 0;
                }
            }
        }else{
            usbInitialized = 0;
        }
    }
}
