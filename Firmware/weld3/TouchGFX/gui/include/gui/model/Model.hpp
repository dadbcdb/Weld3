#ifndef MODEL_HPP
#define MODEL_HPP

class ModelListener;

class Model
{
public:
    struct WeldStage
    {
        unsigned short current;
        unsigned short up;
        unsigned short duration;
        unsigned short down;
        unsigned short cool;
    };

    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    void tick();

    unsigned char getRecipePage() const { return recipePage; }
    unsigned char getRecipeItem() const { return recipeItem; }
    unsigned char getStageCount() const { return stageCount; }
    bool isWeldResultAvailable() const { return weldResultAvailable; }
    const WeldStage& getStage(unsigned char index) const { return stages[index]; }
    unsigned short getSqueeze() const { return squeeze; }

    void setRecipePage(unsigned char value) { recipePage = value; weldResultAvailable = false; }
    void setRecipeItem(unsigned char value) { recipeItem = value; weldResultAvailable = false; }
    void setStageCount(unsigned char value) { stageCount = value >= 3 ? 3 : (value <= 1 ? 1 : 2); weldResultAvailable = false; }
    void setWeldResultAvailable(bool value) { weldResultAvailable = value; }
protected:
    ModelListener* modelListener;
    unsigned char recipePage;
    unsigned char recipeItem;
    unsigned char stageCount;
    bool weldResultAvailable;
    unsigned short squeeze;
    WeldStage stages[3];
};

#endif // MODEL_HPP
