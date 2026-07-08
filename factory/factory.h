/**
 * @file    factory.h
 * @brief   厂测固件公共接口（链 APP_B @ 0x00021000，不直接运行）
 */

#ifndef FACTORY_H
#define FACTORY_H

#include "button.h"

void Factory_Board_Init(void);
void Factory_Start(void);
void factory_button_notify(btn_id_e id, const char *name, btn_permission_e permission,
                           btn_event_e event);

#endif /* FACTORY_H */
