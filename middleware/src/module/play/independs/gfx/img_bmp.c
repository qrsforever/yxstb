
#include "img_decode.h"
#include "ind_mem.h"

#define BMP_RGB      	0
#define BMP_RLE8     	1
#define BMP_RLE4     	2
#define BMP_BITFIELDS	3

typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t alpha;
} Rgb;

typedef struct {
        short	type;
        int	size;
        short	reserved1;
        short	reserved2;
        int	offbits;
} Filehdr;

typedef struct {
	int	size;		/* Size of the Bitmap-file */
	int	lReserved;	/* Reserved */
	int	dataoff;	/* Picture data location */
	int	hsize;		/* Header-Size */
	int	width;		/* Picture width (pixels) */
	int	height;		/* Picture height (pixels) */
	short	planes;		/* Planes (must be 1) */
	short	bpp;		/* Bits per pixel (1, 4, 8 or 24) */
	int	compression;	/* Compression mode */
	int	imagesize;	/* Image size (bytes) */
	int	hres;		/* Horizontal Resolution (pels/meter) */
	int	vres;		/* Vertical Resolution (pels/meter) */
	int	colours;	/* Used Colours (Col-Table index) */
	int	impcolours;	/* Important colours (Col-Table index) */
} Infohdr;

/*
 MS-BMP file reader
 (c) 2003, I.P.Keller

 aims to decode *all* valid bitmap formats, although some of the
 flavours couldn't be verified due to lack of suitable test-files.
 the following flavours are supported:

	Bit/Pix	Orientation	Compression	Tested?
	  1	top->bottom	n/a		yes
	  1	bottom->top	n/a		yes
	  4	top->bottom	no		yes
	  4	bottom->top	no		yes
	  4	top->bottom	RLE4		yes, but not with displacement
	  8	top->bottom	no		yes
	  8	bottom->top	no		yes
	  8	top->bottom	RLE8		yes, but not with displacement
	 16	top->bottom	no		no
	 16	bottom->top	no		no
	 16	top->bottom	BITMASK		no
	 16	bottom->top	BITMASK		no
	 24	top->bottom	n/a		yes
	 24	bottom->top	n/a		yes
	 32	top->bottom	no		no
	 32	bottom->top	no		no
	 32	top->bottom	BITMASK		no
	 32	bottom->top	BITMASK		no

 OS/2 1.x bmp files are recognised as well, but testing was very limited.

 verifying was done with a number of test files, generated by
 different tools. nevertheless, the tests were in no way exhaustive
 enough to guarantee bug-free decoding. caveat emptor!
*/

static short
r16(Biobuf*b)
{
	short s;

	s = Bgetc(b);
	s |= ((short)Bgetc(b)) << 8;
	return s;
}


static int
r32(Biobuf*b)
{
	int l;

	l = Bgetc(b);
	l |= ((int)Bgetc(b)) << 8;
	l |= ((int)Bgetc(b)) << 16;
	l |= ((int)Bgetc(b)) << 24;
	return l;
}


/* get highest bit set */
static int
msb(uint32_t x)
{
	int i;
	for (i = 32; i; i--, x <<= 1)
		if (x & 0x80000000L)
			return i;
	return 0;
}

/* Load a 1-Bit encoded BMP file (uncompressed) */
static int
load_1T(Biobuf *b, int width, int height, Rgb* buf, Rgb* clut)
{
	int ix, iy, i = 0, step_up = 0, padded_width = ((width + 31) / 32) * 32;
	int val = 0, n;

	if (height > 0) {	/* bottom-up */
		i = (height - 1) * width;
		step_up = -2 * width;
	} else
		height = -height;

	for (iy = height; iy; iy--, i += step_up)
		for (ix = 0, n = 0; ix < padded_width; ix++, n--) {
			if (!n) {
				val = Bgetc(b);
				n = 8;
			}
			if (ix < width) {
				buf[i] = clut[val & 0x80 ? 1 : 0];
				i++;
			}
			val <<= 1;
		}
	return 0;
}

/* Load a 4-Bit encoded BMP file (uncompressed) */
static int
load_4T(Biobuf* b, int width, int height, Rgb* buf, Rgb* clut)
{
	int ix, iy, i = 0, step_up = 0, skip = (4 - (((width % 8) + 1) / 2)) & 3;
	uint32_t valH, valL;

	if (height > 0) {	/* bottom-up */
		i = (height - 1) * width;
		step_up = -2 * width;
	} else
		height = -height;

	for (iy = height; iy; iy--, i += step_up) {
		for (ix = 0; ix < width; ) {
			valH = valL = Bgetc(b) & 0xff;
			valH >>= 4;

			buf[i] = clut[valH];
			i++; ix++;

			if (ix < width) {
				valL &= 0xf;
				buf[i] = clut[valL];
				i++; ix++;
			}
		}
		Bseek(b, skip, 1);
	}
	return 0;
}

/* Load a 4-Bit encoded BMP file (RLE4-compressed) */
static int
load_4C(Biobuf *b, int width, int height, Rgb* buf, Rgb* clut)
{
	int ix, iy = height -1;
	uint32_t val, valS, skip;
	Rgb* p;

	while(iy >= 0) {
		ix = 0;
		while(ix < width) {
			val = Bgetc(b);

			if (0 != val) {
				valS = (uint32_t)Bgetc(b);
				p = &buf[ix + iy * width];
				while(val--) {
					*p = clut[0xf & (valS >> 4)];
					p++;
					ix++;
					if (0 < val) {
						*p = clut[0xf & valS];
						p++;
						ix++;
						val--;
					}
				}
			} else {
				/* Special modes... */
				val = Bgetc(b);
				switch(val) {
					case 0:	/* End-Of-Line detected */
						ix = width;
						iy--;
						break;
					case 1:	/* End-Of-Picture detected -->> abort */
						ix = width;
						iy = -1;
						break;
					case 2:	/* Position change detected */
						val = Bgetc(b);
						ix += val;
						val = Bgetc(b);
						iy -= val;
						break;

					default:/* Transparent data sequence detected */
						p = &buf[ix + iy * width];
						if ((1 == (val & 3)) || (2 == (val & 3)))
							skip = 1;
						else 
							skip = 0;

						while(val--) {
							valS = (uint32_t)Bgetc(b);
							*p = clut[0xf & (valS >> 4)];
							p++;
							ix++;
							if (0 < val) {
								*p = clut[0xf & valS];
								p++;
								ix++;
								val--;
							}
						}
						if (skip)
							Bgetc(b);
						break;
				}
			}
		}
	}
	return 0;
}

/* Load a 8-Bit encoded BMP file (uncompressed) */
static int
load_8T(Biobuf *b, int width, int height, Rgb* buf, Rgb* clut)
{
	int ix, iy, i = 0, step_up = 0, skip = (4 - (width % 4)) & 3;

	if (height > 0) {	/* bottom-up */
		i = (height - 1) * width;
		step_up = -2 * width;
	} else
		height = -height;

	for (iy = height; iy; iy--, i += step_up) {
		for (ix = 0; ix < width; ix++, i++)
			buf[i] = clut[Bgetc(b) & 0xff];
		Bseek(b, skip, 1);
	}
	return 0;
}

/* Load a 8-Bit encoded BMP file (RLE8-compressed) */
static int
load_8C(Biobuf *b, int width, int height, Rgb* buf, Rgb* clut)
{
	int ix, iy = height -1;
	int val, valS, skip;
	Rgb* p;

	while(iy >= 0) {
		ix = 0;
		while(ix < width) {
			val = Bgetc(b);

			if (0 != val) {
				valS = Bgetc(b);
				p = &buf[ix + iy * width];
				while(val--) {
					*p = clut[valS];
					p++;
					ix++;
				}
			} else {
				/* Special modes... */
				val = Bgetc(b);
				switch(val) {
					case 0: /* End-Of-Line detected */
						ix = width;
						iy--;
						break;
					case 1: /* End-Of-Picture detected */
						ix = width;
						iy = -1;
						break;
					case 2: /* Position change detected */
						val = Bgetc(b);
						ix += val;
						val = Bgetc(b);
						iy -= val;
						break;
					default: /* Transparent (not compressed) sequence detected */
						p = &buf[ix + iy * width];
						if (val & 1)
							skip = 1;
						else 
							skip = 0;

						while(val--) {
							valS = Bgetc(b);
							*p = clut[valS];
							p++;
							ix++;
						}
						if (skip)
							/* Align data stream */
							Bgetc(b);
						break;
				}
			}
		}
	}
	return 0;
}

/* Load a 16-Bit encoded BMP file (uncompressed) */
static int
load_16(Biobuf *b, int width, int height, Rgb* buf, Rgb* clut)
{
	uint8_t c[2];
	int ix, iy, i = 0, step_up = 0;

	if (height > 0) {	/* bottom-up */
		i = (height - 1) * width;
		step_up = -2 * width;
	} else
		height = -height;

	if (clut) {
		unsigned mask_blue =  (unsigned)clut[0].blue +
		                     ((unsigned)clut[0].green << 8);
		unsigned mask_green =  (unsigned)clut[1].blue +
		                      ((unsigned)clut[1].green << 8);
		unsigned mask_red =  (unsigned)clut[2].blue +
		                    ((unsigned)clut[2].green << 8);
		int shft_blue = msb((uint32_t)mask_blue) - 8;
		int shft_green = msb((uint32_t)mask_green) - 8;
		int shft_red = msb((uint32_t)mask_red) - 8;

		for (iy = height; iy; iy--, i += step_up)
			for (ix = 0; ix < width; ix++, i++) {
				unsigned val;
				Bread(b, c, sizeof(c));
				val = (unsigned)c[0] + ((unsigned)c[1] << 8);

				buf[i].alpha = 0;
				if (shft_blue >= 0)
					buf[i].blue = (uint8_t)((val & mask_blue) >> shft_blue);
				else
					buf[i].blue = (uint8_t)((val & mask_blue) << -shft_blue);
				if (shft_green >= 0)
					buf[i].green = (uint8_t)((val & mask_green) >> shft_green);
				else
					buf[i].green = (uint8_t)((val & mask_green) << -shft_green);
				if (shft_red >= 0)
					buf[i].red = (uint8_t)((val & mask_red) >> shft_red);
				else
					buf[i].red = (uint8_t)((val & mask_red) << -shft_red);
			}
	} else
		for (iy = height; iy; iy--, i += step_up)
			for (ix = 0; ix < width; ix++, i++) {
				Bread(b, c, sizeof(c));
				buf[i].blue = (uint8_t)((c[0] << 3) & 0xf8);
				buf[i].green = (uint8_t)(((((unsigned)c[1] << 6) +
				                        (((unsigned)c[0]) >> 2))) & 0xf8);
				buf[i].red = (uint8_t)((c[1] << 1) & 0xf8);
			}
	return 0;
}

/* Load a 24-Bit encoded BMP file (uncompressed) */
static int
load_24T(Biobuf* b, int width, int height, Rgb* buf)
{
	int ix, iy, i = 0, step_up = 0, skip = (4 - ((width * 3) % 4)) & 3;

	if (height > 0) {	/* bottom-up */
		i = (height - 1) * width;
		step_up = -2 * width;
	} else
		height = -height;

	for (iy = height; iy; iy--, i += step_up) {
		for (ix = 0; ix < width; ix++, i++) {
			buf[i].alpha = 0;
			buf[i].blue = Bgetc(b);
			buf[i].green = Bgetc(b);
			buf[i].red = Bgetc(b);
		}
		Bseek(b, skip, 1);
	}
	return 0;
}

/* Load a 32-Bit encoded BMP file (uncompressed) */
static int
load_32(Biobuf *b, int width, int height, Rgb* buf, Rgb* clut)
{
	uint8_t c[4];
	int ix, iy, i = 0, step_up = 0;

	if (height > 0) {	/* bottom-up */
		i = (height - 1) * width;
		step_up = -2 * width;
	} else
		height = -height;

	if (clut) {
		uint32_t mask_blue =  (uint32_t)clut[0].blue +
		                          ((uint32_t)clut[0].green << 8) +
		                          ((uint32_t)clut[0].red << 16) +
		                          ((uint32_t)clut[0].alpha << 24);
		uint32_t mask_green =  (uint32_t)clut[1].blue +
		                           ((uint32_t)clut[1].green << 8) +
		                           ((uint32_t)clut[1].red << 16) +
		                           ((uint32_t)clut[1].alpha << 24);
		uint32_t mask_red =  (uint32_t)clut[2].blue +
		                         ((uint32_t)clut[2].green << 8) +
		                         ((uint32_t)clut[2].red << 16) +
		                         ((uint32_t)clut[2].alpha << 24);
		int shft_blue = msb(mask_blue) - 8;
		int shft_green = msb(mask_green) - 8;
		int shft_red = msb(mask_red) - 8;

		for (iy = height; iy; iy--, i += step_up)
			for (ix = 0; ix < width; ix++, i++) {
				uint32_t val;
				Bread(b, c, sizeof(c));
				val =  (uint32_t)c[0] + ((uint32_t)c[1] << 8) +
				      ((uint32_t)c[2] << 16) + ((uint32_t)c[1] << 24);

				buf[i].alpha = 0;
				if (shft_blue >= 0)
					buf[i].blue = (uint8_t)((val & mask_blue) >> shft_blue);
				else
					buf[i].blue = (uint8_t)((val & mask_blue) << -shft_blue);
				if (shft_green >= 0)
					buf[i].green = (uint8_t)((val & mask_green) >> shft_green);
				else
					buf[i].green = (uint8_t)((val & mask_green) << -shft_green);
				if (shft_red >= 0)
					buf[i].red = (uint8_t)((val & mask_red) >> shft_red);
				else
					buf[i].red = (uint8_t)((val & mask_red) << -shft_red);
			}
	} else
		for (iy = height; iy; iy--, i += step_up)
			for (ix = 0; ix < width; ix++, i++) {
				Bread(b, c, 4);
				buf[i].blue = c[0];
				buf[i].green = c[1];
				buf[i].red = c[2];
			}
	return 0;
}


static Rgb*
ReadBMP(Biobuf *b, int *width, int *height)
{
	int colours, num_coltab = 0;
	Filehdr bmfh;
	Infohdr bmih;
	Rgb clut[256];
	Rgb* buf;

	bmfh.type = r16(b);
	if (bmfh.type != 0x4d42) 	/* signature must be 'BM' */
		ERR_OUT("bad magic number, not a BMP file");

	bmfh.size = r32(b);
	bmfh.reserved1 = r16(b);
	bmfh.reserved2 = r16(b);
	bmfh.offbits = r32(b);

	memset(&bmih, 0, sizeof(bmih));
	bmih.size = r32(b);

	if (bmih.size == 0x0c) {			/* OS/2 1.x version */
		bmih.width = r16(b);
		bmih.height = r16(b);
		bmih.planes = r16(b);
		bmih.bpp = r16(b);
		bmih.compression = BMP_RGB;
	} else {				/* Windows */
		bmih.width = r32(b);
		bmih.height = r32(b);
		bmih.planes = r16(b);
		bmih.bpp = r16(b);
		bmih.compression = r32(b);
		bmih.imagesize = r32(b);
		bmih.hres = r32(b);
		bmih.vres = r32(b);
		bmih.colours = r32(b);
		bmih.impcolours = r32(b);
	}

	if (bmih.bpp < 16) {
		/* load colour table */
		if (bmih.impcolours)
			num_coltab = (int)bmih.impcolours;
		else
			num_coltab = 1 << bmih.bpp;
	} else if (bmih.compression == BMP_BITFIELDS &&
	          (bmih.bpp == 16 || bmih.bpp == 32))
		/* load bitmasks */
		num_coltab = 3;

	if (num_coltab) {
		int i;
		Bseek(b, 14 + bmih.size, 0);

		for (i = 0; i < num_coltab; i++) {
			clut[i].blue  = (uint8_t)Bgetc(b);
			clut[i].green = (uint8_t)Bgetc(b);
			clut[i].red   = (uint8_t)Bgetc(b);
			clut[i].alpha = (uint8_t)Bgetc(b);
		}
	}

	*width = bmih.width;
	*height = bmih.height;
	colours = bmih.bpp;

	Bseek(b, bmfh.offbits, 0);

	if ((buf = IND_CALLOC(sizeof(Rgb) * (*width) * abs(*height), 1)) == nil)
		ERR_OUT("no memory");

	switch(colours) {
		case 1:
			load_1T(b, *width, *height, buf, clut);
			break;
		case 4:
			if (bmih.compression == BMP_RLE4)
				load_4C(b, *width, *height, buf, clut);
			else
				load_4T(b, *width, *height, buf, clut);
			break;
		case 8:
			if (bmih.compression == BMP_RLE8)
				load_8C(b, *width, *height, buf, clut);
			else
				load_8T(b, *width, *height, buf, clut);
			break;
		case 16:
			load_16(b, *width, *height, buf,
			        bmih.compression == BMP_BITFIELDS ? clut : nil);
			break;
		case 24:
			load_24T(b, *width, *height, buf);
			break;
		case 32:
			load_32(b, *width, *height, buf,
			        bmih.compression == BMP_BITFIELDS ? clut : nil);
			break;
	}
	return buf;
Err:
	return NULL;
}

Rawimage*
Breadbmp(Biobuf *bp)
{
	Rawimage *a;
	int c, width, height;
	uint8_t *r, *g, *b;
	Rgb *s, *e;
	Rgb *bmp;

	a = nil;
	bmp = nil;

	if ((bmp = ReadBMP(bp, &width, &height)) == nil)
		return nil;

	if ((a = IND_CALLOC(sizeof(Rawimage), 1)) == nil)
		goto Error;

	for (c = 0; c  < 3; c++)
		if ((a->chans[c] = IND_CALLOC(width * height, 1)) == nil)
			goto Error;

	a->nchans = 3;
	a->chandesc = CRGB_;
	a->chanlen = width * height;
	a->width = width;
	a->height = height;

	s = bmp;
	e = s + width * height;
	r = a->chans[0];
	g = a->chans[1];
	b = a->chans[2];

	do {
		*r++ = s->red;
		*g++ = s->green;
		*b++ = s->blue;
	}while(++s < e);

	IND_FREE(bmp);
	return a;

Error:
	if (a)
		for (c = 0; c < 3; c++)
			if (a->chans[c])
				IND_FREE(a->chans[c]);
	if (a)
		IND_FREE(a);
	if (bmp)
		IND_FREE(bmp);
	return nil;

}

Rawimage*
img_decode_bmp(uint8_t *imgbuf, int imglen)
{
	Rawimage *image;
	Biobuf b;

	if (Binit(&b, imgbuf, imglen) < 0)
		return nil;
	image = Breadbmp(&b);
	Bterm(&b);
	return image;
}

int img_info_bmp(uint8_t *imgbuf, int imglen, int *pwidth, int *pheight)
{
	uint8_t buf[4];
	Biobuf b;

	if (imgbuf == 0 || imglen < 0x1A)
		ERR_OUT("imgbuf = %p, imglen = %d\n", imgbuf, imglen);

	if (Binit(&b, imgbuf, imglen) < 0)
		ERR_OUT("Binit\n");
	if (Bread(&b, NULL, 14) != 14)
		ERR_OUT("Bread 0x12\n");

	if (Bread(&b, buf, 4) != 4)
		ERR_OUT("Bread\n");
	if (buf[1] != 0 || buf[2] != 0 || buf[3] != 0)
		ERR_OUT("size not zero!\n");

	if (buf[0] == 0x0c) {			/* OS/2 1.x version */
		if (Bread(&b, buf, 2) != 2)
			ERR_OUT("Bread\n");
		*pwidth = (int)(((uint32_t)buf[1] << 8) + (uint32_t)buf[0]);
		if (Bread(&b, buf, 2) != 2)
			ERR_OUT("Bread\n");
		*pheight = (int)(((uint32_t)buf[1] << 8) + (uint32_t)buf[0]);
	} else if (buf[0] == 0x28) {	/* Windows */
		if (Bread(&b, buf, 4) != 4)
			ERR_OUT("Bread\n");
		*pwidth = (int)(((uint32_t)buf[1] << 8) + (uint32_t)buf[0]);
		if (Bread(&b, buf, 4) != 4)
			ERR_OUT("Bread\n");
		*pheight = (int)(((uint32_t)buf[1] << 8) + (uint32_t)buf[0]);
	} else {
		ERR_OUT("size = 0x%02x!\n", buf[0]);
	}

	PRINTF("width = %d, height = %d\n", *pwidth, *pheight);

	return 0;
Err:
	return -1;
}

