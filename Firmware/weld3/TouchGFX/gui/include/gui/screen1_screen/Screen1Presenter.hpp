#ifndef SCREEN1PRESENTER_HPP
#define SCREEN1PRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen1View;

class Screen1Presenter : public touchgfx::Presenter, public ModelListener
{
public:
    Screen1Presenter(Screen1View& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~Screen1Presenter() {}

    unsigned char getStageCount() const { return model->getStageCount(); }
    bool isWeldResultAvailable() const { return model->isWeldResultAvailable(); }
    void setWeldResultAvailable(bool value) { model->setWeldResultAvailable(value); }
    const Model::WeldStage& getStage(unsigned char index) const { return model->getStage(index); }
    virtual void profileSelectionChanged();
    unsigned char getRecipePage() const { return model->getRecipePage(); }
    unsigned char getRecipeItem() const { return model->getRecipeItem(); }
    unsigned short getSqueeze() const { return model->getSqueeze(); }

private:
    Screen1Presenter();

    Screen1View& view;
};

#endif // SCREEN1PRESENTER_HPP
