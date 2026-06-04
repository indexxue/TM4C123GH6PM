#!/usr/bin/env python3
import json, argparse, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
HB = {"A":"0x40004000u","B":"0x40005000u","C":"0x40006000u","D":"0x40007000u","E":"0x40024000u","F":"0x40025000u"}
CK = {"A":"R0","B":"R1","C":"R2","D":"R3","E":"R4","F":"R5"}
def gen(cfg, out):
    for p in cfg.get("gpio",[]):
        p["base"] = HB[p["port"]]
    L = ["/* Auto-generated */","#ifndef TM4C123_BOARD_H","#define TM4C123_BOARD_H",""]
    for p in cfg.get("gpio",[]):
        L.append("#define "+p["label"]+"_PORT  "+p["base"])
        L.append("#define "+p["label"]+"_PIN   "+str(p["pin"]))
        L.append("")
    L.append("#ifndef HWREG")
    L.append("#define HWREG(x) (*((volatile uint32_t *)(x)))")
    L.append("#endif"); L.append("")
    for p in cfg.get("gpio",[]):
        b = p["base"]
        if p["direction"]=="output":
            L.append("#define "+p["label"]+"_On()      HWREG("+b+"+(0x400u|("+str(p["pin"])+"<<2)))=0xFFu")
            L.append("#define "+p["label"]+"_Off()     HWREG("+b+"+(0x400u|("+str(p["pin"])+"<<2)))=0x00u")
            L.append("#define "+p["label"]+"_Toggle()  HWREG("+b+"+(0x400u|("+str(p["pin"])+"<<2)))^=0xFFu")
        else:
            L.append("#define "+p["label"]+"_Read()    (HWREG("+b+"+(0x400u|("+str(p["pin"])+"<<2)))&0xFFu)")
        L.append("")
    clk = cfg.get("system",{}).get("clock_hz",16000000)
    L.append("#define SYSCLK_HZ  "+str(clk)+"u")
    L.append(""); L.append("void Board_Init(void);"); L.append(""); L.append("#endif")
    (out/"tm4c123_board.h").write_text("\n".join(L),encoding="utf-8")
    ports = sorted(set(p["port"] for p in cfg.get("gpio",[])))
    L = ["/* Auto-generated */","#include \"tm4c123_board.h\"","#include \"tm4c123gh6pm.h\"","void Board_Init(void){"]
    for port in ports:
        L.append("    SYSCTL_RCGCGPIO_R|=SYSCTL_RCGCGPIO_"+CK[port]+";")
    for port in ports:
        L.append("    while((SYSCTL_PRGPIO_R&SYSCTL_RCGCGPIO_"+CK[port]+")==0u){}")
    for port in ports:
        base = HB[port]
        pins = [p for p in cfg.get("gpio",[]) if p["port"]==port]
        out_ = sum((1<<p["pin"]) for p in pins if p["direction"]=="output")
        inp_ = sum((1<<p["pin"]) for p in pins if p["direction"]=="input")
        pull = sum((1<<p["pin"]) for p in pins if p.get("pull")=="up")
        L.append("    /* Port "+port+" */")
        if any(p.get("commit")=="unlock" for p in pins):
            L.append("    HWREG("+base+"+0x520u)=GPIO_LOCK_KEY;HWREG("+base+"+0x524u)|=0x"+"%02X"% (out_|inp_)+"u;")
        if out_: L.append("    HWREG("+base+"+0x400u)|=0x"+"%02X"% out_+"u;")
        if inp_: L.append("    HWREG("+base+"+0x400u)&=~0x"+"%02X"% inp_+"u;")
        if out_|inp_: L.append("    HWREG("+base+"+0x51Cu)|=0x"+"%02X"% (out_|inp_)+"u;")
        if pull: L.append("    HWREG("+base+"+0x510u)|=0x"+"%02X"% pull+"u;")
        for p in pins:
            if p["direction"]=="output" and p.get("init"):
                v = "0xFFu" if p["init"]=="high" else "0x00u"
                L.append("    HWREG("+base+"+(0x400u|("+str(p["pin"])+"<<2)))="+v+";")
    L.append("}")
    (out/"tm4c123_board.c").write_text("\n".join(L),encoding="utf-8")
    print("  -> tm4c123_board.h\n  -> tm4c123_board.c\nDone.")
if __name__=="__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfg",default=str(ROOT/".syscfg"/"project.json"))
    ap.add_argument("--out",default=str(ROOT/"src"/"generated"))
    a = ap.parse_args()
    cfg_path = Path(a.cfg)
    if not cfg_path.exists(): print("[SKIP]"); sys.exit(0)
    cfg = json.loads(cfg_path.read_text(encoding="utf-8-sig"))
    Path(a.out).mkdir(parents=True,exist_ok=True)
    print("Generating board config...")
    gen(cfg, Path(a.out))
