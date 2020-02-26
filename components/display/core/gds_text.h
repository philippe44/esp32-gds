/* 
 *  (c) Philippe G. 2019, philippe_44@outlook.com
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
 *
 */

#pragma once

#include "gds_font.h"

#define GDS_TEXT_CLEAR 		0x01
#define	GDS_TEXT_CLEAR_EOL	0x02
#define GDS_TEXT_UPDATE		0x04
#define GDS_TEXT_MONOSPACE	0x08

// these ones are for 'Pos' parameter of TextLine
#define GDS_TEXT_LEFT 	0
#define GDS_TEXT_RIGHT	0xff00
#define	GDS_TEXT_CENTER 0xff01

// these ones are for the 'Where' parameter of TextPos
enum { GDS_TEXT_TOP_LEFT, GDS_TEXT_MIDDLE_LEFT, GDS_TEXT_BOTTOM_LEFT, GDS_TEXT_CENTERED };

enum { GDS_FONT_DEFAULT, GDS_FONT_LINE_1, GDS_FONT_LINE_2, GDS_FONT_SEGMENT, 
	   GDS_FONT_TINY, GDS_FONT_SMALL, GDS_FONT_MEDIUM, GDS_FONT_LARGE, GDS_FONT_FONT_HUGE };
	   
struct GDS_Device;
	   
bool 	GDS_TextSetFontAuto(struct GDS_Device* Device, int N, int FontType, int Space);
bool 	GDS_TextSetFont(struct GDS_Device* Device, int N, const struct GDS_FontDef *Font, int Space);
bool 	GDS_TextLine(struct GDS_Device* Device, int N, int Pos, int Attr, char *Text);
int 	GDS_TextStretch(struct GDS_Device* Device, int N, char *String, int Max);
void 	GDS_TextPos(struct GDS_Device* Device, int FontType, int Where, int Attr, char *Text, ...);