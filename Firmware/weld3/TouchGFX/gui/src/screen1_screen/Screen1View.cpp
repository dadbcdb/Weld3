#include <gui/screen1_screen/Screen1View.hpp>
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>

Screen1View::Screen1View()
{
    // Keep the result graph pixel-perfect with the graph edited in Designer.
    // Reading the generated graph here also keeps future margin/padding edits in sync.
    resultGraph.setPosition(dynamicGraph1.getX(), dynamicGraph1.getY(),
                            dynamicGraph1.getWidth(), dynamicGraph1.getHeight());
    resultGraph.setGraphAreaMargin(dynamicGraph1.getGraphAreaMarginTop(),
                                   dynamicGraph1.getGraphAreaMarginLeft(),
                                   dynamicGraph1.getGraphAreaMarginRight(),
                                   dynamicGraph1.getGraphAreaMarginBottom());
    resultGraph.setGraphAreaPadding(dynamicGraph1.getGraphAreaPaddingTop(),
                                    dynamicGraph1.getGraphAreaPaddingLeft(),
                                    dynamicGraph1.getGraphAreaPaddingRight(),
                                    dynamicGraph1.getGraphAreaPaddingBottom());
    resultGraph.setGraphRangeY(0, 32000);
    resultPainter.setColor(touchgfx::Color::getColorFromRGB(255, 48, 48));
    resultLine.setPainter(resultPainter);
    resultLine.setLineWidth(2);
    resultGraph.addGraphElement(resultLine);
    add(resultGraph);

    fileText.setPosition(boxWithBorderFile.getX(), boxWithBorderFile.getY() + 3,
                         boxWithBorderFile.getWidth(), 13);
    fileText.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    fileText.setTypedText(touchgfx::TypedText(T_PROFILE_VALUE));
    fileText.setWildcard(fileBuffer);
    add(fileText);

    pageText.setPosition(boxWithBorderPage.getX(), boxWithBorderPage.getY() + 3,
                         boxWithBorderPage.getWidth(), 13);
    pageText.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    pageText.setTypedText(touchgfx::TypedText(T_PROFILE_VALUE));
    pageText.setWildcard(pageBuffer);
    add(pageText);

    static const int gridX[6] = { 124, 164, 221, 278, 335, 392 };
    static const int gridWidth[6] = { 40, 57, 57, 57, 57, 57 };
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 6; ++column)
        {
            gridValueText[row][column].setPosition(gridX[column], 29 + row * 18,
                                                   gridWidth[column], 13);
            gridValueText[row][column].setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
            gridValueText[row][column].setTypedText(touchgfx::TypedText(T_PROFILE_VALUE));
            gridValueText[row][column].setWildcard(gridValueBuffer[row][column]);
            add(gridValueText[row][column]);
        }
    }
}

void Screen1View::setupScreen()
{
    Screen1ViewBase::setupScreen();
    refreshProfileSelection();
    rebuildGraphs();
    resultGraph.setVisible(presenter->isWeldResultAvailable());
}

void Screen1View::refreshProfileSelection()
{
    touchgfx::Unicode::snprintf(fileBuffer, 4, "%02u", presenter->getRecipePage());
    touchgfx::Unicode::snprintf(pageBuffer, 4, "%02u", presenter->getRecipeItem());
    for (unsigned char row = 0; row < 3; ++row)
    {
        const Model::WeldStage& stage = presenter->getStage(row);
        touchgfx::Unicode::snprintf(gridValueBuffer[row][0], 8, "%u", row + 1);
        touchgfx::Unicode::snprintf(gridValueBuffer[row][1], 8, "%u", stage.current);
        touchgfx::Unicode::snprintf(gridValueBuffer[row][2], 8, "%u",
                                    row == 0 ? presenter->getSqueeze() : presenter->getStage(row - 1).cool);
        touchgfx::Unicode::snprintf(gridValueBuffer[row][3], 8, "%u", stage.up);
        touchgfx::Unicode::snprintf(gridValueBuffer[row][4], 8, "%u", stage.duration);
        touchgfx::Unicode::snprintf(gridValueBuffer[row][5], 8, "%u", stage.down);
        for (int column = 0; column < 6; ++column)
        {
            gridValueText[row][column].invalidate();
        }
    }
    fileText.invalidate();
    pageText.invalidate();
}

void Screen1View::refreshControllerData()
{
    refreshProfileSelection();
    rebuildGraphs();
}

void Screen1View::tearDownScreen()
{
    Screen1ViewBase::tearDownScreen();
}

void Screen1View::showWeldResult()
{
    presenter->setWeldResultAvailable(true);
    resultGraph.setVisible(true);
    rebuildGraphs();
}

void Screen1View::hideWeldResult()
{
    presenter->setWeldResultAvailable(false);
    resultGraph.clear();
    resultGraph.setVisible(false);
    resultGraph.invalidate();
}

unsigned int Screen1View::totalWaveTime() const
{
    unsigned int total = 20;
    for (unsigned char i = 0; i < presenter->getStageCount(); ++i)
    {
        total += 20 + presenter->getStage(i).duration + 15;
        if (i + 1 < presenter->getStageCount())
        {
            total += 25;
        }
    }
    return total;
}

int Screen1View::targetCurrentAt(unsigned int timeMs) const
{
    if (timeMs < 20)
    {
        return 0;
    }
    timeMs -= 20;

    for (unsigned char i = 0; i < presenter->getStageCount(); ++i)
    {
        const int current = presenter->getStage(i).current;
        if (timeMs < 20)
        {
            return static_cast<int>((static_cast<unsigned long>(current) * timeMs) / 20);
        }
        timeMs -= 20;
        if (timeMs < presenter->getStage(i).duration)
        {
            return current;
        }
        timeMs -= presenter->getStage(i).duration;
        if (timeMs < 15)
        {
            return static_cast<int>((static_cast<unsigned long>(current) * (15 - timeMs)) / 15);
        }
        timeMs -= 15;
        if (i + 1 < presenter->getStageCount())
        {
            if (timeMs < 25)
            {
                return 0;
            }
            timeMs -= 25;
        }
    }
    return 0;
}

void Screen1View::rebuildGraphs()
{
    const unsigned int total = totalWaveTime();
    const float sampleMs = total / 100.0f;

    dynamicGraph1.clear();
    resultGraph.clear();

    dynamicGraph1.setGraphRangeY(0, 32000);
    dynamicGraph1.setXAxisFactor(sampleMs);
    resultGraph.setGraphRangeY(0, 32000);
    resultGraph.setXAxisFactor(sampleMs);

    dynamicGraph1MinorXAxisGrid.setInterval(25);
    dynamicGraph1MajorXAxisGrid.setInterval(50);
    dynamicGraph1MinorYAxisGrid.setInterval(2000);
    dynamicGraph1MajorYAxisGrid.setInterval(4000);
    dynamicGraph1MajorXAxisLabel.setInterval(50);
    dynamicGraph1MajorYAxisLabel.setInterval(4000);
    dynamicGraph1Line1Painter.setColor(touchgfx::Color::getColorFromRGB(0, 230, 70));

    for (unsigned int i = 0; i <= 100; ++i)
    {
        const int target = targetCurrentAt(static_cast<unsigned int>(i * sampleMs));
        const int ripple = target == 0 ? 0 : static_cast<int>((i % 7) * 90) - 270;
        int measured = target + ripple;
        if (measured < 0)
        {
            measured = 0;
        }
        dynamicGraph1.addDataPoint(target);
        if (presenter->isWeldResultAvailable())
        {
            resultGraph.addDataPoint(measured);
        }
    }

    dynamicGraph1.invalidate();
    if (presenter->isWeldResultAvailable())
    {
        resultGraph.invalidate();
    }
}
