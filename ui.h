/*
 * Copyright (c) 2026 Marcel Licence
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file ui.h
 * @author Marcel Licence
 * @date 09.08.2026
 *
 * @brief   Declarations for the on-device SH1106 OLED + rotary encoder + button UI.
 * @n       UI_Setup()/UI_Loop() are safe to call unconditionally: when UI_OLED_ENABLED
 * @n       is not defined for the active board configuration, both are no-ops.
 */

#ifndef UI_H_
#define UI_H_


void UI_Setup(void);
void UI_Loop(void);


#endif /* UI_H_ */
