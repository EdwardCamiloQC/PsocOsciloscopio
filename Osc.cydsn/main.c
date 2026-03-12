#include "project.h"
#include <stdlib.h>

//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MACROS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
#define CHANNELS              4
#define BYTES_PER_SAMPLE      (CHANNELS * 2)

#define USB_PACKET_SIZE       32

#define DMA_BYTES_PER_BURST   2
#define DMA_REQUEST_PER_BURST 1

//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// VARIABLES GLOBALES
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
uint16 usbPacketSignals[USB_PACKET_SIZE/2];
volatile uint8 flagComplete = 0;

uint8 dmaChannel;
uint8 dmaTd[1];

uint8 prueba[USB_PACKET_SIZE];

//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ISRs
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
CY_ISR(DMA_ISR){
    CyDmaClearPendingDrq(dmaChannel);
    flagComplete = 1;
}


//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FUNCTIONS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
void DMA_Config(void){
    dmaChannel = DMA_1_DmaInitialize(DMA_BYTES_PER_BURST, DMA_REQUEST_PER_BURST, HI16(CYDEV_PERIPH_BASE), HI16(CYDEV_SRAM_BASE));
    
    dmaTd[0] = CyDmaTdAllocate(); //DMA transfer descriptor
    
    CyDmaTdSetConfiguration(dmaTd[0], USB_PACKET_SIZE, dmaTd[0], TD_INC_DST_ADR | TD_AUTO_EXEC_NEXT | DMA_1__TD_TERMOUT_EN);
    CyDmaTdSetAddress(dmaTd[0], LO16((uint32)ADC_SAR_Seq_SAR_SAR_WRK_PTR), LO16((uint32)usbPacketSignals));
    CyDmaChSetInitialTd(dmaChannel, dmaTd[0]);
    
    isrDMA_StartEx(DMA_ISR);
    
    CyDmaChEnable(dmaChannel, 1);
}

//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MAIN
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
int main(void){
    CyGlobalIntEnable;

    for(unsigned int i = 0; i < USB_PACKET_SIZE/2; i++){
        usbPacketSignals[i] = (0x7E << 8) | 0x7C;
    }

    DMA_Config();

    USBUART_Start(0, USBUART_DWR_VDDD_OPERATION);
    //while(){};
    
    WaveDAC8_Start();

    ADC_SAR_Seq_Start();
    ADC_SAR_Seq_IRQ_Disable();
    
    ADC_SAR_Seq_StartConvert();
    
    uint8 usbInitialized = 0;

    for(;;){
        if(USBUART_GetConfiguration() != 0){
            if(!usbInitialized){
                USBUART_CDC_Init();
                usbInitialized = 1;
            }
            if(flagComplete){
                if(USBUART_CDCIsReady()){
                    USBUART_PutData((uint8*)usbPacketSignals, USB_PACKET_SIZE);
                    flagComplete = 0;
                }
            }
        }else{
            usbInitialized = 0;
        }
    }
}
