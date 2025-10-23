/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* rtc.h - WonderSwan RTC Emulation
**  Copyright (C) 2014-2016 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __WSWAN_RTC_H
#define __WSWAN_RTC_H

namespace MDFN_IEN_WSWAN
{

//template<bool century21st>
struct GenericRTC
{
 GenericRTC();
 void Init(const struct tm& toom);
 void Clock(void);

 bool BCDInc(uint8 &V, uint8 thresh, uint8 reset_val = 0x00);

 uint8 sec;
 uint8 min;
 uint8 hour;
 uint8 wday;
 uint8 mday;
 uint8 mon;
 uint8 year;
};

GenericRTC *RTC_Get(void);

void RTC_Write(uint8 A, uint8 V);
uint8 RTC_Read(uint8 A);

void RTC_Init(void) MDFN_COLD;
void RTC_Reset(void);

void RTC_Clock(uint32 cycles);
void RTC_StateAction(StateMem *sm, const unsigned load, const bool data_only);

}

#endif
