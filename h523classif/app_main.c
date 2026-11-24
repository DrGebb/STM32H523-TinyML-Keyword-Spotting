#include "app_main.h"
#include "mic_imn44.h"
#include "ai_oled_shared.h"

extern osStatus_t ai_detector_start(void);


void app_main(void){

    NVIC_SetPriorityGrouping(3);

    osKernelInitialize();
    oled_task_start();
    ai_detector_start();
    osKernelStart();

    while(1){

       
    }

}
