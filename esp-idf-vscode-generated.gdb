set remote hardware-watchpoint-limit 2
target remote :3333
symbol-file C:\Users\sle11\Documents\VSCode\squeezelite-esp32/build/recovery.elf
mon reset halt
flushregs
thb app_main
c