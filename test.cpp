#include <cmath>
#include <iostream>
#include <random>

using namespace std;

typedef double r64;

std::pair<r64, r64> WilsonScoreInterval(r64 total_value, r64 variance, int total) {
    constexpr r64 confidence = 0.95;

    r64 mean = total_value / total;

    r64 standard_deviation = sqrt(variance);

    // r64 variance = ((r64)(success + failure) / total) * (1.0 - ((r64)(success - failure) / total));

    r64 z = -1.0 * std::erf(-1 * (confidence / sqrt(2)));

    r64 interval = z * (standard_deviation / sqrt(total));

    return { mean - interval, mean + interval };
}

void UpdateVariance(r64 *variance, int count, r64 *mean, r64 value)
{
    r64 delta = value - *mean;
    *mean += delta / count;
    *variance += delta * (value - *mean);
    if (count == 1)
    {
        *variance = 0.0;
    }
    else
    {
        *variance /= (count - 1);
    }
}

int main()
{
    srand(42);
    int total_playouts = 30;
    r64 variance = 0.0;
    r64 mean = 0.0;
    r64 total_value = 0.0;
    for (int playout_counter = 0; playout_counter < total_playouts; ++playout_counter)
    {
        r64 outcome = (r64)((rand() % 3) - 1);
        if (playout_counter == total_playouts / 3)
        {
            outcome = 1000.0;
        }
        total_value += outcome;
        UpdateVariance(&variance, playout_counter + 1, &mean, outcome);
        pair<r64, r64> interval = WilsonScoreInterval(total_value, variance, playout_counter + 1);
        cout << "outcome: " << outcome << ", variance: " << variance << ", mean: " << mean << ", interval: [" << interval.first << ", " << interval.second << "], interval width: " << interval.second - interval.first << endl;
    }
    // for (int success = 0; success <= total_playouts; ++success)
    // {
    //     int total = total_playouts;
    //     int failure = total - success;
    //     pair<r64, r64> interval = WilsonScoreInterval(success, failure, total);
    //     cout << interval.first << ", " << interval.second << endl;
    // }
}
