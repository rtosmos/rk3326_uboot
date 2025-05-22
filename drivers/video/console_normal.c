/*
 * Copyright (c) 2015 Google, Inc
 * (C) Copyright 2001-2015
 * DENX Software Engineering -- wd@denx.de
 * Compulab Ltd - http://compulab.co.il/
 * Bernecker & Rainer Industrieelektronik GmbH - http://www.br-automation.com
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <video.h>
#include <video_console.h>
#include <video_font.h>		/* Get font data, width and height */

#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	#include <rockchip_display_cmds.h>
#endif

static int console_normal_set_row(struct udevice *dev, uint row, int clr)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	void *line;
	int pixels = VIDEO_FONT_HEIGHT * vid_priv->xsize;
	int i;

	line = vid_priv->fb + row * VIDEO_FONT_HEIGHT * vid_priv->line_length;
	switch (vid_priv->bpix) {
#ifdef CONFIG_VIDEO_BPP8
	case VIDEO_BPP8: {
		uint8_t *dst = line;

		for (i = 0; i < pixels; i++)
			*dst++ = clr;
		break;
	}
#endif
#ifdef CONFIG_VIDEO_BPP16
	case VIDEO_BPP16: {
		uint16_t *dst = line;

		for (i = 0; i < pixels; i++)
			*dst++ = clr;
		break;
	}
#endif
#ifdef CONFIG_VIDEO_BPP32
	case VIDEO_BPP32: {
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
		struct lcd_fb_bit *dst = line, *bg = lcd_getbg();
		struct video_fb_bit *c = (struct video_fb_bit *)&clr;
		if (!lcd_gettransp()) {
			for (i = 0; i < pixels; i++, dst++) {
				if (bg == NULL) {
					dst->r = c->r;
					dst->g = c->g;
					dst->b = c->b;
				} else {
					dst->r = bg->r;
					dst->g = bg->g;
					dst->b = bg->b;
				}
			}
		}
		break;
#else
		uint32_t *dst = line;

		for (i = 0; i < pixels; i++)
			*dst++ = clr;
		break;
#endif
	}
#endif
	default:
		return -ENOSYS;
	}

	return 0;
}

static int console_normal_move_rows(struct udevice *dev, uint rowdst,
				     uint rowsrc, uint count)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	void *dst;
	void *src;

	dst = vid_priv->fb + rowdst * VIDEO_FONT_HEIGHT * vid_priv->line_length;
	src = vid_priv->fb + rowsrc * VIDEO_FONT_HEIGHT * vid_priv->line_length;
	memmove(dst, src, VIDEO_FONT_HEIGHT * vid_priv->line_length * count);

	return 0;
}
/*
static int console_normal_putc_xy(struct udevice *dev, uint x_frac, uint y,
				  char ch)
{
	struct vidconsole_priv *vc_priv = dev_get_uclass_priv(dev);
	struct udevice *vid = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid);
	int i,row;
	int j;
	unsigned long timer_count = get_timer(0);
	//int xrow=0;
	//int yrow=0;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	void *line = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x_frac) * 3;
#else
	void *line = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x_frac) * VNBYTES(vid_priv->bpix);
#endif

	if (x_frac + VID_TO_POS(vc_priv->x_charsize) > vc_priv->xsize_frac)
		return -EAGAIN;

	local_irq_disable();	
	for (row = 0; row < 480; row++) {
	//for (row = 0; row < VIDEO_FONT_HEIGHT; row++) {
		//printf("ch: %x \n",ch);
	//	uchar bits = video_fontdata[ch * VIDEO_FONT_HEIGHT + row];
	//uchar bits = video_font32x32[0][0];
	//uchar bits = video_font64x64[0][0];
	//for (row = 0; row < 12; row++) {
	//uchar bits =video_font32x64[0][0];
	uchar bits =IMG_DATA320x480[row][0];
	
	struct lcd_fb_bit *dst = line, *lfg, *lbg;
	struct video_fb_bit *fg, *bg;
	bool transp = lcd_gettransp();
			fg = (struct video_fb_bit *)&vid_priv->colour_fg;
			bg = (struct video_fb_bit *)&vid_priv->colour_bg;

			lfg = lcd_getfg();
			lbg = lcd_getbg();
			
			//for (i = 0; i < VIDEO_FONT_WIDTH; i++, dst++) {
			for (j = 0; j < 40;j++) {
				
				//bits = video_font64x64[row][j];
				bits = IMG_DATA320x480[row][j];
			//	printf("video_font: %#x \n",bits);
			for (i = 0; i < 8; i++, dst++) {
				if (bits & 0x80) {
					 if (lfg == NULL) {
						dst->r = fg->r;
						dst->g = fg->g;
						dst->b = fg->b;
						printf("lfg \n");
					} else {
						
						//dst->r = lfg->r;
						//dst->g = lfg->g;
						//dst->b = lfg->b;
						timer_count = get_timer(0);
			            printk("当前定时器计数（毫秒）time: %lu\n", timer_count);
						dst->r = 0x0;
						dst->g = 0xff;
						dst->b = 0x0;
					}
				} else {
					if (!transp) {
						if (lbg == NULL) {
							dst->r = bg->r;
							dst->g = bg->g;
							dst->b = bg->b;
						} else {
							dst->r = lbg->r;
							dst->g = lbg->g;
							dst->b = lbg->b;
						}
					}
				}
				bits <<= 1;
			}
		}
		line += vid_priv->line_length;
	}
 local_irq_enable();
	return VID_TO_POS(VIDEO_FONT_WIDTH);

}
*/
/*
static int console_normal_putc_xy(struct udevice *dev, uint x_frac, uint y,
				  char ch)
{
	struct vidconsole_priv *vc_priv = dev_get_uclass_priv(dev);
	struct udevice *vid = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid);
	 int i,row;
	int j;
	int a;
	a=ch;
	printf("a: %d \n",a);
	//int xrow=0;
	//int yrow=0;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	void *line = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x_frac) * 3;
#else
	void *line = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x_frac) * VNBYTES(vid_priv->bpix);
#endif

	if (x_frac + VID_TO_POS(vc_priv->x_charsize) > vc_priv->xsize_frac)
		return -EAGAIN;

		
	for (row = 0; row < 640; row++) {
	//for (row = 0; row < VIDEO_FONT_HEIGHT; row++) {
		printf("ch: %x \n",ch);
	//	uchar bits = video_fontdata[ch * VIDEO_FONT_HEIGHT + row];
	//uchar bits = video_font32x32[0][0];
	//uchar bits = video_font64x64[0][0];
	//for (row = 0; row < 12; row++) {
	//uchar bits =video_font32x64[0][0];
	//uchar bits =video_font32x64[ch*46-1702+row][0];
	uchar bits =IMG_DATA64x100[row];
	struct lcd_fb_bit *dst = line, *lfg, *lbg;
	struct video_fb_bit *fg, *bg;
	bool transp = lcd_gettransp();
			fg = (struct video_fb_bit *)&vid_priv->colour_fg;
			bg = (struct video_fb_bit *)&vid_priv->colour_bg;

			lfg = lcd_getfg();
			lbg = lcd_getbg();
			
			//for (i = 0; i < VIDEO_FONT_WIDTH; i++, dst++) {
		//	for (j = 0; j < VIDEO_FONT32x64_WIDTH;j++) {
				//bits = video_font64x64[row][j];
				//bits = video_font32x64[ch*46-1702+row][j];
				printf("video_font: %#x \n",bits);
			for (i = 0; i < 8; i++, dst++) {
				if (bits & 0x80) {
					 if (lfg == NULL) {
						dst->r = fg->r;
						dst->g = fg->g;
						dst->b = fg->b;
						printf("lfg \n");
					} else {
						
						//dst->r = lfg->r;
						//dst->g = lfg->g;
						//dst->b = lfg->b;
						
						dst->r = 0x0;
						dst->g = 0xff;
						dst->b = 0x0;
					}
				} else {
					if (!transp) {
						if (lbg == NULL) {
							dst->r = bg->r;
							dst->g = bg->g;
							dst->b = bg->b;
						} else {
							dst->r = lbg->r;
							dst->g = lbg->g;
							dst->b = lbg->b;
						}
					}
				}
				bits <<= 1;
			}
		//}
        j=row/50;
		if(j==1){line += vid_priv->line_length;}
	}

	return VID_TO_POS(VIDEO_FONT_WIDTH);

}
*/
///*
static int console_normal_putc_xy(struct udevice *dev, uint x_frac, uint y,
				  char ch)
{
	struct vidconsole_priv *vc_priv = dev_get_uclass_priv(dev);
	struct udevice *vid = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid);
	int i,row;
	int j;
	
	//int xrow=0;
	//int yrow=0;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	void *line = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x_frac) * 3;
#else
	void *line = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x_frac) * VNBYTES(vid_priv->bpix);
#endif

	if (x_frac + VID_TO_POS(vc_priv->x_charsize) > vc_priv->xsize_frac)
		return -EAGAIN;

		
	for (row = 0; row < 46; row++) {
	//for (row = 0; row < VIDEO_FONT_HEIGHT; row++) {
		printf("ch: %x \n",ch);
	//	uchar bits = video_fontdata[ch * VIDEO_FONT_HEIGHT + row];
	//uchar bits = video_font32x32[0][0];
	//uchar bits = video_font64x64[0][0];
	//for (row = 0; row < 12; row++) {
	//uchar bits =video_font32x64[0][0];
	uchar bits =video_font32x64[ch*46-1702+row][0];
	
	struct lcd_fb_bit *dst = line, *lfg, *lbg;
	struct video_fb_bit *fg, *bg;
	bool transp = lcd_gettransp();
			fg = (struct video_fb_bit *)&vid_priv->colour_fg;
			bg = (struct video_fb_bit *)&vid_priv->colour_bg;

			lfg = lcd_getfg();
			lbg = lcd_getbg();
			
			//for (i = 0; i < VIDEO_FONT_WIDTH; i++, dst++) {
			for (j = 0; j < VIDEO_FONT32x64_WIDTH;j++) {
				//bits = video_font64x64[row][j];
				bits = video_font32x64[ch*46-1702+row][j];
				printf("video_font: %#x \n",bits);
			for (i = 0; i < 8; i++, dst++) {
				if (bits & 0x80) {
					 if (lfg == NULL) {
						dst->r = fg->r;
						dst->g = fg->g;
						dst->b = fg->b;
						printf("lfg \n");
					} else {
						
						//dst->r = lfg->r;
						//dst->g = lfg->g;
						//dst->b = lfg->b;
						
						dst->r = 0x0;
						dst->g = 0xff;
						dst->b = 0x0;
					}
				} else {
					if (!transp) {
						if (lbg == NULL) {
							dst->r = bg->r;
							dst->g = bg->g;
							dst->b = bg->b;
						} else {
							dst->r = lbg->r;
							dst->g = lbg->g;
							dst->b = lbg->b;
						}
					}
				}
				bits <<= 1;
			}
		}
		line += vid_priv->line_length;
	}

	return VID_TO_POS(VIDEO_FONT_WIDTH);

}
//*/
/*
static int console_normal_putc_xy(struct udevice *dev, uint x_frac, uint y,
				  char ch)
{
	struct vidconsole_priv *vc_priv = dev_get_uclass_priv(dev);
	struct udevice *vid = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid);
	int i,row;
	int j;
	//int xrow=0;
	//int yrow=0;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	void *line = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x_frac) * 3;
#else
	void *line = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x_frac) * VNBYTES(vid_priv->bpix);
#endif

	if (x_frac + VID_TO_POS(vc_priv->x_charsize) > vc_priv->xsize_frac)
		return -EAGAIN;

	for (row = 0; row < 46; row++) {
	//for (row = 0; row < VIDEO_FONT_HEIGHT; row++) {
		printf("ch: %x \n",ch);
	//	uchar bits = video_fontdata[ch * VIDEO_FONT_HEIGHT + row];
	//uchar bits = video_font32x32[0][0];
	//uchar bits = video_font64x64[0][0];
	//for (row = 0; row < 12; row++) {
	//uchar bits =video_font32x64[0][0];
	uchar bits =video_font32x64[ch*46-1702+row][0];
//uchar bits = video_font[xrow][yrow];
		switch (vid_priv->bpix) {
#ifdef CONFIG_VIDEO_BPP8
		case VIDEO_BPP8: {
			uint8_t *dst = line;

			for (i = 0; i < VIDEO_FONT_WIDTH; i++) {
				*dst++ = (bits & 0x80) ? vid_priv->colour_fg
					: vid_priv->colour_bg;
				bits <<= 1;
			}
			break;
		}
#endif
#ifdef CONFIG_VIDEO_BPP16
		case VIDEO_BPP16: {
			uint16_t *dst = line;

			for (i = 0; i < VIDEO_FONT_WIDTH; i++) {
				*dst++ = (bits & 0x80) ? vid_priv->colour_fg
					: vid_priv->colour_bg;
				bits <<= 1;
			}
			break;
		}
#endif
#ifdef CONFIG_VIDEO_BPP32
		case VIDEO_BPP32: {
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
			struct lcd_fb_bit *dst = line, *lfg, *lbg;
			struct video_fb_bit *fg, *bg;
			bool transp = lcd_gettransp();

			fg = (struct video_fb_bit *)&vid_priv->colour_fg;
			bg = (struct video_fb_bit *)&vid_priv->colour_bg;

			lfg = lcd_getfg();
			lbg = lcd_getbg();
			
			//for (i = 0; i < VIDEO_FONT_WIDTH; i++, dst++) {
			for (j = 0; j < VIDEO_FONT32x64_WIDTH;j++) {
				//bits = video_font64x64[row][j];
				bits = video_font32x64[ch*46-1702+row][j];
				printf("video_font: %#x \n",bits);
			for (i = 0; i < 8; i++, dst++) {
				if (bits & 0x80) {
					if (lfg == NULL) {
						dst->r = fg->r;
						dst->g = fg->g;
						dst->b = fg->b;
						printf("lfg \n");
					} else {
						
						//dst->r = lfg->r;
						//dst->g = lfg->g;
						//dst->b = lfg->b;
						
						dst->r = 0x0;
						dst->g = 0xff;
						dst->b = 0x0;
					}
				} else {
					if (!transp) {
						if (lbg == NULL) {
							dst->r = bg->r;
							dst->g = bg->g;
							dst->b = bg->b;
						} else {
							dst->r = lbg->r;
							dst->g = lbg->g;
							dst->b = lbg->b;
						}
					}
				}
				bits <<= 1;
			}
			}
			break;
#else
			uint32_t *dst = line;

			for (i = 0; i < VIDEO_FONT_WIDTH; i++) {
				*dst++ = (bits & 0x80) ? vid_priv->colour_fg
					: vid_priv->colour_bg;
				bits <<= 1;
			}
			break;
#endif
		}
#endif
		default:
			return -ENOSYS;
		}
		line += vid_priv->line_length;
	}

	return VID_TO_POS(VIDEO_FONT_WIDTH);
}
*/
static int console_normal_probe(struct udevice *dev)
{
	struct vidconsole_priv *vc_priv = dev_get_uclass_priv(dev);
	struct udevice *vid_dev = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid_dev);

	vc_priv->x_charsize = VIDEO_FONT_WIDTH;
	vc_priv->y_charsize = VIDEO_FONT_HEIGHT;
	vc_priv->cols = vid_priv->xsize / VIDEO_FONT_WIDTH;
	vc_priv->rows = vid_priv->ysize / VIDEO_FONT_HEIGHT;

	return 0;
}

struct vidconsole_ops console_normal_ops = {
	.putc_xy	= console_normal_putc_xy,
	.move_rows	= console_normal_move_rows,
	.set_row	= console_normal_set_row,
};

U_BOOT_DRIVER(vidconsole_normal) = {
	.name	= "vidconsole0",
	.id	= UCLASS_VIDEO_CONSOLE,
	.ops	= &console_normal_ops,
	.probe	= console_normal_probe,
};
