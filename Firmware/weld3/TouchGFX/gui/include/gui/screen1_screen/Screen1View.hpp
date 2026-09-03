#ifndef SCREEN1VIEW_HPP
#define SCREEN1VIEW_HPP

#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>
#include <touchgfx/widgets/graph/GraphWrapAndClear.hpp>
#include <touchgfx/widgets/graph/GraphElements.hpp>
#include <touchgfx/widgets/canvas/PainterRGB888.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class Screen1View : public Screen1ViewBase
{
public:
    Screen1View();
    virtual ~Screen1View() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    void showWeldResult();
    void hideWeldResult();
    void refreshProfileSelection();
    void refreshControllerData();
protected:
    touchgfx::GraphWrapAndClear<81> resultGraph;
    touchgfx::GraphElementLine resultLine;
    touchgfx::PainterRGB888 resultPainter;
    touchgfx::TextAreaWithOneWildcard fileText;
    touchgfx::TextAreaWithOneWildcard pageText;
    touchgfx::Unicode::UnicodeChar fileBuffer[4];
    touchgfx::Unicode::UnicodeChar pageBuffer[4];
    touchgfx::TextAreaWithOneWildcard gridValueText[3][6];
    touchgfx::Unicode::UnicodeChar gridValueBuffer[3][6][8];
    unsigned int dynamicGraphDataCounter;
    int targetCurrentAt(unsigned int timeMs) const;
    unsigned int totalWaveTime() const;
    void rebuildGraphs();
};

#endif // SCREEN1VIEW_HPP
