/*************************************************************************/
/*  image_loader_svg.h                                                   */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef IMAGE_LOADER_SVG_H
#define IMAGE_LOADER_SVG_H

#include "core/io/image_loader.h"

#include "thirdparty/thorvg/inc/thorvg.h"

class ImageLoaderSVG : public ImageFormatLoader {
	static struct ReplaceColors {
		List<uint32_t> old_colors;
		List<uint32_t> new_colors;
	} replace_colors;

public:
	typedef bool (*ConvertTVGColorsFunc)(const tvg::Paint *);
	static ConvertTVGColorsFunc convert_tvg_color_func;
	static bool _convert_tvg_paints(const tvg::Paint *p_paint);

	static void set_convert_colors(Dictionary *p_replace_color = nullptr);
	static void create_image_from_string(Ref<Image> p_image, String p_string, float p_scale, bool p_upsample, bool p_convert_color);

	virtual Error load_image(Ref<Image> p_image, FileAccess *p_fileaccess, bool p_force_linear, float p_scale) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;

	virtual ~ImageLoaderSVG() {}
};

#endif // IMAGE_LOADER_SVG_H
