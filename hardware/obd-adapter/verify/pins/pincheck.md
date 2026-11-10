# STM32G474RET6 pin check vs ST open pin data (STM32G474R(B-C-E)Tx.xml, GPIO-STM32G47x_gpio_v1_0)

```
symbol pin names vs ST: OK

net -> pin -> required signal
      1 VBAT             3V3
      2 PC13             PANIC_BTN
      3 PC14-OSC32_IN    ACC_INT1
      4 PC15-OSC32_OUT   ACC_INT2
OK    5 PF0-OSC_IN       HSE_IN       needs RCC_OSC_IN           AF-
OK    6 PF1-OSC_OUT      HSE_OUT      needs RCC_OSC_OUT          AF-
      7 PG10-NRST        NRST
OK    8 PC0              KLINE_RX     needs LPUART1_RX           AFAF8
OK    9 PC1              KLINE_TX     needs LPUART1_TX           AFAF8
     10 PC2              LLINE_TX
     11 PC3              DLC_SW_EN
OK   12 PA0              ESP_RTS      needs USART2_CTS           AFAF7
OK   13 PA1              ESP_CTS_M    needs USART2_RTS           AFAF7
OK   14 PA2              MCU_TX_ESP_M needs USART2_TX            AFAF7
     15 VSS              GND
     16 VDD              3V3
OK   17 PA3              ESP_TX_MCU   needs USART2_RX            AFAF7
OK   18 PA4              VBAT_SENSE   needs ADC2_IN17            AF-
     19 PA5              EXP_PA5
OK   20 PA6              QSPI_IO3     needs QUADSPI1_BK1_IO3     AFAF10
OK   21 PA7              QSPI_IO2     needs QUADSPI1_BK1_IO2     AFAF10
OK   22 PC4              EXP_TX       needs USART1_TX            AFAF7
OK   23 PC5              EXP_RX       needs USART1_RX            AFAF7
OK   24 PB0              QSPI_IO1     needs QUADSPI1_BK1_IO1     AFAF10
OK   25 PB1              QSPI_IO0     needs QUADSPI1_BK1_IO0     AFAF10
     26 PB2              J1850_VSEL
     27 VSSA             GND
     28 VREF+            3V3
     29 VDDA             3V3
OK   30 PB10             QSPI_CLK     needs QUADSPI1_CLK         AFAF10
     31 VSS              GND
     32 VDD              3V3
OK   33 PB11             QSPI_NCS     needs QUADSPI1_BK1_NCS     AFAF10
OK   34 PB12             CAN1_RXD     needs FDCAN2_RX            AFAF9
OK   35 PB13             CAN1_TXD     needs FDCAN2_TX            AFAF9
     36 PB14             CAN1_STB
     37 PB15             CAN2_STB
OK   38 PC6              VPW_RX       needs TIM3_CH1             AFAF2
OK   39 PC7              PWM_RX       needs TIM3_CH2             AFAF2
OK   40 PC8              J1850_TXP    needs TIM3_CH3             AFAF2
OK   41 PC9              J1850_TXN    needs TIM3_CH4             AFAF2
OK   42 PA8              I2C_SDA      needs I2C2_SDA             AFAF4
OK   43 PA9              I2C_SCL      needs I2C2_SCL             AFAF4
     44 PA10             EXP_PA10
OK   45 PA11             USB_DM       needs USB_DM               AF-
OK   46 PA12             USB_DP       needs USB_DP               AF-
     47 VSS              GND
     48 VDD              3V3
OK   49 PA13             SWDIO        needs SYS_JTMS-SWDIO       AFAF0
OK   50 PA14             SWCLK        needs SYS_JTCK-SWCLK       AFAF0
     51 PA15             BTN_USER
     52 PC10             EXP_PC10
     53 PC11             LED_G
     54 PC12             LED_B
     55 PD2              EXP_PD2
OK   56 PB3              CAN2_RXD     needs FDCAN3_RX            AFAF11
OK   57 PB4              CAN2_TXD     needs FDCAN3_TX            AFAF11
     58 PB5              ESP_IO0_M
     59 PB6              ESP_PWR_EN
     60 PB7              ESP_EN_M
     61 PB8-BOOT0        BOOT0
     62 PB9              LED_R
     63 VSS              GND
     64 VDD              3V3
```

Notes:
- PB3 resets as JTDO/SWO and PB4 as NJTRST (pull-up). TDO is tri-stated when the TAP is not shifting, and SWD never shifts JTAG, so there is no fight with TCAN RXD on PB3. PB4 (CAN2 TXD) idles high from the NJTRST pull-up, and the transceiver is in standby (STB pulled high) until firmware runs.
- PA15 resets as JTDI with a pull-up, which suits a button to GND: since 2026-11-09 that pull-up is the USER button's only one (R2 is gone), so firmware keeps it on when it takes the pin as a GPIO.
- PC0 (KLINE_RX) has no resistor either (R60 is gone): the LM2901 output is open collector, so firmware turns on PC0's pull-up before it enables LPUART1.
- PC6 / PC7 (VPW_RX / PWM_RX) keep their 10k pull-ups (R47 / R50). They are the ROM bootloader's I2C4 pins (AN2606), and the LM2901 that drives them has no supply while `DLC_SW` is off, so without the resistors they would float during a DFU session.
- Re-run on 2026-11-09 after the part reduction (`verify/REVIEW.md` #54): the only change is VDDA / VREF+ on 3V3.
