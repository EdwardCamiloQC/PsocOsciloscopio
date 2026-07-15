#include "project.h"
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DEFINES
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
#define TD_BYTES_SIZE            512

#define BYTES_ADC                2
#define SAMPLES_PER_TD           (TD_BYTES_SIZE / BYTES_ADC) //256 samples

#define DMA_TRANSFER_BYTES       (TD_BYTES_SIZE - BYTES_ADC) //510 bytes
#define SAMPLES_PER_DMA          (SAMPLES_PER_TD - 1) //255 samples per DMA -> tirq ≈ 0.57375ms
#define BYTES_PER_BURST          2
#define REQUEST_PER_BURST        1

#define NUM_TRANSFER_DESCRIPTORS 2
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GLOBALS
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
uint16 voltage1[NUM_TRANSFER_DESCRIPTORS][SAMPLES_PER_TD];

volatile uint8 writeBuffer = 0;
volatile uint8 lastBuffer = 0;
volatile uint8 numPacket = 0;

uint8 channelDMA1;
uint8 tdDMA1[NUM_TRANSFER_DESCRIPTORS]; //Transfer Descriptors;
//===============================================
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ISRs
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//===============================================
CY_ISR(DMA1_ISR){
    lastBuffer = writeBuffer;
    writeBuffer++;
    if(writeBuffer >= NUM_TRANSFER_DESCRIPTORS){
        writeBuffer = 0;
    }

    voltage1[lastBuffer][0] = (voltage1[lastBuffer][0] & 0xFF00) | numPacket;

    if(numPacket != 255){
        numPacket++;
    }else{
        numPacket = 0;
    }
    
    if(USBFS_1_GetEPState(1) == USBFS_1_IN_BUFFER_EMPTY){
        USBFS_1_LoadInEP(1, (uint8*)&voltage1[lastBuffer], TD_BYTES_SIZE);
    }
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

    for(uint8 i=0; i<NUM_TRANSFER_DESCRIPTORS; i++){
        tdDMA1[i] = CyDmaTdAllocate(); //Reserva los TD.
        if(tdDMA1[i] == DMA_INVALID_TD){
            while(1);
        }
    }

    for(uint8 i=0; i<NUM_TRANSFER_DESCRIPTORS; i++){
        uint8 next = (i+1)%NUM_TRANSFER_DESCRIPTORS;
        CyDmaTdSetConfiguration( 
            tdDMA1[i],
            DMA_TRANSFER_BYTES,
            tdDMA1[next],
            TD_INC_DST_ADR |
            DMA_1__TD_TERMOUT_EN
        );

        CyDmaTdSetAddress(
            tdDMA1[i],
            LO16((uint32)ADC_SAR_1_SAR_WRK_PTR),
            LO16((uint32)&voltage1[i][1])
        );
    }

    CyDmaChSetInitialTd(channelDMA1, tdDMA1[0]);

    isr_1_StartEx(DMA1_ISR);

    CyDmaChEnable(channelDMA1, 1);
            
}

void reset_packet(uint16 data[][SAMPLES_PER_TD], uint8 num){
    for(uint8 j=0; j<NUM_TRANSFER_DESCRIPTORS; j++){
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

    uint8 usbInitialized = 0;

    reset_packet(voltage1, 1);

    dma_config();

    USBFS_1_Start(0, USBFS_1_DWR_VDDD_OPERATION);

    WaveDAC8_Start();

    ADC_SAR_1_Start();
    ADC_SAR_1_StartConvert();

    for(;;){
        if(USBFS_1_IsConfigurationChanged()){
            if(USBFS_1_GetConfiguration() && !usbInitialized){
                USBFS_1_Init();
                usbInitialized = 1;
            }
        }
    }
}
