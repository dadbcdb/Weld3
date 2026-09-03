#include <gui/screen2_screen/Screen2View.hpp>
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>

Screen2View::Screen2View() : buttonCallback(this, &Screen2View::buttonPressed)
{
    textArea1.setVisible(false);
    image1.setVisible(false);
    headerBox.setPosition(58, 8, 414, 62);
    headerBox.setColor(touchgfx::Color::getColorFromRGB(18, 28, 38));
    add(headerBox);

    configureButton(pageDownButton, 64, 15, 38, 46);
    configureButton(pageUpButton, 176, 15, 38, 46);
    configureButton(itemDownButton, 222, 15, 38, 46);
    configureButton(itemUpButton, 334, 15, 38, 46);
    configureButton(twoStageButton, 380, 15, 40, 46);
    configureButton(threeStageButton, 426, 15, 40, 46);

    configureText(pageText, pageBuffer, 105, 26, 68, 30);
    configureText(itemText, itemBuffer, 263, 26, 68, 30);
    const int buttonX[6] = { 74, 186, 232, 344, 391, 437 };
    for (int i = 0; i < 6; ++i)
        configureText(buttonText[i], buttonBuffer[i], buttonX[i], 26, 20, 28);
    touchgfx::Unicode::strncpy(buttonBuffer[0], "<", 4);
    touchgfx::Unicode::strncpy(buttonBuffer[1], ">", 4);
    touchgfx::Unicode::strncpy(buttonBuffer[2], "<", 4);
    touchgfx::Unicode::strncpy(buttonBuffer[3], ">", 4);
    touchgfx::Unicode::strncpy(buttonBuffer[4], "2", 4);
    touchgfx::Unicode::strncpy(buttonBuffer[5], "3", 4);

    for (int i = 0; i < 3; ++i)
    {
        const int y = 78 + i * 61;
        stageBox[i].setPosition(58, y, 414, 53);
        stageBox[i].setColor(touchgfx::Color::getColorFromRGB(10, 40, 52));
        add(stageBox[i]);
        configureText(stageNumberText[i], stageNumberBuffer[i], 68, y + 14, 30, 28);
        configureText(stageCurrentText[i], stageCurrentBuffer[i], 145, y + 14, 100, 28);
        configureText(stageTimeText[i], stageTimeBuffer[i], 315, y + 14, 85, 28);
    }
}

void Screen2View::setupScreen()
{
    Screen2ViewBase::setupScreen();
    refreshValues();
}

void Screen2View::tearDownScreen()
{
    Screen2ViewBase::tearDownScreen();
}

void Screen2View::configureButton(UiButton& button, int x, int y, int w, int h)
{
    button.setBoxWithBorderPosition(0, 0, w, h);
    button.setBorderSize(2);
    button.setBoxWithBorderColors(touchgfx::Color::getColorFromRGB(25, 72, 96),
                                  touchgfx::Color::getColorFromRGB(35, 112, 148),
                                  touchgfx::Color::getColorFromRGB(5, 20, 28),
                                  touchgfx::Color::getColorFromRGB(0, 210, 255));
    button.setPosition(x, y, w, h);
    button.setAction(buttonCallback);
    add(button);
}

void Screen2View::configureText(touchgfx::TextAreaWithOneWildcard& text,
                                touchgfx::Unicode::UnicodeChar* buffer,
                                int x, int y, int w, int h)
{
    text.setPosition(x, y, w, h);
    text.setColor(touchgfx::Color::getColorFromRGB(220, 240, 250));
    text.setTypedText(touchgfx::TypedText(T___SINGLEUSE_QNYZ));
    text.setWildcard(buffer);
    add(text);
}

void Screen2View::buttonPressed(const touchgfx::AbstractButtonContainer& source)
{
    if (&source == &pageDownButton)
        presenter->setRecipePage(static_cast<unsigned char>(presenter->getRecipePage() - 1));
    else if (&source == &pageUpButton)
        presenter->setRecipePage(static_cast<unsigned char>(presenter->getRecipePage() + 1));
    else if (&source == &itemDownButton)
        presenter->setRecipeItem(static_cast<unsigned char>(presenter->getRecipeItem() - 1));
    else if (&source == &itemUpButton)
        presenter->setRecipeItem(static_cast<unsigned char>(presenter->getRecipeItem() + 1));
    else if (&source == &twoStageButton)
        presenter->setStageCount(2);
    else if (&source == &threeStageButton)
        presenter->setStageCount(3);
    refreshValues();
}

void Screen2View::refreshValues()
{
    touchgfx::Unicode::snprintf(pageBuffer, 8, "%03u", presenter->getRecipePage() + 1);
    touchgfx::Unicode::snprintf(itemBuffer, 8, "%03u", presenter->getRecipeItem() + 1);
    for (unsigned char i = 0; i < 3; ++i)
    {
        const bool enabled = i < presenter->getStageCount();
        touchgfx::Unicode::snprintf(stageNumberBuffer[i], 4, "%u", i + 1);
        touchgfx::Unicode::snprintf(stageCurrentBuffer[i], 8, "%u", presenter->getStage(i).current);
        touchgfx::Unicode::snprintf(stageTimeBuffer[i], 8, "%u", presenter->getStage(i).duration);
        stageBox[i].setColor(enabled ? touchgfx::Color::getColorFromRGB(10, 40, 52)
                                     : touchgfx::Color::getColorFromRGB(22, 22, 22));
        stageNumberText[i].setAlpha(enabled ? 255 : 70);
        stageCurrentText[i].setAlpha(enabled ? 255 : 70);
        stageTimeText[i].setAlpha(enabled ? 255 : 70);
        stageBox[i].invalidate();
        stageNumberText[i].invalidate();
        stageCurrentText[i].invalidate();
        stageTimeText[i].invalidate();
    }
    const touchgfx::colortype selected = touchgfx::Color::getColorFromRGB(0, 255, 120);
    const touchgfx::colortype normal = touchgfx::Color::getColorFromRGB(0, 210, 255);
    twoStageButton.setBoxWithBorderColors(touchgfx::Color::getColorFromRGB(25, 72, 96),
                                          touchgfx::Color::getColorFromRGB(35, 112, 148),
                                          presenter->getStageCount() == 2 ? selected : normal,
                                          selected);
    threeStageButton.setBoxWithBorderColors(touchgfx::Color::getColorFromRGB(25, 72, 96),
                                            touchgfx::Color::getColorFromRGB(35, 112, 148),
                                            presenter->getStageCount() == 3 ? selected : normal,
                                            selected);
    pageText.invalidate();
    itemText.invalidate();
    twoStageButton.invalidate();
    threeStageButton.invalidate();
}
