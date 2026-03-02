#include "project.h"
#include <stdlib.h>

//===============================================
//========== MACROS
//===============================================
#define CHANNELS              4
#define BYTES_PER_SAMPLE      (CHANNELS * 2)
#define RING_SIZE             4096
#define USB_PACKET_SIZE       64

#define DMA_BYTES_PER_BURST   2
#define DMA_REQUEST_PER_BURST 1

//===============================================
//========== VARIABLES GLOBALES
//===============================================
volatile uint8 ringBuffer[RING_SIZE];
volatile uint16 writeIndex = 0;

uint8 usbPacket[USB_PACKET_SIZE];

uint8 dmaChannel;
uint8 dmaTd[1];

volatile uint8 muxChannel = 0;

//===============================================
//========== ISRs
//===============================================
CY_ISR(DMA_ISR){
    CyDmaClearPendingDrq(dmaChannel);
    writeIndex += BYTES_PER_SAMPLE;
    if(writeIndex >= RING_SIZE){
        writeIndex = 0;
    }
    muxChannel++;
    if(muxChannel >= 4){
        muxChannel = 0;
    }
    AMux_1_Select(muxChannel);
}


//===============================================
//========== FUNCTIONS
//===============================================
void DMA_Config(void){
    dmaChannel = DMA_1_DmaInitialize(DMA_BYTES_PER_BURST, DMA_REQUEST_PER_BURST, HI16(CYDEV_PERIPH_BASE), HI16(CYDEV_SRAM_BASE));
    
    dmaTd[0] = CyDmaTdAllocate(); //DMA transfer descriptor
    
    CyDmaTdSetConfiguration(dmaTd[0], RING_SIZE, dmaTd[0], TD_INC_DST_ADR | TD_AUTO_EXEC_NEXT);
                                                         //DMA_1__TD_TERMOUT_EN
    CyDmaTdSetAddress(dmaTd[0], LO16((uint32)ADC_SAR_SAR_WRK0_PTR), LO16((uint32)ringBuffer));
    CyDmaChSetInitialTd(dmaChannel, dmaTd[0]);
    CyDmaChEnable(dmaChannel, 1);
}

void GetLatestSamples(void){
    uint16 readIndex;
    if(writeIndex >= USB_PACKET_SIZE)
        readIndex = writeIndex - USB_PACKET_SIZE;
    else
        readIndex = RING_SIZE + writeIndex - USB_PACKET_SIZE;

    for(uint16 i = 0; i < USB_PACKET_SIZE; i++){
        usbPacket[i] = ringBuffer[(readIndex + i) % RING_SIZE];
    }
}

//===============================================
//========== MAIN
//===============================================
int main(void){
    CyGlobalIntEnable;
    
    DMA_Config();

    USBUART_Start(0, USBUART_DWR_VDDD_OPERATION);
    
    AMux_1_Start();
    AMux_1_Select(muxChannel);
    ADC_SAR_Start();
    ADC_SAR_IRQ_Disable();
    
    isrDMA_StartEx(DMA_ISR);
    
    ADC_SAR_StartConvert();
    
    uint8 usbInitialized = 0;

    for(;;){
        if(USBUART_GetConfiguration() != 0){
            if(!usbInitialized){
                USBUART_CDC_Init();
                usbInitialized = 1;
            }
            if(USBUART_CDCIsReady()){
                GetLatestSamples();
                USBUART_PutData(usbPacket, USB_PACKET_SIZE);
            }
        }else{
            usbInitialized = 0;
        }
    }
}
