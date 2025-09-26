#include <gtest/gtest.h>
#include "slip.h"

#include "support/LogMock.h"


extern LogMock& getUartLibLog();

class SlipProtocolTest: public ::testing::Test {
protected:

    SlipProtocolTest()
      : ::testing::Test(),
        m_mock{getUartLibLog()}
    {
    }

    virtual ~SlipProtocolTest() = default;

    void SetUp() override
    {
        m_mock.clearLogs();
        m_mock.clearData();
    }

    LogMock& m_mock;
};


TEST_F(SlipProtocolTest, sendFrame)
{
    slip_send_frame_data(0x55);

    EXPECT_EQ(m_mock.popRecord(), "stub_lib_uart_tx_one_char(char=0x55)");
    EXPECT_EQ(m_mock.popRecord(), "");
}

TEST_F(SlipProtocolTest, sendFrameEndEsc)
{
    slip_send_frame_data(0xC0);

    EXPECT_EQ(m_mock.popRecord(), "stub_lib_uart_tx_one_char(char=0xdb)");
    EXPECT_EQ(m_mock.popRecord(), "stub_lib_uart_tx_one_char(char=0xdc)");
    EXPECT_EQ(m_mock.popRecord(), "");
}

TEST_F(SlipProtocolTest, sendFrameEscEsc)
{
    slip_send_frame_data(0xDB);

    EXPECT_EQ(m_mock.popRecord(), "stub_lib_uart_tx_one_char(char=0xdb)");
    EXPECT_EQ(m_mock.popRecord(), "stub_lib_uart_tx_one_char(char=0xdd)");
    EXPECT_EQ(m_mock.popRecord(), "");
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
