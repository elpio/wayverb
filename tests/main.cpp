#include "glog/logging.h"
#include "gtest/gtest.h"

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);

    ::testing::InitGoogleTest(&argc, argv);
//    ::testing::GTEST_FLAG(filter) = "*raytrace*";
    return RUN_ALL_TESTS();
}
