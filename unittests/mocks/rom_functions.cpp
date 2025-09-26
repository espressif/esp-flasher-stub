#include <esp-stub-lib/rom_wrappers.h>
#include "support/LogMock.h"

LogMock& getUartLibLog()
{
  static LogMock obj;
  return obj;
}

#ifdef __cplusplus
extern "C" {
#endif

uint8_t stub_lib_uart_tx_one_char(uint8_t ch)
{
    getUartLibLog().log() << "stub_lib_uart_tx_one_char(char=0x" << std::hex << int(ch) << ")";
    getUartLibLog().saveLog();
    return 0;
}

#ifdef __cplusplus
}
#endif
