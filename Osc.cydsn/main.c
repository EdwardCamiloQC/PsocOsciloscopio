#include "project.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DEFINES
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
#define USB_PACKET_SIZE        64

#define BYTES_ADC              2
#define SAMPLES_PER_PACKET     (USB_PACKET_SIZE/BYTES_ADC) //32 samples

#define DMA_TRANSFER_BYTES     ((SAMPLES_PER_PACKET)-2) //30 bytes
#define SAMPLES_PER_DMA        ((SAMPLES_PER_PACKET/2)-2) //15 samples per DMA -> time_irq=15/444444
#define BYTES_PER_BURST        2
#define REQUEST_PER_BURST      1

#define NUM_BUFFERS            32
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GLOBALS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
uint16 usbPacket[NUM_BUFFERS][SAMPLES_PER_PACKET];


uint8 indPack = 0;
volatile uint8 indRead = 0;
uint8 indDMA1 = 0;
uint8 indDMA2 = 0;

uint8 channelDMA1;
uint8 tdDMA1[NUM_BUFFERS]; //Transfer Descriptors;

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
    indDMA1++;

    if(indDMA1 <= atomic_load(&indDMA2))
        atomic_fetch_add(&indPack, 1);

    if(atomic_load(&indPack) <= NUM_BUFFERS){
        CyDmaClearPendingDrq(channelDMA1);
    }else{
        atomic_store(&indPack, NUM_BUFFERS);
        indDMA1 = 0;
    }
}

CY_ISR(DMA2_ISR){
    indDMA2++;

    if(indDMA2 <= atomic_load(&indDMA1))
        atomic_fetch_add(&indPack, 1);

    if(atomic_load(&indPack) <= NUM_BUFFERS){
        CyDmaClearPendingDrq(channelDMA2);
    }else{
        atomic_store(&indPack, NUM_BUFFERS);
        indDMA2 = 0;
    }
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
                    LO16((uint32)&usbPacket[i][1])
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
                    LO16((uint32)&usbPacket[i][17])
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

void reset_packet(uint16 data[][SAMPLES_PER_PACKET]){
    for(uint8 j=0; j<NUM_BUFFERS; j++){
        data[j][0]  = (1<<13) | 0x1145;
        data[j][16] = (1<<14) | 0x1144;
    }
}
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MAIN
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
int main(void){
    CyGlobalIntEnable;

    reset_packet(usbPacket);

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
                if(indPack){
                    USBUART_PutData((uint8*)usbPacket[indRead], USB_PACKET_SIZE);
                    indRead++;
                    if(indRead >= NUM_BUFFERS){
                        indRead = 0;
                    }
                    atomic_fetch_sub(&indPack, 1);
                }
            }
        }else{
            usbInitialized = 0;
        }
    }
}
