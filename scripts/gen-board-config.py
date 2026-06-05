
def gen_pinout(cfg, out):
    L = ['/* Auto-generated pinout by gen-board-config.py */']
    L.append('#include <stdint.h>')
    L.append('#include <stdbool.h>')
    L.append('#include "inc/hw_memmap.h"')
    L.append('#include "inc/hw_gpio.h"')
    L.append('#include "inc/hw_types.h"')
    L.append('#include "driverlib/sysctl.h"')
    L.append('#include "driverlib/gpio.h"')
    L.append('#include "pinout.h"')
    L.append('')
    L.append('void PinoutSet(void) {')
    ports_seen = set()
    for p in cfg.get("gpio", []):
        port = p["port"]
        if port not in ports_seen:
            ports_seen.add(port)
            L.append('    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIO' + port + ');')
    if ports_seen:
        L.append('    while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIO' + list(ports_seen)[0] + ')){}')
    for p in cfg.get("gpio", []):
        port = p["port"]
        mask = 'GPIO_PIN_' + str(p["pin"])
        if p["direction"] == "output":
            L.append('    MAP_GPIOPinTypeGPIOOutput(GPIO_PORT' + port + '_BASE, ' + mask + ');')
        else:
            L.append('    HWREG(GPIO_PORT' + port + '_BASE+GPIO_O_DIR)&=~' + mask + ';')
            L.append('    HWREG(GPIO_PORT' + port + '_BASE+GPIO_O_DEN)|=' + mask + ';')
            if p.get("pull")=="up":
                L.append('    HWREG(GPIO_PORT' + port + '_BASE+GPIO_O_PUR)|=' + mask + ';')
    L.append('}')
    (out / "pinout.c").write_text("\n".join(L), encoding="utf-8")
    print('  -> pinout.c')
    
    # Also generate pinout.h
    H = ['#ifndef __DRIVERS_PINOUT_H__', '#define __DRIVERS_PINOUT_H__']
    H.append('extern void PinoutSet(void);')
    H.append('#endif')
    (out / "pinout.h").write_text("\n".join(H), encoding="utf-8")
    print('  -> pinout.h')

# Call gen_pinout in main()
old_main = 'print("Generating board config...")'
new_main = 'print("Generating board config...")'
old_gen = 'gen(cfg, Path(a.out)); gen_pinout(cfg, Path(a.out)); '
new_gen = 'gen(cfg, Path(a.out)); gen_pinout(cfg, Path(a.out)); ; gen_pinout(cfg, Path(a.out)); '

def gen_pinout(cfg, out):
    L = ['/* Auto-generated */', '#include <stdint.h>', '#include <stdbool.h>',
         '#include "inc/hw_memmap.h"', '#include "inc/hw_gpio.h"', '#include "inc/hw_types.h"',
         '#include "driverlib/sysctl.h"', '#include "driverlib/gpio.h"', '#include "pinout.h"', '']
    L.append('void PinoutSet(void){')
    for p in cfg.get('gpio',[]):
        if p['direction']=='output':
            L.append('    MAP_GPIOPinTypeGPIOOutput(0x%08Xu,1<<%d);' % ({'A':0x40004000,'B':0x40005000,'C':0x40006000,'D':0x40007000,'E':0x40024000,'F':0x40025000}[p['port']], p['pin']))
    L.append('}')
    (out/'pinout.c').write_text(chr(10).join(L)+chr(10), encoding='utf-8')
    H = ['#ifndef __DRIVERS_PINOUT_H__','#define __DRIVERS_PINOUT_H__','extern void PinoutSet(void);','#endif']
    (out/'pinout.h').write_text(chr(10).join(H)+chr(10), encoding='utf-8')
    print('  -> pinout.c/h (fallback)')
