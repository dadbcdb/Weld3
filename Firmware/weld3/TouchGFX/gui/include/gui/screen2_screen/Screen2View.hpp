#ifndef SCREEN2VIEW_HPP
#define SCREEN2VIEW_HPP

#include <gui_generated/screen2_screen/Screen2ViewBase.hpp>
#include <gui/screen2_screen/Screen2Presenter.hpp>
#include <touchgfx/containers/buttons/Buttons.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/widgets/Box.hpp>

class Screen2View : public Screen2ViewBase
{
public:
    Screen2View();
    virtual ~Screen2View() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
    typedef touchgfx::BoxWithBorderButtonStyle<touchgfx::ClickButtonTrigger> UiButton;
    touchgfx::Box headerBox;
    touchgfx::Box stageBox[3];
    UiButton pageDownButton, pageUpButton, itemDownButton, itemUpButton;
    UiButton twoStageButton, threeStageButton;
    touchgfx::TextAreaWithOneWildcard pageText, itemText;
    touchgfx::TextAreaWithOneWildcard buttonText[6];
    touchgfx::TextAreaWithOneWildcard stageNumberText[3];
    touchgfx::TextAreaWithOneWildcard stageCurrentText[3];
    touchgfx::TextAreaWithOneWildcard stageTimeText[3];
    touchgfx::Unicode::UnicodeChar pageBuffer[8], itemBuffer[8];
    touchgfx::Unicode::UnicodeChar buttonBuffer[6][4];
    touchgfx::Unicode::UnicodeChar stageNumberBuffer[3][4];
    touchgfx::Unicode::UnicodeChar stageCurrentBuffer[3][8];
    touchgfx::Unicode::UnicodeChar stageTimeBuffer[3][8];
    touchgfx::Callback<Screen2View, const touchgfx::AbstractButtonContainer&> buttonCallback;

    void configureButton(UiButton& button, int x, int y, int w, int h);
    void configureText(touchgfx::TextAreaWithOneWildcard& text,
                       touchgfx::Unicode::UnicodeChar* buffer,
                       int x, int y, int w, int h);
    void buttonPressed(const touchgfx::AbstractButtonContainer& source);
    void refreshValues();
};

#endif // SCREEN2VIEW_HPP
