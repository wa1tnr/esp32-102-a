# WiFi

##  main.cpp

```
     6	main.cpp:954:#  include <WiFi.h>
     7	main.cpp:955:#  include <WiFiClient.h>

     9	main.cpp:972:  /* WiFi */ \
    11	main.cpp:974:      WiFi.config(ToIP(n3))
    12	main.cpp:975:      WiFi.begin(c1, c0)
    13	main.cpp:976:      WiFi.disconnect()
    14	main.cpp:977:      WiFi.status()
    15	main.cpp:978:      WiFi.macAddress(b0)
    16	main.cpp:979:      WiFi.localIP()
    17	main.cpp:980:      WiFi.mode(wifi_mode_t)
    18	main.cpp:981:      WiFi.setTxPower(wifi_power_t)
    19	main.cpp:982:      WiFi.getTxPower()
    20	main.cpp:983:      WiFi.softAP(c1, c0)
    21	main.cpp:984:      WiFi.softAPIP()
    22	main.cpp:985:      WiFi.softAPBroadcastIP()
    23	main.cpp:986:      WiFi.softAPNetworkID()
    24	main.cpp:987:      WiFi.softAPConfig(ToIP(n2))
    25	main.cpp:988:      WiFi.softAPdisconnect(n0)
    26	main.cpp:989:      WiFi.softAPgetStationNum()

    29	main.cpp:1724:( WiFi Modes )
    30	main.cpp:1725:0 constant WIFI_MODE_NULL
    31	main.cpp:1726:1 constant WIFI_MODE_STA
    32	main.cpp:1727:2 constant WIFI_MODE_AP
    33	main.cpp:1728:3 constant WIFI_MODE_APSTA

    35	main.cpp:3548:   WIFI_MODE_STA Wifi.mode
    36	main.cpp:3549:   WiFi.begin begin WiFi.localIP 0= while 100 ms repeat WiFi.localIP ip. cr
```

END.
