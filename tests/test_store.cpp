#include "store.h"
#include <gtest/gtest.h>
#include <time.h>

TEST(NumberStoreTest, InsertValid) {
    NumberStore s;
    std::string msg;
    time_t now = 1000;
    EXPECT_TRUE(s.insert(42, now, msg));
    EXPECT_EQ(s.size(), 1u);
    EXPECT_NE(msg.find("OK Inserted 42"), std::string::npos);
}

TEST(NumberStoreTest, InsertDuplicate) {
    NumberStore s;
    std::string msg;
    time_t now = 1000;
    EXPECT_TRUE(s.insert(42, now, msg));
    EXPECT_FALSE(s.insert(42, now+1, msg));
    EXPECT_EQ(s.size(), 1u);
    EXPECT_NE(msg.find("Duplicate"), std::string::npos);
}

TEST(NumberStoreTest, DeleteValid) {
    NumberStore s;
    std::string msg;
    s.insert(42, 1234, msg);
    EXPECT_TRUE(s.erase(42, msg));
    EXPECT_EQ(s.size(), 0u);
    EXPECT_NE(msg.find("OK Deleted 42"), std::string::npos);
}

TEST(NumberStoreTest, DeleteMissing) {
    NumberStore s;
    std::string msg;
    EXPECT_FALSE(s.erase(99, msg));
    EXPECT_NE(msg.find("Not found"), std::string::npos);
}

TEST(NumberStoreTest, DeleteAll) {
    NumberStore s;
    std::string msg;
    s.insert(1, 100, msg);
    s.insert(2, 200, msg);
    s.eraseAll(msg);
    EXPECT_EQ(s.size(), 0u);
    EXPECT_NE(msg.find("DeletedAll"), std::string::npos);
}
