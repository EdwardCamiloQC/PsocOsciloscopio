#include "project.h"
#include <stdlib.h>

//===============================================
//========== MACROS
//===============================================
#define CHANNELS          4
#define BYTES_PER_SAMPLE  (CHANNELS * 2)
#define RING_SIZE         4096
#define USB_PACKET_SIZE   64

//===============================================
//========== VARIABLES GLOBALES
//===============================================
volatile uint8 ringBuffer[RING_SIZE];
volatile uint16 writeIndex = 0;

uint8 usbPacket[USB_PACKET_SIZE];

uint8 dmaChannel;
uint8 dmaTd;

//===============================================
//========== ISRs
//===============================================
CY_ISR(DMA_ISR){
    CyDmaClearPendingDrq(dmaChannel);
    writeIndex += BYTES_PER_SAMPLE;
    if(writeIndex >= RING_SIZE){
        writeIndex = 0;
    }
}


//===============================================
//========== FUNCTIONS
//===============================================
void DMA_Config(void){
    dmaChannel = DMA_1_DmaInitialize(2, 1, HI16(CYDEV_PERIPH_BASE), HI16(CYDEV_SRAM_BASE));
    
    dmaTd = CyDmaTdAllocate(); //DMA transfer descriptor
    
    CyDmaTdSetConfiguration(dmaTd, RING_SIZE, dmaTd, TD_INC_DST_ADR | TD_AUTO_EXEC_NEXT);
    CyDmaTdSetAddress(dmaTd, LO16((uint32)ADC_SAR_SAR_WRK0_PTR), LO16((uint32)ringBuffer));
    CyDmaChSetInitialTd(dmaChannel, dmaTd);
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

    USBUART_Start(0, USBUART_DWR_VDDD_OPERATION);
    
    ADC_SAR_Start();
    ADC_SAR_StartConvert();
    
    DMA_Config();
    
    isrDMA_StartEx(DMA_ISR);
    
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
                //USBUART_PutString("Test\r\n");
            }
        }else{
            usbInitialized = 0;
        }
    }
}
