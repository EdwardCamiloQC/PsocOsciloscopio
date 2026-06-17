#include "project.h"
#include <stdlib.h>
#include <stdio.h>
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DEFINES
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
#define USB_PACKET_SIZE        64

#define BYTES_ADC              2
#define SAMPLES_PER_PACKET     (USB_PACKET_SIZE/BYTES_ADC)
#define DMA_TRANSFER_BYTES     ((SAMPLES_PER_PACKET*BYTES_ADC)-2)
#define BYTES_PER_BURST        2
#define REQUEST_PER_BURST      1

#define NUM_BUFFERS            32
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GLOBALS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
uint16 voltageBuffer1[NUM_BUFFERS][SAMPLES_PER_PACKET];
volatile uint8 indWriteBuf1 = 0;
volatile uint8 indReadBuf1 = 0;
volatile uint8 nReadyBuf1 = 0;
uint8 channelDMA1;
uint8 tdDMA1[NUM_BUFFERS]; //Transfer Descriptors;

uint16 voltageBuffer2[NUM_BUFFERS][SAMPLES_PER_PACKET];
volatile uint8 indWriteBuf2 = 0;
volatile uint8 indReadBuf2 = 0;
volatile uint8 nReadyBuf2 = 0;
uint8 channelDMA2;
uint8 tdDMA2[NUM_BUFFERS]; //Transfer Descriptors.
volatile uint8 pingpongDMA2 = 0;

char mensaje[100];
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ISRs
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
CY_ISR(DMA1_ISR){
    indWriteBuf1++;

    if(indWriteBuf1 >= NUM_BUFFERS){
        indWriteBuf1 = 0;
    }

    nReadyBuf1++;

    CyDmaClearPendingDrq(channelDMA1);
}

CY_ISR(DMA2_ISR){
    indWriteBuf2++;

    if(indWriteBuf2 >= NUM_BUFFERS){
        indWriteBuf2 = 0;
    }

    nReadyBuf2++;

    CyDmaClearPendingDrq(channelDMA2);
}
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FUNCTIONS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
void dma_config(uint8 i){
    switch(i){
        case 1:
            channelDMA1 = DMA_1_DmaInitialize(
                BYTES_PER_BURST,
                REQUEST_PER_BURST,
                HI16(CYDEV_PERIPH_BASE),
                HI16(CYDEV_SRAM_BASE)
            );

            for(uint8 i=0; i<NUM_BUFFERS; i++){
                tdDMA1[i] = CyDmaTdAllocate(); //Reserva los TD.
            }

            for(uint8 i=0; i<NUM_BUFFERS; i++){
                uint8 next = (i+1)%NUM_BUFFERS;
                CyDmaTdSetConfiguration( 
                    tdDMA1[i],
                    DMA_TRANSFER_BYTES,
                    tdDMA1[next],
                    TD_INC_DST_ADR |
                    TD_AUTO_EXEC_NEXT |
                    DMA_1__TD_TERMOUT_EN
                );

                CyDmaTdSetAddress(
                    tdDMA1[i],
                    LO16((uint32)ADC_SAR_1_SAR_WRK_PTR),
                    LO16((uint32)&voltageBuffer1[i][1])
                );
            }

            CyDmaChSetInitialTd(channelDMA1, tdDMA1[0]);

            isr_1_StartEx(DMA1_ISR);

            CyDmaChEnable(channelDMA1, 1);
            break;
        case 2:
            channelDMA2 = DMA_2_DmaInitialize(
                BYTES_PER_BURST,
                REQUEST_PER_BURST,
                HI16(CYDEV_PERIPH_BASE),
                HI16(CYDEV_SRAM_BASE)
            );

            for(uint8 i=0; i<NUM_BUFFERS; i++){
                tdDMA2[i] = CyDmaTdAllocate();
            }
            
            for(uint8 i=0; i<NUM_BUFFERS; i++){
                uint8 next = (i+1)%NUM_BUFFERS;
                CyDmaTdSetConfiguration(
                    tdDMA2[i],
                    DMA_TRANSFER_BYTES,
                    tdDMA2[next],
                    TD_INC_DST_ADR |
                    TD_AUTO_EXEC_NEXT |
                    DMA_2__TD_TERMOUT_EN
                );

                CyDmaTdSetAddress(
                    tdDMA2[i],
                    LO16((uint32)ADC_SAR_2_SAR_WRK_PTR),
                    LO16((uint32)&voltageBuffer2[i][1])
                );
            }

            CyDmaChSetInitialTd(channelDMA2, tdDMA2[0]);
    
            isr_2_StartEx(DMA2_ISR);

            CyDmaChEnable(channelDMA2, 1);
            break;
        default:
            break;
    }
}

void signals_to_zero(uint16 data[][SAMPLES_PER_PACKET], uint8 num){
    for(uint8 j=0; j<NUM_BUFFERS; j++){
        data[j][0] = num;
        for(uint8_t i = 1; i < SAMPLES_PER_PACKET; i++){
            data[j][i] = 0x00;
        }
    }
}
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MAIN
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
int main(void){
    CyGlobalIntEnable;

    signals_to_zero(voltageBuffer1, 1);

    signals_to_zero(voltageBuffer2, 2);

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
                USBUART_PutString("USB OK\r\n");
                usbInitialized = 1;
            }
            if(USBUART_CDCIsReady()){ //Endpoint libre.
                if(nReadyBuf1){
                    USBUART_PutData((uint8*)voltageBuffer1[indReadBuf1], USB_PACKET_SIZE);
                    indReadBuf1++;
                    if(indReadBuf1 >= NUM_BUFFERS){
                        indReadBuf1 = 0;
                    }
                    nReadyBuf1--;
                }
            }
            if(USBUART_CDCIsReady()){ //Endpoint libre.
                if(nReadyBuf2){
                    USBUART_PutData((uint8*)voltageBuffer2[indReadBuf2], USB_PACKET_SIZE);
                    indReadBuf2++;
                    if(indReadBuf2 >= NUM_BUFFERS){
                        indReadBuf2 = 0;
                    }
                    nReadyBuf2--;
                }
            }
        }else{
            usbInitialized = 0;
        }
    }
}
