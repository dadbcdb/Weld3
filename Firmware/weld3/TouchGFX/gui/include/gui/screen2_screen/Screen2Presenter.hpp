#ifndef SCREEN2PRESENTER_HPP
#define SCREEN2PRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen2View;

class Screen2Presenter : public touchgfx::Presenter, public ModelListener
{
public:
    Screen2Presenter(Screen2View& v);

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

    virtual ~Screen2Presenter() {}

    unsigned char getRecipePage() const { return model->getRecipePage(); }
    unsigned char getRecipeItem() const { return model->getRecipeItem(); }
    unsigned char getStageCount() const { return model->getStageCount(); }
    const Model::WeldStage& getStage(unsigned char index) const { return model->getStage(index); }
    void setRecipePage(unsigned char value) { model->setRecipePage(value); }
    void setRecipeItem(unsigned char value) { model->setRecipeItem(value); }
    void setStageCount(unsigned char value) { model->setStageCount(value); }

private:
    Screen2Presenter();

    Screen2View& view;
};

#endif // SCREEN2PRESENTER_HPP
