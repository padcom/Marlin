/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/**
 * Software SPI functions originally from Arduino Sd2Card Library
 * Copyright (c) 2009 by William Greiman
 */

/**
 * For TARGET_LPC1768
 */

/**
 * Hardware SPI and Software SPI implementations are included in this file.
 * The hardware SPI runs faster and has higher throughput but is not compatible
 * with some LCD interfaces/adapters.
 *
 * Control of the slave select pin(s) is handled by the calling routines.
 *
 * Some of the LCD interfaces/adapters result in the LCD SPI and the SD card
 * SPI sharing pins. The SCK, MOSI & MISO pins can NOT be set/cleared with
 * WRITE nor digitalWrite when the hardware SPI module within the LPC17xx is
 * active.  If any of these pins are shared then the software SPI must be used.
 *
 * A more sophisticated hardware SPI can be found at the following link.  This
 * implementation has not been fully debugged.
 * https://github.com/MarlinFirmware/Marlin/tree/071c7a78f27078fd4aee9a3ef365fcf5e143531e
 */

#ifdef TARGET_LPC1768

#include "../../inc/MarlinConfig.h"
#include <SPI.h>

// Hardware SPI and SPIClass
#include <lpc17xx_pinsel.h>
#include <lpc17xx_clkpwr.h>

// ------------------------
// Public functions
// ------------------------
#if ENABLED(LPC_SOFTWARE_SPI)

  #include <SoftwareSPI.h>

  // Software SPI

  static uint8_t SPI_speed = 0;

  static uint8_t spiTransfer(uint8_t b) {
    return swSpiTransfer(b, SPI_speed, SCK_PIN, MISO_PIN, MOSI_PIN);
  }

  void spiBegin() {
    swSpiBegin(SCK_PIN, MISO_PIN, MOSI_PIN);
  }

  void spiInit(uint8_t spiRate) {
    SPI_speed = swSpiInit(spiRate, SCK_PIN, MOSI_PIN);
  }

  uint8_t spiRec() { return spiTransfer(0xFF); }

  void spiRead(uint8_t*buf, uint16_t nbyte) {
    for (int i = 0; i < nbyte; i++)
      buf[i] = spiTransfer(0xFF);
  }

  void spiSend(uint8_t b) { (void)spiTransfer(b); }

  void spiSend(const uint8_t* buf, size_t nbyte) {
    for (uint16_t i = 0; i < nbyte; i++)
      (void)spiTransfer(buf[i]);
  }

  void spiSendBlock(uint8_t token, const uint8_t* buf) {
    (void)spiTransfer(token);
    for (uint16_t i = 0; i < 512; i++)
      (void)spiTransfer(buf[i]);
  }

#else

  // decide which HW SPI device to use
  #ifndef LPC_HW_SPI_DEV
    #if (SCK_PIN == P0_07 && MISO_PIN == P0_08 && MOSI_PIN == P0_09)
      #define LPC_HW_SPI_DEV 1
    #else
      #if (SCK_PIN == P0_15 && MISO_PIN == P0_17 && MOSI_PIN == P0_18)
        #define LPC_HW_SPI_DEV 0
      #else
        #error "Invalid pins selected for hardware SPI"
      #endif
    #endif
  #endif
  #if LPC_HW_SPI_DEV == 0
    #define LPC_SSPn LPC_SSP0
  #else
    #define LPC_SSPn LPC_SSP1
  #endif

  void spiBegin() {  // setup SCK, MOSI & MISO pins for SSP0
    PINSEL_CFG_Type PinCfg;  // data structure to hold init values
    PinCfg.Funcnum = 2;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = LPC176x::pin_bit(SCK_PIN);
    PinCfg.Portnum = LPC176x::pin_port(SCK_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_OUTPUT(SCK_PIN);

    PinCfg.Pinnum = LPC176x::pin_bit(MISO_PIN);
    PinCfg.Portnum = LPC176x::pin_port(MISO_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_INPUT(MISO_PIN);

    PinCfg.Pinnum = LPC176x::pin_bit(MOSI_PIN);
    PinCfg.Portnum = LPC176x::pin_port(MOSI_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_OUTPUT(MOSI_PIN);
    // divide PCLK by 2 for SSP0
    CLKPWR_SetPCLKDiv(LPC_HW_SPI_DEV == 0 ? CLKPWR_PCLKSEL_SSP0 : CLKPWR_PCLKSEL_SSP1, CLKPWR_PCLKSEL_CCLK_DIV_2);
    spiInit(0);
    SSP_Cmd(LPC_SSPn, ENABLE);  // start SSP running
  }

  void spiInit(uint8_t spiRate) {
    // table to convert Marlin spiRates (0-5 plus default) into bit rates
    uint32_t Marlin_speed[7]; // CPSR is always 2
    Marlin_speed[0] = 8333333; //(SCR:  2)  desired: 8,000,000  actual: 8,333,333  +4.2%  SPI_FULL_SPEED
    Marlin_speed[1] = 4166667; //(SCR:  5)  desired: 4,000,000  actual: 4,166,667  +4.2%  SPI_HALF_SPEED
    Marlin_speed[2] = 2083333; //(SCR: 11)  desired: 2,000,000  actual: 2,083,333  +4.2%  SPI_QUARTER_SPEED
    Marlin_speed[3] = 1000000; //(SCR: 24)  desired: 1,000,000  actual: 1,000,000         SPI_EIGHTH_SPEED
    Marlin_speed[4] =  500000; //(SCR: 49)  desired:   500,000  actual:   500,000         SPI_SPEED_5
    Marlin_speed[5] =  250000; //(SCR: 99)  desired:   250,000  actual:   250,000         SPI_SPEED_6
    Marlin_speed[6] =  125000; //(SCR:199)  desired:   125,000  actual:   125,000         Default from HAL.h
    // setup for SPI mode
    SSP_CFG_Type HW_SPI_init; // data structure to hold init values
    SSP_ConfigStructInit(&HW_SPI_init);  // set values for SPI mode
    HW_SPI_init.ClockRate = Marlin_speed[_MIN(spiRate, 6)]; // put in the specified bit rate
    HW_SPI_init.Mode |= SSP_CR1_SSP_EN;
    SSP_Init(LPC_SSPn, &HW_SPI_init);  // puts the values into the proper bits in the SSP0 registers
  }

  static uint8_t doio(uint8_t b) {
    /* send and receive a single byte */
    SSP_SendData(LPC_SSPn, b & 0x00FF);
    while (SSP_GetStatus(LPC_SSPn, SSP_STAT_BUSY));  // wait for it to finish
    return SSP_ReceiveData(LPC_SSPn) & 0x00FF;
  }

  void spiSend(uint8_t b) { doio(b); }

  void spiSend(const uint8_t* buf, size_t nbyte) {
    for (uint16_t i = 0; i < nbyte; i++) doio(buf[i]);
  }

  void spiSend(uint32_t chan, byte b) {
  }

  void spiSend(uint32_t chan, const uint8_t* buf, size_t nbyte) {
  }

  // Read single byte from SPI
  uint8_t spiRec() { return doio(0xFF); }

  uint8_t spiRec(uint32_t chan) { return 0; }

  // Read from SPI into buffer
  void spiRead(uint8_t *buf, uint16_t nbyte) {
    for (uint16_t i = 0; i < nbyte; i++) buf[i] = doio(0xFF);
  }

  uint8_t spiTransfer(uint8_t b) {
    return doio(b);
  }

  // Write from buffer to SPI
  void spiSendBlock(uint8_t token, const uint8_t* buf) {
   (void)spiTransfer(token);
    for (uint16_t i = 0; i < 512; i++)
      (void)spiTransfer(buf[i]);
  }

  /** Begin SPI transaction, set clock, bit order, data mode */
  void spiBeginTransaction(uint32_t spiClock, uint8_t bitOrder, uint8_t dataMode) {
    // TODO: to be implemented

  }

#endif // LPC_SOFTWARE_SPI

/**
 * @brief Wait until TXE (tx empty) flag is set and BSY (busy) flag unset.
 */
static inline void waitSpiTxEnd(LPC_SSP_TypeDef *spi_d) {
  while (SSP_GetStatus(spi_d, SSP_STAT_TXFIFO_EMPTY) == RESET) { /* nada */ } // wait until TXE=1
  while (SSP_GetStatus(spi_d, SSP_STAT_BUSY) == SET) { /* nada */ }     // wait until BSY=0
}

SPIClass::SPIClass(uint8_t device) {
  // Init things specific to each SPI device
  // clock divider setup is a bit of hack, and needs to be improved at a later date.

  PINSEL_CFG_Type PinCfg;  // data structure to hold init values
  #if BOARD_NR_SPI >= 1
    _settings[0].spi_d = LPC_SSP0;
    // _settings[0].clockDivider = determine_baud_rate(_settings[0].spi_d, _settings[0].clock);
    PinCfg.Funcnum = 2;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = LPC176x::pin_bit(BOARD_SPI1_SCK_PIN);
    PinCfg.Portnum = LPC176x::pin_port(BOARD_SPI1_SCK_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_OUTPUT(BOARD_SPI1_SCK_PIN);

    PinCfg.Pinnum = LPC176x::pin_bit(BOARD_SPI1_MISO_PIN);
    PinCfg.Portnum = LPC176x::pin_port(BOARD_SPI1_MISO_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_INPUT(BOARD_SPI1_MISO_PIN);

    PinCfg.Pinnum = LPC176x::pin_bit(BOARD_SPI1_MOSI_PIN);
    PinCfg.Portnum = LPC176x::pin_port(BOARD_SPI1_MOSI_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_OUTPUT(BOARD_SPI1_MOSI_PIN);
  #endif

  #if BOARD_NR_SPI >= 2
    _settings[1].spi_d = LPC_SSP1;
    // _settings[1].clockDivider = determine_baud_rate(_settings[1].spi_d, _settings[1].clock);
    PinCfg.Funcnum = 2;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = LPC176x::pin_bit(BOARD_SPI2_SCK_PIN);
    PinCfg.Portnum = LPC176x::pin_port(BOARD_SPI2_SCK_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_OUTPUT(BOARD_SPI2_SCK_PIN);

    PinCfg.Pinnum = LPC176x::pin_bit(BOARD_SPI2_MISO_PIN);
    PinCfg.Portnum = LPC176x::pin_port(BOARD_SPI2_MISO_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_INPUT(BOARD_SPI2_MISO_PIN);

    PinCfg.Pinnum = LPC176x::pin_bit(BOARD_SPI2_MOSI_PIN);
    PinCfg.Portnum = LPC176x::pin_port(BOARD_SPI2_MOSI_PIN);
    PINSEL_ConfigPin(&PinCfg);
    SET_OUTPUT(BOARD_SPI2_MOSI_PIN);
  #endif

  setModule(device);

  /* Initialize GPDMA controller */
  //TODO: call once in the constructor? or each time?
  GPDMA_Init();
}

void SPIClass::begin() {
  updateSettings();
  SSP_Cmd(_currentSetting->spi_d, ENABLE);  // start SSP running
}

void SPIClass::beginTransaction(const SPISettings &cfg) {
  setBitOrder(cfg.bitOrder);
  setDataMode(cfg.dataMode);
  setDataSize(cfg.dataSize);
  //setClockDivider(determine_baud_rate(_currentSetting->spi_d, settings.clock));
  begin();
}

uint8_t SPIClass::transfer(const uint16_t b) {
  /* send and receive a single byte */
  SSP_ReceiveData(_currentSetting->spi_d); // read any previous data
  SSP_SendData(_currentSetting->spi_d, b);
  waitSpiTxEnd(_currentSetting->spi_d);  // wait for it to finish
  return SSP_ReceiveData(_currentSetting->spi_d);
}

uint16_t SPIClass::transfer16(const uint16_t data) {
  return (transfer((data >> 8) & 0xFF) << 8)
       | (transfer(data & 0xFF) & 0xFF);
}

void SPIClass::end() {
  // SSP_Cmd(_currentSetting->spi_d, DISABLE);  // stop device or SSP_DeInit?
  SSP_DeInit(_currentSetting->spi_d);
}

void SPIClass::send(uint8_t data) {
  SSP_SendData(_currentSetting->spi_d, data);
}

void SPIClass::dmaSend(void *buf, uint16_t length, bool minc) {
  //TODO: LPC dma can only write 0xFFF bytes at once.
  GPDMA_Channel_CFG_Type GPDMACfg;

  /* Configure GPDMA channel 0 -------------------------------------------------------------*/
  /* DMA Channel 0 */
  GPDMACfg.ChannelNum = 0;
  // Source memory
  GPDMACfg.SrcMemAddr = (uint32_t)buf;
  // Destination memory - Not used
  GPDMACfg.DstMemAddr = 0;
  // Transfer size
  GPDMACfg.TransferSize = (minc ? length : 1);
  // Transfer width
  GPDMACfg.TransferWidth = (_currentSetting->dataSize == DATA_SIZE_16BIT) ? GPDMA_WIDTH_HALFWORD : GPDMA_WIDTH_BYTE;
  // Transfer type
  GPDMACfg.TransferType = GPDMA_TRANSFERTYPE_M2P;
  // Source connection - unused
  GPDMACfg.SrcConn = 0;
  // Destination connection
  GPDMACfg.DstConn = (_currentSetting->spi_d == LPC_SSP0) ? GPDMA_CONN_SSP0_Tx : GPDMA_CONN_SSP1_Tx;

  GPDMACfg.DMALLI = 0;

  // Enable dma on SPI
  SSP_DMACmd(_currentSetting->spi_d, SSP_DMA_TX, ENABLE);

  // if minc=false, I'm repeating the same byte 'length' times, as I could not find yet how do GPDMA without memory increment
  do {
    // Setup channel with given parameter
    GPDMA_Setup(&GPDMACfg);

    // enabled dma
    GPDMA_ChannelCmd(0, ENABLE);

    // wait data transfer
    while (!GPDMA_IntGetStatus(GPDMA_STAT_INTTC, 0) && !GPDMA_IntGetStatus(GPDMA_STAT_INTERR, 0)) { }

    // clear err and int
    GPDMA_ClearIntPending (GPDMA_STATCLR_INTTC, 0);
    GPDMA_ClearIntPending (GPDMA_STATCLR_INTERR, 0);

    // dma disable
    GPDMA_ChannelCmd(0, DISABLE);

    --length;
  } while (!minc && length > 0);

  waitSpiTxEnd(_currentSetting->spi_d);

  SSP_DMACmd(_currentSetting->spi_d, SSP_DMA_TX, DISABLE);
}

uint16_t SPIClass::read() {
  return SSP_ReceiveData(_currentSetting->spi_d);
}

void SPIClass::read(uint8_t *buf, uint32_t len) {
  for (uint16_t i = 0; i < len; i++) buf[i] = transfer(0xFF);
}

void SPIClass::setClock(uint32_t clock) {
  _currentSetting->clock = clock;
}

void SPIClass::setModule(uint8_t device) {
  _currentSetting = &_settings[device - 1];// SPI channels are called 1 2 and 3 but the array is zero indexed
}

void SPIClass::setBitOrder(uint8_t bitOrder) {
  _currentSetting->bitOrder = bitOrder;
}

void SPIClass::setDataMode(uint8_t dataMode) {
  _currentSetting->dataSize = dataMode;
}

void SPIClass::setDataSize(uint32_t ds) {
  _currentSetting->dataSize = ds;
}

/**
 * Set up/tear down
 */
void SPIClass::updateSettings() {
  //SSP_DeInit(_currentSetting->spi_d); //todo: need force de init?!

  // divide PCLK by 2 for SSP0
  CLKPWR_SetPCLKDiv(_currentSetting->spi_d == LPC_SSP0 ? CLKPWR_PCLKSEL_SSP0 : CLKPWR_PCLKSEL_SSP1, CLKPWR_PCLKSEL_CCLK_DIV_2);

  SSP_CFG_Type HW_SPI_init; // data structure to hold init values
  SSP_ConfigStructInit(&HW_SPI_init);  // set values for SPI mode
  HW_SPI_init.ClockRate = _currentSetting->clock;
  HW_SPI_init.Databit = _currentSetting->dataSize;

  /**
   * SPI Mode  CPOL  CPHA  Shift SCK-edge  Capture SCK-edge
   * 0       0     0     Falling     Rising
   * 1       0     1     Rising      Falling
   * 2       1     0     Rising      Falling
   * 3       1     1     Falling     Rising
   */
  switch (_currentSetting->dataMode) {
    case SPI_MODE0:
      HW_SPI_init.CPHA = SSP_CPHA_FIRST;
	    HW_SPI_init.CPOL = SSP_CPOL_HI;
      break;
    case SPI_MODE1:
      HW_SPI_init.CPHA = SSP_CPHA_SECOND;
	    HW_SPI_init.CPOL = SSP_CPOL_HI;
      break;
    case SPI_MODE2:
      HW_SPI_init.CPHA = SSP_CPHA_FIRST;
	    HW_SPI_init.CPOL = SSP_CPOL_LO;
      break;
    case SPI_MODE3:
      HW_SPI_init.CPHA = SSP_CPHA_SECOND;
	    HW_SPI_init.CPOL = SSP_CPOL_LO;
      break;
    default:
      break;
  }

  // TODO: handle bitOrder
  SSP_Init(_currentSetting->spi_d, &HW_SPI_init);  // puts the values into the proper bits in the SSP0 registers
}

#if MISO_PIN == BOARD_SPI1_MISO_PIN
  SPIClass SPI(1);
#elif MISO_PIN == BOARD_SPI2_MISO_PIN
  SPIClass SPI(2);
#endif

#endif // TARGET_LPC1768
