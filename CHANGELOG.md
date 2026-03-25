## v0.5.0 (2026-03-25)

### ✨ New Features

- **esp32-p4**: Support ECO7 (rev3.2) *(Radim Karniš - c8a372d)*

### 🔧 Code Refactoring

- remove esptool build dependency, use pyelftools directly *(Roland Dobai - 371cc6c)*


## v0.4.0 (2026-03-24)

### ✨ New Features

- add plugin support for loading optional features on request *(Roland Dobai - 966ce00)*


## v0.3.0 (2026-03-19)

### ✨ New Features

- **esp32c61**: Add large flash support *(Jaroslav Burian - 0cece56)*

### 📖 Documentation

- add initial documentation structure *(Roland Dobai - bc0066d)*


## v0.2.0 (2026-02-16)

### ✨ New Features

- **esp32-h4**: Support the ESP32-H4 target *(Radim Karniš - 7f8902e)*
- **commands**: Flash data after successful response is sent to increase throughput *(Jaroslav Burian - 9c57067)*
- **commands**: Start async erase while waiting for new data for flashing *(Jaroslav Burian - 2a17a6f)*
- **commands**: Calculate checksum of data to be flashed *(Jaroslav Burian - e2c4115)*
- **slip**: Add double buffering for data reception to improve throughput *(Jaroslav Burian - a4bb702)*
- **esp32p4**: Add a separate >=ECO5 target *(Radim Karniš - c0d3927)*
- **commands**: Add compressed flashing support *(Jaroslav Burian - 962d319)*
- **commands**: Add change baudrate support *(Jaroslav Burian - 3681f7d)*
- **commands**: Add flash reading support *(Jaroslav Burian - e33ba64)*
- **commands**: Add MD5 check support *(Jaroslav Burian - 1b4664a)*
- **usb-otg**: Add support for USB-OTG RX/TX *(Radim Karniš - 4114166)*
- **transport**: Add USB-Serial/JTAG and transport layer selection support *(Radim Karniš - fa785ba)*
- **slip**: Implement SLIP protocol and OHAI handshake *(Radim Karniš - a7c5c1b)*
- Update esp-stub-lib to incorporate latest features and fixes *(Jaroslav Burian - e4e025e)*
- Increase CPU frequency to speed up operations *(Jaroslav Burian - 8ef3138)*
- Disable watchdogs when USB-Serial-JTAG is used *(Jaroslav Burian - 0af8d1a)*
- Add basic flash support *(Jaroslav Burian - 281eb39)*
- Implement basic commands *(Jaroslav Safka - 2bdb7ec)*
- Add UART support for slip frame receive *(Jaroslav Burian - ae5f342)*
- Add slip functions for receive *(Jaroslav Burian - 7ca57aa)*
- add tools/install_all_chips.sh *(Jaroslav Safka - 7674d6b)*
- Add support for basic commands *(Jaroslav Burian - d2b4e94)*
- Add command handling loop *(Jaroslav Burian - 45e76a4)*

### 🐛 Bug Fixes

- **transport**: Flush USB-Serial/JTAG after each frame *(Jaroslav Burian - 6994b74)*
- **slip**: Allow reception into second buffer even if first buffer is complete *(Jaroslav Burian - 9e591da)*
- **slip**: Add volatile to variables called from interrupt to avoid sync issues *(Jaroslav Burian - c9822bb)*
- **commands**: Wait for erase completion before responding to ERASE_REGION command *(Jaroslav Burian - 4fcd6f7)*
- **commands**: Avoid reading issues of esptool by setting max_unacked_packets to 1 *(Jaroslav Burian - 84c48af)*
- **commands**: Do not expect second parameter of SPI_ATTACH command *(Jaroslav Burian - c3865d7)*
- **commands**: Do not include Adler-32 checksum in flash data remaining calculation *(Jaroslav Burian - 11d177c)*
- **usb-otg**: Handle chip resets triggered via RTS line *(Radim Karniš - 4028ada)*
- **commands**: Hard-code max in-flight packets to 2 for read flash command *(Jaroslav Burian - d01f11d)*
- **flash**: Allow erase over ROMs default flash size *(Jaroslav Burian - 5eb4c4f)*
- **commands**: Interpret correctly execute flag for MEM_END command *(Jaroslav Burian - 37bc574)*
- **load_ram**: Fix wrong MEM cmd opcodes *(Radim Karniš - af3dba6)*
- Erase bss section at the beginning *(Jaroslav Burian - e841b3f)*
- Account for checksum field in command header *(Jaroslav Burian - 8deb40c)*
- sending error responses (align with esptool) *(Jaroslav Safka - 0f45388)*
- make build_all_chips.sh current path independent *(Jaroslav Safka - 5a93b1a)*
- Use compiler flags globally so it applies to esp-stub-lib *(Jaroslav Burian - 86271db)*
- Reorder CMakeLists.txt to fix builds on MacOS *(Radim Karniš - eeb13e5)*

### 🔧 Code Refactoring

- **commands**: Remove code repetition and improve readability in command_handler *(Jaroslav Burian - cbd377e)*
- **commands**: Separate RAM and Flash flashing states to save RAM usage *(Jaroslav Burian - e089646)*
- **ld**: Reduce code duplication by defining common ld *(Radim Karniš - 3a0126a)*
- Use int for return values and typedefs for enums and structs *(Jaroslav Burian - d7967a1)*
- Use common place for build flags in CMakeLists *(Jaroslav Burian - f29923d)*


## v0.1.0 (2025-05-13)

### ✨ New Features

- add missing chip targets *(Roland Dobai - 9403482)*
- Produce JSON outputs from ELF binaries *(Roland Dobai - d4bc777)*
- Build with the esp-stub-lib *(Roland Dobai - baea82b)*
- Adopt cmake for project build system generation *(Roland Dobai - a812780)*
- add esp-stub-lib dependency *(Roland Dobai - 5fdad64)*
