/*
 * macro_utils.h - define some useful macros.
 *
 * Copyright (c) 2015 Chaos Shen
 *
 * This file is part of wtfs, What the fxck filesystem.  You may take
 * the letter 'f' from, at your option, either 'fxck' or 'filesystem'.
 *
 * wtfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * wtfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wtfs.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef WTFS_MACRO_UTILS_H_
#define WTFS_MACRO_UTILS_H_

/* int types */
typedef __u8 wtfs8_t;
typedef __le16 wtfs16_t;
typedef __le32 wtfs32_t;
typedef __le64 wtfs64_t;

/* int type converters */
# define wtfs16_to_cpu le16_to_cpu
# define wtfs32_to_cpu le32_to_cpu
# define wtfs64_to_cpu le64_to_cpu
# define cpu_to_wtfs16 cpu_to_le16
# define cpu_to_wtfs32 cpu_to_le32
# define cpu_to_wtfs64 cpu_to_le64

/* get the size of a structure/union's member */
#define member_size(type, member) sizeof(((type *)0)->member)

#endif /* WTFS_MACRO_UTILS_H_ */
