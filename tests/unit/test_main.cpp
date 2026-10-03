// GoogleTest entry point.
//
// Defined explicitly rather than linking gtest_main so that the test binary owns
// its own initialisation: sampling tests benefit from setting the process to a
// known state before the fixture runs.
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
