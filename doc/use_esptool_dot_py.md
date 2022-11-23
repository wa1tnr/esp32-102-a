Using esptool.py at the command line

23 November 2022

 - - -   snip!   - - -

 $  python3  ./esptool.py  --chip esp32 --port  /dev/ttyACM0 erase_flash
esptool.py v4.2.1
Serial port /dev/ttyACM0
Connecting....
Chip is ESP32-PICO-V3-02 (revision 3)
Features: WiFi, BT, Dual Core, 240MHz, Embedded Flash, Embedded PSRAM, VRef calibration in efuse, Coding Scheme None
Crystal is 40MHz
MAC: e8:9f:6d:22:0d:28
Uploading stub...
Running stub...
Stub running...
Erasing flash (this may take a while)...
Chip erase completed successfully in 8.0s
Hard resetting via RTS pin...

 - - -   snip!   - - -
 $ /bin/pwd
/some/path/to/.platformio/packages/tool-esptoolpy

END.
