/*  $Header: /home/cvsroot/dvipdfmx/src/colors.h,v 1.5 2004/01/30 18:34:20 hirata Exp $
    
    This is dvipdfmx, an eXtended version of dvipdfm by Mark A. Wicks.

    Copyright (C) 2002 by Jin-Hwan Cho and Shunsaku Hirata,
    the dvipdfmx project team <dvipdfmx@project.ktug.or.kr>
    
    Copyright (C) 1998, 1999 by Mark A. Wicks <mwicks@kettering.edu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
*/

#ifndef _COLORS_H_
#define _COLORS_H_

struct color_by_name {
  const char *name;
  struct color color;
} colors_by_name[] = {
  {"GreenYellow", {CMYK, 0.15, 0, 0.69, 0}},
  {"Yellow", {CMYK, 0, 0, 1, 0}},
  {"Goldenrod", {CMYK, 0, 0.10, 0.84, 0}},
  {"Dandelion", {CMYK, 0, 0.29, 0.84, 0}},
  {"Apricot", {CMYK, 0, 0.32, 0.52, 0}},
  {"Peach", {CMYK, 0, 0.50, 0.70, 0}},
  {"Melon", {CMYK, 0, 0.46, 0.50, 0}},
  {"YellowOrange", {CMYK, 0, 0.42, 1, 0}},
  {"Orange", {CMYK, 0, 0.61, 0.87, 0}},
  {"BurntOrange", {CMYK, 0, 0.51, 1, 0}},
  {"Bittersweet", {CMYK, 0, 0.75, 1, 0.24}},
  {"RedOrange", {CMYK, 0, 0.77, 0.87, 0}},
  {"Mahogany", {CMYK, 0, 0.85, 0.87, 0.35}},
  {"Maroon", {CMYK, 0, 0.87, 0.68, 0.32}},
  {"BrickRed", {CMYK, 0, 0.89, 0.94, 0.28}},
  {"Red", {CMYK, 0, 1, 1, 0}},
  {"OrangeRed", {CMYK, 0, 1, 0.50, 0}},
  {"RubineRed", {CMYK, 0, 1, 0.13, 0}},
  {"WildStrawberry", {CMYK, 0, 0.96, 0.39, 0}},
  {"Salmon", {CMYK, 0, 0.53, 0.38, 0}},
  {"CarnationPink", {CMYK, 0, 0.63, 0, 0}},
  {"Magenta", {CMYK, 0, 1, 0, 0}},
  {"VioletRed", {CMYK, 0, 0.81, 0, 0}},
  {"Rhodamine", {CMYK, 0, 0.82, 0, 0}},
  {"Mulberry", {CMYK, 0.34, 0.90, 0, 0.02}},
  {"RedViolet", {CMYK, 0.07, 0.90, 0, 0.34}},
  {"Fuchsia", {CMYK, 0.47, 0.91, 0, 0.08}},
  {"Lavender", {CMYK, 0, 0.48, 0, 0}},
  {"Thistle", {CMYK, 0.12, 0.59, 0, 0}},
  {"Orchid", {CMYK, 0.32, 0.64, 0, 0}},
  {"DarkOrchid", {CMYK, 0.40, 0.80, 0.20, 0}},
  {"Purple", {CMYK, 0.45, 0.86, 0, 0}},
  {"Plum", {CMYK, 0.50, 1, 0, 0}},
  {"Violet", {CMYK, 0.79, 0.88, 0, 0}},
  {"RoyalPurple", {CMYK, 0.75, 0.90, 0, 0}},
  {"BlueViolet", {CMYK, 0.86, 0.91, 0, 0.04}},
  {"Periwinkle", {CMYK, 0.57, 0.55, 0, 0}},
  {"CadetBlue", {CMYK, 0.62, 0.57, 0.23, 0}},
  {"CornflowerBlue", {CMYK, 0.65, 0.13, 0, 0}},
  {"MidnightBlue", {CMYK, 0.98, 0.13, 0, 0.43}},
  {"NavyBlue", {CMYK, 0.94, 0.54, 0, 0}},
  {"RoyalBlue", {CMYK, 1, 0.50, 0, 0}},
  {"Blue", {CMYK, 1, 1, 0, 0}},
  {"Cerulean", {CMYK, 0.94, 0.11, 0, 0}},
  {"Cyan", {CMYK, 1, 0, 0, 0}},
  {"ProcessBlue", {CMYK, 0.96, 0, 0, 0}},
  {"SkyBlue", {CMYK, 0.62, 0, 0.12, 0}},
  {"Turquoise", {CMYK, 0.85, 0, 0.20, 0}},
  {"TealBlue", {CMYK, 0.86, 0, 0.34, 0.02}},
  {"Aquamarine", {CMYK, 0.82, 0, 0.30, 0}},
  {"BlueGreen", {CMYK, 0.85, 0, 0.33, 0}},
  {"Emerald", {CMYK, 1, 0, 0.50, 0}},
  {"JungleGreen", {CMYK, 0.99, 0, 0.52, 0}},
  {"SeaGreen", {CMYK, 0.69, 0, 0.50, 0}},
  {"Green", {CMYK, 1, 0, 1, 0}},
  {"ForestGreen", {CMYK, 0.91, 0, 0.88, 0.12}},
  {"PineGreen", {CMYK, 0.92, 0, 0.59, 0.25}},
  {"LimeGreen", {CMYK, 0.50, 0, 1, 0}},
  {"YellowGreen", {CMYK, 0.44, 0, 0.74, 0}},
  {"SpringGreen", {CMYK, 0.26, 0, 0.76, 0}},
  {"OliveGreen", {CMYK, 0.64, 0, 0.95, 0.40}},
  {"RawSienna", {CMYK, 0, 0.72, 1, 0.45}},
  {"Sepia", {CMYK, 0, 0.83, 1, 0.70}},
  {"Brown", {CMYK, 0, 0.81, 1, 0.60}},
  {"Tan", {CMYK, 0.14, 0.42, 0.56, 0}},
  {"Gray", {CMYK, 0, 0, 0, 0.50}},
  {"Black", {CMYK, 0, 0, 0, 1}},
  {"White", {CMYK, 0, 0, 0, 0}}
};

#endif /* _COLORS_H_ */
