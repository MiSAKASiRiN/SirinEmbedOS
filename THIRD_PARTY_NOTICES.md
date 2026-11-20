# SirinEmbedOS 许可范围

SirinEmbedOS 自有代码（含应用示例和主机工具）采用 [Mozilla Public License 2.0](LICENSE)。以下第三方文件及基于供应商模板修改的文件沿用各自的条款。请随源码保留原有的版权、许可和免责声明。

| 组件或文件 | 适用条款与范围 |
| --- | --- |
| FreeRTOS 10.6.2 与 CMSIS-RTOS2 | 内核及其移植层采用 [MIT](ThirdParty/FreeRTOS/Source/LICENSE)。Arm 的 CMSIS 声明及 RTOS2 封装采用 [Apache-2.0](ThirdParty/FreeRTOS/CMSIS/LICENSE.txt)，包括位于内核目录下的 [CMSIS_RTOS_V2](ThirdParty/FreeRTOS/Source/CMSIS_RTOS_V2/)。 |
| CMSIS-DSP 1.7.0 | 随源码提供的头文件采用 [Apache-2.0](ThirdParty/CMSISDSP/LICENSE.txt)。版本、文件摘要和当前构建用途见[来源记录](ThirdParty/CMSISDSP/PROVENANCE.json)。 |
| lwIP 2.2.1 | 通用条款为 [BSD-3-Clause](ThirdParty/LwIP/COPYING)。快照也包含有单独文件声明的上游代码，例如 [PPP](ThirdParty/LwIP/src/netif/ppp/) 及其 PolarSSL 子集。分发完整源码时须保留这些声明。 |
| STM32CubeMP2 V1.3.0 | M33 目标引用安装包内的 CMSIS Core、STM32MP2 设备支持、HAL/LL 和 FreeRTOS ARM_CM33_NTZ port。M0+ 目标引用同一安装包的 CMSIS Core、设备头、启动汇编和系统初始化文件。组件版权和许可见原包的 `Package_license.md`、各组件许可证与源码声明。[ST 软件包](https://www.st.com/en/embedded-software/stm32cubemp2.html) |
| STM32H7 HAL、CMSIS Core 与 STM32H7 设备支持 | HAL 1.11.6 使用 [BSD-3-Clause 全文](Docs/Licenses/STM32H7xx-HAL-BSD-3-Clause.txt)。CMSIS Core 与设备包使用 [Apache-2.0](Board/Drivers/CMSIS/LICENSE.txt)。组件归属见 [STM32CubeH7 1.13.0 软件包许可清单](Docs/Licenses/STM32CubeH7-Package_license.txt)。 |
| STM32CubeMX 输出与板级模板 | `Board/Core/`、启动汇编及生成的板级构建文件保留 ST 模板或来源组件的条款。两个链接脚本均带有完整的 ST 三条款 BSD 声明，包括项目修改的 [SirinEmbedOS.ld](Board/Linker/SirinEmbedOS.ld)。生成工具的软件包条款见 [STM32CubeMX SLA0048](https://www.st.com/resource/en/license/SLA0048_STM32CubeMX.pdf)。 |
| USB Device 与派生适配 | AL94 I-CUBE-USBD-COMPOSITE 1.0.3 附有 [AL94 MIT 许可](ThirdParty/USBDevice/LICENSE-AL94.txt)。ST USB 文件头明确指定 [SLA0044](ThirdParty/USBDevice/LICENSE-ST.txt)，要求仅在 ST 处理器上或与其配合使用，并保留 ST 条款。相同声明也出现在 [usb_device_board.c](Board/Support/USB/usb_device_board.c) 和 [USB 服务](Source/Services/USB/) 的 `usbd_conf.h`、`usbd_desc.c/.h`、`usbd_cdc_acm_if.h` 中。该服务的 `AL94.I-CUBE-USBD-COMPOSITE_conf.h` 保留 ST 生成模板声明。 |
| Microchip MCP2518FD | [驱动实现](Source/Devices/MCP2518FD/drv_canfdspi_api.c) 和 [vendor 目录的三个头文件](Source/Devices/MCP2518FD/vendor/) 保留 Microchip 2016–2018 声明，限定与 Microchip 产品配合使用。[厂商发布页](https://www.microchip.com/en-us/development-tool/adm00576)提供 canfdspi API。上述四个文件的声明未明确授予向公众再分发源码的权利，公开分发这四个文件仍需核实相应授权。 |

对外分发固件时，应让接收者能取得固件中 MPL 文件及其修改的对应源码，并按所含第三方组件的二进制分发条款附上所需声明。移植到 N32、GD32 等其他厂商的 MCU 时，应重新核对所选后端，尤其是带有 ST 处理器限制的 USB 代码。
