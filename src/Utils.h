class Utils
{
public:
    static float Clamp(float Input, const float Low, const float High)
    {
        if (Input < Low)
        {
            Input = Low;
        }
        if (Input > High)
        {
            Input = High;
        }

        return Input;
    }
};