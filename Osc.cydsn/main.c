#include "project.h"
#include <stdlib.h>
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
uint16 usbPacket[NUM_BUFFERS][SAMPLES_PER_PACKET];

volatile uint8 writeBuffer = 0;
volatile uint8 lastBuffer = 0;
volatile uint8 numPacket = 0;

uint8 channelDMA1;
uint8 tdDMA1[NUM_BUFFERS]; //Transfer Descriptors;

char mensaje[100];
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ISRs
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
CY_ISR(DMA1_ISR){
    lastBuffer = writeBuffer;
    writeBuffer++;
    if(writeBuffer >= NUM_BUFFERS){
        writeBuffer = 0;
    }

    usbPacket[lastBuffer][0] = (usbPacket[lastBuffer][0] & 0xFF00) | numPacket;

    if(numPacket != 255){
        numPacket++;
    }else{
        numPacket = 0;
    }

    CyDmaClearPendingDrq(channelDMA1);
}
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FUNCTIONS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
void dma_config(){
    channelDMA1 = DMA_1_DmaInitialize(
        BYTES_PER_BURST,
        REQUEST_PER_BURST,
        HI16(CYDEV_PERIPH_BASE),
        HI16(CYDEV_SRAM_BASE)
    );

    for(uint8 i=0; i<NUM_BUFFERS; i++){
        tdDMA1[i] = CyDmaTdAllocate(); //Reserva los TD.
        if(tdDMA1[i] == DMA_INVALID_TD){
            while(1);
        }
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
int main(void)
{
    CyGlobalIntEnable;

    uint8 usbInitialized = 0;

    static uint8 copyPacket[USB_PACKET_SIZE];

    reset_packet(usbPacket, 1);

    dma_config();

    USBUART_Start(0, USBUART_DWR_VDDD_OPERATION);

    WaveDAC8_Start();

    ADC_SAR_1_Start();
    ADC_SAR_1_StartConvert();

    for(;;){
        if(USBUART_IsConfigurationChanged()){
            if(USBUART_GetConfiguration() && !usbInitialized){
                USBUART_CDC_Init();
                usbInitialized = 1;
            }
        }

        if(USBUART_CDCIsReady()){
            uint8 idx;
            uint8 intrState;

            intrState = CyEnterCriticalSection();
            idx = lastBuffer;
            CyExitCriticalSection(intrState);

            memcpy(copyPacket, (uint8*)usbPacket[idx], USB_PACKET_SIZE);

            USBUART_PutData(copyPacket, USB_PACKET_SIZE);
        }
    }
}
