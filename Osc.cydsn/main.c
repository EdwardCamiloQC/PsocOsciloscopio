#include "project.h"
#include <stdlib.h>

//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MACROS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
#define CHANNELS              4
#define BYTES_PER_SAMPLES     (CHANNELS * 2)
#define DMA_BYTES_PER_BURST   2
#define DMA_REQUEST_PER_BURST 1

#define USB_PACKET_SIZE       32

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
    flagComplete = 1;
    CyDmaClearPendingDrq(dmaChannel);
}


//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FUNCTIONS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
void DMA_Config(void){
    dmaChannel = DMA_1_DmaInitialize(DMA_BYTES_PER_BURST, DMA_REQUEST_PER_BURST, HI16(CYDEV_PERIPH_BASE), HI16(CYDEV_SRAM_BASE));
    
    dmaTd[0] = CyDmaTdAllocate(); //DMA transfer descriptor
    
    CyDmaTdSetConfiguration(dmaTd[0], BYTES_PER_SAMPLES, dmaTd[0], TD_INC_DST_ADR | TD_AUTO_EXEC_NEXT | DMA_1__TD_TERMOUT_EN);
    CyDmaTdSetAddress(dmaTd[0], LO16((uint32)ADC_SAR_Seq_SAR_SAR_WRK_PTR), LO16((uint32)&usbPacketSignals[4]));
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

    usbPacketSignals[0] = (0x7E << 8) | 0x23;
    for(unsigned int i = 1; i < 4; i++){
        usbPacketSignals[i] = (0x7E << 8) | 0x7E;
    }
    for(unsigned int i = 4; i < 8; i++){
        usbPacketSignals[i] = 0x0000;
    }
    for(unsigned int i = 8; i < 15; i++){
        usbPacketSignals[i] = (0x7C << 8) | 0x7C;
    }
    usbPacketSignals[15] = (0x0A << 8) | 0x0D;

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
