// w5500_mode_patch.h
// Patch SPI mode pentru module W5500 clona care raspund pe SPI_MODE1.
// Include PRIMUL inainte de <Ethernet.h>.
#pragma once
#undef SPI_ETHERNET_SETTINGS
#define SPI_ETHERNET_SETTINGS SPISettings(4000000, MSBFIRST, SPI_MODE1)
