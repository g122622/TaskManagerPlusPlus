// Measures how long each hardware probe takes.
//
// The sampler runs these on its own thread at a fixed interval, so a probe slower than the interval
// does not merely delay one figure: it makes the whole page late and the interval meaningless. A
// timing test is the only way to see that, because a slow probe still returns correct values.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>

#include "Platform/Windows/HardwareCounterProbe.h"

namespace tmpp::platform::test
{
    namespace
    {
        [[nodiscard]] double _millisSince(std::chrono::steady_clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    }

    TEST(HardwareProbeTiming, EachReadIsFasterThanTheSamplingInterval)
    {
        constexpr double INTERVAL_MS = 1000.0;

        HardwareCounterProbe probe;

        // The first call of each may initialise a subsystem, so it is measured separately and not
        // held against the steady-state figure.
        auto const diskFirstStart = std::chrono::steady_clock::now();
        probe.ReadDisks();
        double const diskFirst = _millisSince(diskFirstStart);

        auto const netFirstStart = std::chrono::steady_clock::now();
        probe.ReadNetwork();
        double const netFirst = _millisSince(netFirstStart);

        auto const gpuFirstStart = std::chrono::steady_clock::now();
        probe.ReadGpu();
        double const gpuFirst = _millisSince(gpuFirstStart);

        std::printf("\n--- probe timings (ms) ---\n");
        std::printf("disk   first %8.2f\n", diskFirst);
        std::printf("network first %8.2f\n", netFirst);
        std::printf("gpu    first %8.2f\n", gpuFirst);

        double diskTotal = 0.0;
        double networkTotal = 0.0;
        double gpuTotal = 0.0;
        constexpr int ROUNDS = 10;

        for (int i = 0; i < ROUNDS; ++i)
        {
            auto const diskStart = std::chrono::steady_clock::now();
            probe.ReadDisks();
            diskTotal += _millisSince(diskStart);

            auto const netStart = std::chrono::steady_clock::now();
            probe.ReadNetwork();
            networkTotal += _millisSince(netStart);

            auto const gpuStart = std::chrono::steady_clock::now();
            probe.ReadGpu();
            gpuTotal += _millisSince(gpuStart);
        }

        double const diskMean = diskTotal / ROUNDS;
        double const networkMean = networkTotal / ROUNDS;
        double const gpuMean = gpuTotal / ROUNDS;

        std::printf("disk   mean  %8.2f\n", diskMean);
        std::printf("network mean %8.2f\n", networkMean);
        std::printf("gpu    mean  %8.2f\n", gpuMean);
        std::printf("total  mean  %8.2f  (interval is %.0f)\n", diskMean + networkMean + gpuMean, INTERVAL_MS);

        // The whole round must fit inside the interval with room to spare. A probe that eats the
        // interval makes every other figure late, which is what "sampling became slow" looks like
        // from outside.
        EXPECT_LT(diskMean + networkMean + gpuMean, INTERVAL_MS / 2.0)
            << "the hardware probes must not consume half the sampling interval";
    }
}
