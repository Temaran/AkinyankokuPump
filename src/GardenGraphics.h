using GardenFrame = const int[3][3];

constexpr GardenFrame GardenNotConnectedIcon =
{
    {1, 0, 1},
    {0, 1, 0},
    {1, 0, 1}
};

constexpr GardenFrame GardenErrorIcon =
{
    {1, 1, 1},
    {1, 0, 1},
    {1, 1, 1}
};

constexpr GardenFrame GardenNotWateringIcon =
{
    {1, 1, 1},
    {1, 0, 1},
    {1, 1, 1}
};

constexpr int GardenWateringAnimLength = 8;
constexpr GardenFrame GardenWateringAnim[] =
{
    {
        {0, 1, 0},
        {0, 1, 0},
        {0, 0, 0}
    },
    {
        {0, 0, 1},
        {0, 1, 0},
        {0, 0, 0}
    },
    {
        {0, 0, 0},
        {0, 1, 1},
        {0, 0, 0}
    },
    {
        {0, 0, 0},
        {0, 1, 0},
        {0, 0, 1}
    },
    {
        {0, 0, 0},
        {0, 1, 0},
        {0, 1, 0}
    },
    {
        {0, 0, 0},
        {0, 1, 0},
        {1, 0, 0}
    },
    {
        {0, 0, 0},
        {1, 1, 0},
        {0, 0, 0}
    },
    {
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 0}
    },
};
