
def gen_pinout(cfg, out, inc):
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
            L.append('    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIO' + port + ');')
    if ports_seen:
        L.append('    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIO' + list(ports_seen)[0] + ')){}')
    for p in cfg.get("gpio", []):
        port = p["port"]
        mask = 'GPIO_PIN_' + str(p["pin"])
        if p["direction"] == "output":
            L.append('    GPIOPinTypeGPIOOutput(GPIO_PORT' + port + '_BASE, ' + mask + ');')
        else:
            L.append('    HWREG(GPIO_PORT' + port + '_BASE+GPIO_O_DIR)&=~' + mask + ';')
            L.append('    HWREG(GPIO_PORT' + port + '_BASE+GPIO_O_DEN)|=' + mask + ';')
            if p.get("pull")=="up":
                L.append('    HWREG(GPIO_PORT' + port + '_BASE+GPIO_O_PUR)|=' + mask + ';')
    L.append('}')
    (out / "pinout.c").write_text("\n".join(L), encoding="utf-8")
    print('  -> Common/src/pinout.c')

    inc.mkdir(parents=True, exist_ok=True)
    H = ['#ifndef PINOUT_H', '#define PINOUT_H']
    H.append('extern void PinoutSet(void);')
    H.append('#endif')
    (inc / "pinout.h").write_text("\n".join(H), encoding="utf-8")
    print('  -> Common/inc/pinout.h')


if __name__ == "__main__":
    import argparse, json
    from pathlib import Path
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfg", default=str(Path(__file__).resolve().parent.parent / ".syscfg" / "project.json"))
    root = Path(__file__).resolve().parent.parent
    ap.add_argument("--out", default=str(root / "Common" / "src"))
    ap.add_argument("--inc", default=str(root / "Common" / "inc"))
    a = ap.parse_args()
    cfg_path = Path(a.cfg)
    if not cfg_path.exists(): print("[SKIP]"); exit(0)
    cfg = json.loads(cfg_path.read_text(encoding="utf-8-sig"))
    out = Path(a.out)
    inc = Path(a.inc)
    out.mkdir(parents=True, exist_ok=True)
    inc.mkdir(parents=True, exist_ok=True)
    print("Generating board config...")
    gen_pinout(cfg, out, inc)
