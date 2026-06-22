#include "project.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DEFINES
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
#define USB_PACKET_SIZE        64

#define BYTES_ADC              2
#define SAMPLES_PER_PACKET     (USB_PACKET_SIZE/BYTES_ADC) //32 samples

#define DMA_TRANSFER_BYTES     ((USB_PACKET_SIZE)-2) //62 bytes
#define SAMPLES_PER_DMA        ((SAMPLES_PER_PACKET)-1) //31 samples per DMA -> time_irq=31/444444
#define BYTES_PER_BURST        2
#define REQUEST_PER_BURST      1

#define NUM_BUFFERS            16
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GLOBALS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
uint16 usbPacket1[NUM_BUFFERS][SAMPLES_PER_PACKET];
uint16 usbPacket2[NUM_BUFFERS][SAMPLES_PER_PACKET];

volatile bool pivot = false; //false->usbPacket1, true->usbPacket2
volatile uint8 writeDMA1 = 0;
volatile uint8 readDMA1;
volatile uint8 writeDMA2 = 0;
volatile uint8 readDMA2;
volatile bool newData1 = false;
volatile bool newData2 = false;

uint8 channelDMA1;
uint8 tdDMA1[NUM_BUFFERS]; //Transfer Descriptors;

uint8 channelDMA2;
uint8 tdDMA2[NUM_BUFFERS]; //Transfer Descriptors.

char mensaje[100];
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ISRs
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
CY_ISR(DMA1_ISR){
    readDMA1 = writeDMA1;
    writeDMA1++;
    if(writeDMA1 >= NUM_BUFFERS){
        writeDMA1 = 0;
    }
    newData1 = true;

    CyDmaClearPendingDrq(channelDMA1);
}

CY_ISR(DMA2_ISR){
    readDMA2 = writeDMA2;
    writeDMA2++;
    if(writeDMA2 >= NUM_BUFFERS){
        writeDMA2 = 0;
    }
    newData2 = true;

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
                    LO16((uint32)&usbPacket1[i][1])
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
                    LO16((uint32)&usbPacket2[i][1])
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

void reset_packet(uint16 data[][SAMPLES_PER_PACKET], uint8 num){
    for(uint8 j=0; j<NUM_BUFFERS; j++){
        data[j][0]  = (num<<13) | 0x1145;
    }
}
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MAIN
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
int main(void){
    CyGlobalIntEnable;

    reset_packet(usbPacket1, 1);
    reset_packet(usbPacket2, 2);

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
                if(!pivot){
                    if(newData1){
                        USBUART_PutData((uint8*)usbPacket2[readDMA1], USB_PACKET_SIZE);
                        newData1 = false;
                        pivot = true;
                    }
                }else{
                    if(newData2){
                        USBUART_PutData((uint8*)usbPacket1[readDMA2], USB_PACKET_SIZE);
                        newData2 = false;
                        pivot = false;
                    }
                }
                pivot = !pivot;
            }
        }else{
            usbInitialized = 0;
        }
    }
}
