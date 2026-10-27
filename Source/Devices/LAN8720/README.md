# LAN8720A

本目录为 Ethernet 提供私有寄存器定义及身份/协商结果解码。依据 Microchip [DS00002165C](https://ww1.microchip.com/downloads/aemDocuments/documents/UNG/ProductDocuments/DataSheets/LAN8720A-LAN8720Ai-Data-Sheet-DS00002165.pdf)；ID1 为 `0x0007`，ID2 为 `0xc0fX`，末四位为 revision。BMSR 由 Ethernet worker 双读以清除 latch-low，再检查 link/auto-neg 完成；寄存器 31 指示实际 10/100、半/全双工。

MDIO、PE14、MCO 和 HAL 接线归 Board/Support/IO；设备没有单独任务或公开地址配置。应用通过 Ethernet 公共 API 读取副本，不直接操作 PHY：

```c
#include "SirinEmbedOS/IO/Ethernet/Ethernet.h"
bool lan_link_ready(void) {
    SRNOS_EthernetStatus status;
    return SRNOS_EthernetGetStatus(&status) &&
           status.state == SRNOS_ETHERNET_LINK_UP;
}
```

需先在启动配置中设置 `ethernet.enabled=true`。测试模型不能代替实际链路验证。
