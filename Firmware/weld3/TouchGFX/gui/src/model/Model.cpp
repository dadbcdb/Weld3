#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#ifndef SIMULATOR
#include "../../../../Core/Src/WeldData.h"
#endif

Model::Model() : modelListener(0), recipePage(0), recipeItem(0), stageCount(2),
                 weldResultAvailable(false), squeeze(100)
{
    stages[0].current = 27000;
    stages[0].up = 20;
    stages[0].duration = 80;
    stages[0].down = 15;
    stages[0].cool = 25;
    stages[1].current = 16000;
    stages[1].up = 20;
    stages[1].duration = 55;
    stages[1].down = 15;
    stages[1].cool = 25;
    stages[2].current = 22000;
    stages[2].up = 20;
    stages[2].duration = 65;
    stages[2].down = 15;
    stages[2].cool = 0;
}

void Model::tick()
{
#ifndef SIMULATOR
    uint8_t file = 0;
    uint8_t number = 0;
    WeldSettings settings = {};
    WeldData_GetProfileSelection(&file, &number);
    WeldData_GetSettings(&settings);
    bool changed = (recipePage != file) || (recipeItem != number) ||
                   (squeeze != settings.squeeze_ms);
    for (unsigned char i = 0; i < 3; ++i)
    {
        const unsigned short current = static_cast<unsigned short>(settings.stage_current_a[i] + 0.5f);
        const unsigned short cool = i < 2 ? static_cast<unsigned short>(settings.cool_ms[i]) : 0;
        changed = changed ||
                  (stages[i].current != current) ||
                  (stages[i].up != settings.stage_up_ms[i]) ||
                  (stages[i].duration != settings.stage_time_ms[i]) ||
                  (stages[i].down != settings.stage_down_ms[i]) ||
                  (stages[i].cool != cool);
        stages[i].current = current;
        stages[i].up = static_cast<unsigned short>(settings.stage_up_ms[i]);
        stages[i].duration = static_cast<unsigned short>(settings.stage_time_ms[i]);
        stages[i].down = static_cast<unsigned short>(settings.stage_down_ms[i]);
        stages[i].cool = cool;
    }
    unsigned char activeStages = 1;
    if ((stages[2].current != 0U) || (stages[2].duration != 0U))
    {
        activeStages = 3;
    }
    else if ((stages[1].current != 0U) || (stages[1].duration != 0U))
    {
        activeStages = 2;
    }
    changed = changed || (stageCount != activeStages);
    stageCount = activeStages;
    squeeze = static_cast<unsigned short>(settings.squeeze_ms);
    if (changed)
    {
        recipePage = file;
        recipeItem = number;
        weldResultAvailable = false;
        if (modelListener != 0)
        {
            modelListener->profileSelectionChanged();
        }
    }
#endif
}
