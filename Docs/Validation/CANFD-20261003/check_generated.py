from pathlib import Path
import re
before=Path('build/cubemx/20261003-005600-fzxuj303/Board-before'); board=Path('Board')
count=0
for src in before.rglob('*'):
    if src.suffix not in ('.c','.h','.s'): continue
    regex=rb'/\* USER CODE BEGIN (.*?) \*/(.*?)/\* USER CODE END \1 \*/'
    old=re.findall(regex,src.read_bytes(),re.S)
    assert old==re.findall(regex,(board/src.relative_to(before)).read_bytes(),re.S),src
    count+=len(old)
print('PASS:',count,'USER CODE blocks preserved')
for group in ('Drivers','cmake','Linker'):
    for src in (before/group).rglob('*'):
        if src.is_file():assert src.read_bytes()==(board/src.relative_to(before)).read_bytes(),src
    print('PASS:',group,'unchanged from generation snapshot')
print('INFO: Support/IO/can_native_board.c completed after generation; generator changed-files.txt excludes Support')
ioc=dict(line.split('=',1) for line in (board/'OMNICTL2_3.ioc').read_text().splitlines() if '=' in line)
assert ioc['RCC.DIVQ2']=='10'
assert ioc['RCC.FDCANFreq_Value']=='80000000'
main=(board/'Core/Src/main.c').read_text()
assert 'PLL2.PLL2Q = 10;' in main and 'PLL2.PLL2P = 2;' in main and 'PLL2.PLL2R = 20;' in main
fdcan=(board/'Core/Src/fdcan.c').read_text()
for bus,offset in ((1,'0'),(2,'0x300'),(3,'0x600')):
    for key,value in {'NominalPrescaler':'1','NominalTimeSeg1':'60','NominalTimeSeg2':'19','NominalSyncJumpWidth':'13','MessageRAMOffset':offset}.items():
        assert ioc.get(f'FDCAN{bus}.{key}','0' if bus==1 and key=='MessageRAMOffset' else None)==value,(bus,key)
        assert f'hfdcan{bus}.Init.{key} = {value};' in fdcan
print('PASS: IOC/generated 80 MHz Classic baseline, nominal timing and fixed RAM windows')
expected={f'FDCAN{bus}_IT{line}_IRQn':5 for bus in range(1,4) for line in range(2)}
expected.update({name:5 for name in ['EXTI9_5_IRQn','SPI4_IRQn','DMA2_Stream0_IRQn','DMA2_Stream1_IRQn']})
expected.update({name:6 for name in ['EXTI15_10_IRQn','UART7_IRQn','UART8_IRQn','UART9_IRQn','USART1_IRQn','USART6_IRQn']})
expected.update({f'DMA1_Stream{i}_IRQn':6 for i in range(2,8)})
expected.update({f'DMA2_Stream{i}_IRQn':6 for i in range(2,6)})
sources='\n'.join(p.read_text() for p in (board/'Core/Src').glob('*.c'))
for irq,priority in expected.items():
    assert ioc['NVIC.'+irq].split(':')[1].rstrip('\\')==str(priority),irq
    assert f'HAL_NVIC_SetPriority({irq}, {priority}, 0);' in sources,irq
print('PASS:',len(expected),'P0/P1 IRQ priorities preserved in IOC and generated code')
