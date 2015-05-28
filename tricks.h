/*
 * tricks.h - define some macros that help coding in IDE.
 * references to this file must be cleared on compiling
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

#ifndef WTFS_TRICKS_H_
#define WTFS_TRICKS_H_

/* enable kernel development */
#ifndef __KERNEL__
# define __KERNEL__
#endif /* __KERNEL__ */

/* enable kernel block layer */
#ifndef CONFIG_BLOCK
# define CONFIG_BLOCK
#endif /* CONFIG_BLOCK */

/* enable kernel slab memory */
#ifndef CONFIG_SLOB
# define CONFIG_SLOB
#endif /* CONFIG_SLOB */

/* disable assembly */
#ifdef __ASSEMBLY__
# undef __ASSEMBLY__
#endif /* __ASSEMBLY__ */

#endif /* WTFS_TRICKS_H_ */
