/*
 * Copyright (c) 2014,2015 Hayaki Saito
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#if HAVE_JPEG
# include <stdio.h>
# include <jpeglib.h>
#endif  /* HAVE_JPEG */

#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_FCNTL_H
# include <fcntl.h>
#endif

#if !defined(HAVE_MEMCPY)
# define memcpy(d, s, n) (bcopy ((s), (d), (n)))
#endif

#if !defined(HAVE_MEMMOVE)
# define memmove(d, s, n) (bcopy ((s), (d), (n)))
#endif

#if !defined(O_BINARY) && defined(_O_BINARY)
# define O_BINARY _O_BINARY
#endif  /* !defined(O_BINARY) && !defined(_O_BINARY) */

#include <assert.h>

#define STBI_NO_STDIO 1
#include "stb_image.h"

#ifdef HAVE_GDK_PIXBUF2
# include <gdk-pixbuf/gdk-pixbuf.h>
#endif

#ifdef HAVE_GD
# include <gd.h>
#endif

#ifdef HAVE_LIBCURL
# include <curl/curl.h>
#endif

#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif

#ifdef HAVE_LIBPNG
# include <png.h>
#endif  /* HAVE_LIBPNG */

#include <stdio.h>
#include "frompnm.h"
#include "frame.h"
#include <sixel.h>

#define STBI_NO_STDIO 1
#define STB_IMAGE_IMPLEMENTATION 1
#include "stb_image.h"


typedef struct chunk
{
    unsigned char *buffer;
    size_t size;
    size_t max_size;
} chunk_t;


static void
chunk_init(chunk_t * const pchunk,
           size_t initial_size)
{
    pchunk->max_size = initial_size;
    pchunk->size = 0;
    pchunk->buffer = (unsigned char *)malloc(pchunk->max_size);
}


# ifdef HAVE_LIBCURL
static size_t
memory_write(void *ptr,
             size_t size,
             size_t len,
             void *memory)
{
    size_t nbytes;
    chunk_t *chunk;

    nbytes = size * len;
    if (nbytes == 0) {
        return 0;
    }

    chunk = (chunk_t *)memory;

    if (chunk->max_size <= chunk->size + nbytes) {
        do {
            chunk->max_size *= 2;
        } while (chunk->max_size <= chunk->size + nbytes);
        chunk->buffer = (unsigned char*)realloc(chunk->buffer, chunk->max_size);
    }

    memcpy(chunk->buffer + chunk->size, ptr, nbytes);
    chunk->size += nbytes;

    return nbytes;
}
# endif


static FILE *
open_binary_file(char const *filename)
{
    FILE *f;
#if HAVE_STAT
    struct stat sb;
#endif  /* HAVE_STAT */

    if (filename == NULL || strcmp(filename, "-") == 0) {
        /* for windows */
#if defined(O_BINARY)
# if HAVE__SETMODE
        _setmode(fileno(stdin), O_BINARY);
# elif HAVE_SETMODE
        setmode(fileno(stdin), O_BINARY);
# endif  /* HAVE_SETMODE */
#endif  /* defined(O_BINARY) */
        return stdin;
    }

#if HAVE_STAT
    if (stat(filename, &sb) != 0) {
# if HAVE_ERRNO_H
        fprintf(stderr, "stat() failed.\n" "reason: %s.\n",
                strerror(errno));
# endif  /* HAVE_ERRNO_H */
        return NULL;
    }
    if ((sb.st_mode & S_IFMT) == S_IFDIR) {
        fprintf(stderr, "'%s' is directory.\n", filename);
        return NULL;
    }
#endif  /* HAVE_STAT */

    f = fopen(filename, "rb");
    if (!f) {
#if HAVE_ERRNO_H
        fprintf(stderr, "fopen('%s') failed.\n" "reason: %s.\n",
                filename, strerror(errno));
#endif  /* HAVE_ERRNO_H */
        return NULL;
    }
    return f;
}


static int
get_chunk_from_file(char const *filename, chunk_t *pchunk)
{
    FILE *f;
    int n;

    f = open_binary_file(filename);
    if (!f) {
        return (-1);
    }

    chunk_init(pchunk, 64 * 1024);
    if (pchunk->buffer == NULL) {
#if HAVE_ERRNO_H
        fprintf(stderr, "get_chunk_from_file('%s'): malloc failed.\n" "reason: %s.\n",
                filename, strerror(errno));
#endif  /* HAVE_ERRNO_H */
        return (-1);
    }

    for (;;) {
        if (pchunk->max_size - pchunk->size < 4096) {
            pchunk->max_size *= 2;
            pchunk->buffer = (unsigned char *)realloc(pchunk->buffer, pchunk->max_size);
            if (pchunk->buffer == NULL) {
#if HAVE_ERRNO_H
                fprintf(stderr, "get_chunk_from_file('%s'): relloc failed.\n" "reason: %s.\n",
                        filename, strerror(errno));
#endif  /* HAVE_ERRNO_H */
                return (-1);
            }
        }
        n = fread(pchunk->buffer + pchunk->size, 1, 4096, f);
        if (n <= 0) {
            break;
        }
        pchunk->size += n;
    }

    if (f != stdin) {
        fclose(f);
    }
    return 0;
}


# ifdef HAVE_LIBCURL
static int
get_chunk_from_url(char const *url, chunk_t *pchunk)
{
    CURL *curl;
    CURLcode code;

    chunk_init(pchunk, 1024);
    if (pchunk->buffer == NULL) {
#if HAVE_ERRNO_H
        fprintf(stderr, "get_chunk_from_url('%s'): malloc failed.\n" "reason: %s.\n",
                url, strerror(errno));
#endif  /* HAVE_ERRNO_H */
        return (-1);
    }
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (strncmp(url, "https://", 8) == 0) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memory_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)pchunk);
    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform('%s') failed.\n" "code: %d.\n",
                url, code);
        curl_easy_cleanup(curl);
        return (-1);
    }
    curl_easy_cleanup(curl);
    return 0;
}
# endif  /* HAVE_LIBCURL */


# if HAVE_JPEG
/* import from @uobikiemukot's sdump loader.h */
static unsigned char *
load_jpeg(unsigned char *data,
          int datasize,
          int *pwidth,
          int *pheight,
          int *ppixelformat)
{
    int row_stride, size;
    unsigned char *result = NULL;
    JSAMPARRAY buffer;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr pub;

    cinfo.err = jpeg_std_error(&pub);

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, datasize);
    jpeg_read_header(&cinfo, TRUE);

    /* disable colormap (indexed color), grayscale -> rgb */
    cinfo.quantize_colors = FALSE;
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    if (cinfo.output_components != 3) {
        fprintf(stderr, "load_jpeg() failed(unknown format).\n");
        goto end;
    }

    *ppixelformat = PIXELFORMAT_RGB888;
    *pwidth = cinfo.output_width;
    *pheight = cinfo.output_height;

    size = *pwidth * *pheight * cinfo.output_components;
    result = (unsigned char *)malloc(size);
    if (result == NULL) {
#if HAVE_ERRNO_H
        fprintf(stderr, "load_jpeg() failed.\n" "reason: %s.\n",
                strerror(errno));
#endif  /* HAVE_ERRNO_H */
        goto end;
    }

    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        memcpy(result + (cinfo.output_scanline - 1) * row_stride, buffer[0], row_stride);
    }

end:
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return result;
}
# endif  /* HAVE_JPEG */


# if HAVE_LIBPNG
static void
read_png(png_structp png_ptr,
         png_bytep data,
         png_size_t length)
{
    chunk_t *pchunk = png_get_io_ptr(png_ptr);
    if (length > pchunk->size) {
        length = pchunk->size;
    }
    if (length > 0) {
        memcpy(data, pchunk->buffer, length);
        pchunk->buffer += length;
        pchunk->size -= length;
    }
}


static void
read_palette(png_structp png_ptr,
             png_infop info_ptr,
             unsigned char *palette,
             int ncolors,
             png_color *png_palette,
             png_color_16 *pbackground,
             int *transparent)
{
    png_bytep trans = NULL;
    int num_trans = 0;
    int i;

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, NULL);
    }
    if (num_trans > 0) {
        *transparent = trans[0];
    }
    for (i = 0; i < ncolors; ++i) {
        if (pbackground && i < num_trans) {
            palette[i * 3 + 0] = ((0xff - trans[i]) * pbackground->red
                                   + trans[i] * png_palette[i].red) >> 8;
            palette[i * 3 + 1] = ((0xff - trans[i]) * pbackground->green
                                   + trans[i] * png_palette[i].green) >> 8;
            palette[i * 3 + 2] = ((0xff - trans[i]) * pbackground->blue
                                   + trans[i] * png_palette[i].blue) >> 8;
        } else {
            palette[i * 3 + 0] = png_palette[i].red;
            palette[i * 3 + 1] = png_palette[i].green;
            palette[i * 3 + 2] = png_palette[i].blue;
        }
    }
}



static unsigned char *
load_png(unsigned char *buffer,
         int size,
         int *psx,
         int *psy,
         unsigned char **ppalette,
         int *pncolors,
         int reqcolors,
         int *pixelformat,
         unsigned char *bgcolor,
         int *transparent)
{
    chunk_t read_chunk;
    png_uint_32 bitdepth;
    png_uint_32 png_status;
    png_structp png_ptr;
    png_infop info_ptr;
    unsigned char **rows = NULL;
    unsigned char *result = NULL;
    png_color *png_palette = NULL;
    png_color_16 background;
    png_color_16p default_background;
    int i;
    int depth;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "png_create_read_struct failed.\n");
        goto cleanup;
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "png_create_info_struct failed.\n");
        png_destroy_read_struct(&png_ptr, (png_infopp)0, (png_infopp)0);
        goto cleanup;
    }
    read_chunk.buffer = buffer;
    read_chunk.size = size;

    png_set_read_fn(png_ptr,(png_voidp)&read_chunk, read_png);
    png_read_info(png_ptr, info_ptr);
    *psx = png_get_image_width(png_ptr, info_ptr);
    *psy = png_get_image_height(png_ptr, info_ptr);
    bitdepth = png_get_bit_depth(png_ptr, info_ptr);
    if (bitdepth == 16) {
#  if HAVE_DEBUG
        fprintf(stderr, "bitdepth: %u\n", (unsigned int)bitdepth);
        fprintf(stderr, "stripping to 8bit...\n");
#  endif
        png_set_strip_16(png_ptr);
        bitdepth = 8;
    }

    if (bgcolor) {
        background.red = bgcolor[0];
        background.green = bgcolor[1];
        background.blue = bgcolor[2];
        background.gray = (bgcolor[0] + bgcolor[1] + bgcolor[2]) / 3;
    } else if (png_get_bKGD(png_ptr, info_ptr, &default_background) == PNG_INFO_bKGD) {
        memcpy(&background, default_background, sizeof(background));
    } else {
        background.red = 0;
        background.green = 0;
        background.blue = 0;
        background.gray = 0;
    }

    switch (png_get_color_type(png_ptr, info_ptr)) {
    case PNG_COLOR_TYPE_PALETTE:
#  if HAVE_DEBUG
        fprintf(stderr, "paletted PNG(PNG_COLOR_TYPE_PALETTE)\n");
#  endif
        png_status = png_get_PLTE(png_ptr, info_ptr,
                                  &png_palette, pncolors);
        if (png_status != PNG_INFO_PLTE || png_palette == NULL) {
            fprintf(stderr, "PLTE chunk not found\n");
            goto cleanup;
        }
#  if HAVE_DEBUG
        fprintf(stderr, "palette colors: %d\n", *pncolors);
        fprintf(stderr, "bitdepth: %u\n", (unsigned int)bitdepth);
#  endif
        if (ppalette == NULL || *pncolors > reqcolors) {
#  if HAVE_DEBUG
            fprintf(stderr, "detected more colors than reqired(>%d).\n",
                    reqcolors);
            fprintf(stderr, "expand to RGB format...\n");
#  endif
            png_set_background(png_ptr, &background,
                               PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
            png_set_palette_to_rgb(png_ptr);
            png_set_strip_alpha(png_ptr);
            *pixelformat = PIXELFORMAT_RGB888;
        } else {
            switch (bitdepth) {
            case 1:
                *ppalette = malloc(*pncolors * 3);
                if (*ppalette == NULL) {
                    goto cleanup;
                }
                read_palette(png_ptr, info_ptr, *ppalette,
                             *pncolors, png_palette, &background, transparent);
                *pixelformat = PIXELFORMAT_PAL1;
                break;
            case 2:
                *ppalette = malloc(*pncolors * 3);
                if (*ppalette == NULL) {
                    goto cleanup;
                }
                read_palette(png_ptr, info_ptr, *ppalette,
                             *pncolors, png_palette, &background, transparent);
                *pixelformat = PIXELFORMAT_PAL2;
                break;
            case 4:
                *ppalette = malloc(*pncolors * 3);
                if (*ppalette == NULL) {
                    goto cleanup;
                }
                read_palette(png_ptr, info_ptr, *ppalette,
                             *pncolors, png_palette, &background, transparent);
                *pixelformat = PIXELFORMAT_PAL4;
                break;
            case 8:
                *ppalette = malloc(*pncolors * 3);
                if (*ppalette == NULL) {
                    goto cleanup;
                }
                read_palette(png_ptr, info_ptr, *ppalette,
                             *pncolors, png_palette, &background, transparent);
                *pixelformat = PIXELFORMAT_PAL8;
                break;
            default:
                png_set_background(png_ptr, &background,
                                   PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
                png_set_palette_to_rgb(png_ptr);
                *pixelformat = PIXELFORMAT_RGB888;
                break;
            }
        }
        break;
    case PNG_COLOR_TYPE_GRAY:
#  if HAVE_DEBUG
        fprintf(stderr, "grayscale PNG(PNG_COLOR_TYPE_GRAY)\n");
        fprintf(stderr, "bitdepth: %u\n", (unsigned int)bitdepth);
#  endif
        if (1 << bitdepth > reqcolors) {
#  if HAVE_DEBUG
            fprintf(stderr, "detected more colors than reqired(>%d).\n",
                    reqcolors);
            fprintf(stderr, "expand into RGB format...\n");
#  endif
            png_set_background(png_ptr, &background,
                               PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
            png_set_gray_to_rgb(png_ptr);
            *pixelformat = PIXELFORMAT_RGB888;
        } else {
            switch (bitdepth) {
            case 1:
            case 2:
            case 4:
#  if HAVE_DECL_PNG_SET_EXPAND_GRAY_1_2_4_TO_8
#   if HAVE_DEBUG
                fprintf(stderr, "expand %u bpp to 8bpp format...\n",
                        (unsigned int)bitdepth);
#   endif
                png_set_expand_gray_1_2_4_to_8(png_ptr);
                *pixelformat = PIXELFORMAT_G8;
#  elif HAVE_DECL_PNG_SET_GRAY_1_2_4_TO_8
#   if HAVE_DEBUG
                fprintf(stderr, "expand %u bpp to 8bpp format...\n",
                        (unsigned int)bitdepth);
#   endif
                png_set_gray_1_2_4_to_8(png_ptr);
                *pixelformat = PIXELFORMAT_G8;
#  else
#   if HAVE_DEBUG
                fprintf(stderr, "expand into RGB format...\n");
#   endif
                png_set_background(png_ptr, &background,
                                   PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
                png_set_gray_to_rgb(png_ptr);
                *pixelformat = PIXELFORMAT_RGB888;
#  endif
                break;
            case 8:
                if (ppalette) {
                    *pixelformat = PIXELFORMAT_G8;
                } else {
#  if HAVE_DEBUG
                    fprintf(stderr, "expand into RGB format...\n");
#  endif
                    png_set_background(png_ptr, &background,
                                       PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
                    png_set_gray_to_rgb(png_ptr);
                    *pixelformat = PIXELFORMAT_RGB888;
                }
                break;
            default:
#  if HAVE_DEBUG
                fprintf(stderr, "expand into RGB format...\n");
#  endif
                png_set_background(png_ptr, &background,
                                   PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
                png_set_gray_to_rgb(png_ptr);
                *pixelformat = PIXELFORMAT_RGB888;
                break;
            }
        }
        break;
    case PNG_COLOR_TYPE_GRAY_ALPHA:
#  if HAVE_DEBUG
        fprintf(stderr, "grayscale-alpha PNG(PNG_COLOR_TYPE_GRAY_ALPHA)\n");
        fprintf(stderr, "bitdepth: %u\n", (unsigned int)bitdepth);
        fprintf(stderr, "expand to RGB format...\n");
#  endif
        png_set_background(png_ptr, &background,
                           PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
        png_set_gray_to_rgb(png_ptr);
        *pixelformat = PIXELFORMAT_RGB888;
        break;
    case PNG_COLOR_TYPE_RGB_ALPHA:
#  if HAVE_DEBUG
        fprintf(stderr, "RGBA PNG(PNG_COLOR_TYPE_RGB_ALPHA)\n");
        fprintf(stderr, "bitdepth: %u\n", (unsigned int)bitdepth);
        fprintf(stderr, "expand to RGB format...\n");
#  endif
        png_set_background(png_ptr, &background,
                           PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
        *pixelformat = PIXELFORMAT_RGB888;
        break;
    case PNG_COLOR_TYPE_RGB:
#  if HAVE_DEBUG
        fprintf(stderr, "RGB PNG(PNG_COLOR_TYPE_RGB)\n");
        fprintf(stderr, "bitdepth: %u\n", (unsigned int)bitdepth);
#  endif
        png_set_background(png_ptr, &background,
                           PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
        *pixelformat = PIXELFORMAT_RGB888;
        break;
    default:
        /* unknown format */
        goto cleanup;
    }
    depth = sixel_helper_compute_depth(*pixelformat);
    result = malloc(*psx * *psy * depth);
    if (result == NULL) {
        goto cleanup;
    }
    rows = malloc(*psy * sizeof(unsigned char *));
    if (rows == NULL) {
        goto cleanup;
    }
    switch (*pixelformat) {
    case PIXELFORMAT_PAL1:
    case PIXELFORMAT_PAL2:
    case PIXELFORMAT_PAL4:
        for (i = 0; i < *psy; ++i) {
            rows[i] = result + (depth * *psx * bitdepth + 7) / 8 * i;
        }
        break;
    default:
        for (i = 0; i < *psy; ++i) {
            rows[i] = result + depth * *psx * i;
        }
        break;
    }
#if USE_SETJMP && HAVE_SETJMP
    if (setjmp(png_jmpbuf(png_ptr))) {
        free(result);
        result = NULL;
        goto cleanup;
    }
#endif  /* HAVE_SETJMP */
    png_read_image(png_ptr, rows);
cleanup:
    png_destroy_read_struct(&png_ptr, &info_ptr,(png_infopp)0);
    free(rows);

    return result;
}
# endif  /* HAVE_PNG */


static unsigned char *
load_sixel(unsigned char *buffer,
           int size,
           int *psx,
           int *psy,
           unsigned char **ppalette,
           int *pncolors,
           int reqcolors,
           int *ppixelformat)
{
    unsigned char *p = NULL;
    unsigned char *pixels = NULL;
    unsigned char *palette = NULL;
    int colors;
    int i;
    int ret;

    /* sixel */
    ret = sixel_decode(buffer, size,
                       &p, psx, psy,
                       &palette, &colors, malloc);
    if (ret != 0) {
#if HAVE_ERRNO_H
            fprintf(stderr, "sixel_decode failed.\n" "reason: %s.\n",
                    strerror(errno));
#endif  /* HAVE_ERRNO_H */
        return NULL;
    }
    if (ppalette == NULL || colors > reqcolors) {
        *ppixelformat = PIXELFORMAT_RGB888;
        pixels = malloc(*psx * *psy * 3);
        if (pixels == NULL) {
            goto cleanup;
        }
        for (i = 0; i < *psx * *psy; ++i) {
            pixels[i * 3 + 0] = palette[p[i] * 3 + 0];
            pixels[i * 3 + 1] = palette[p[i] * 3 + 1];
            pixels[i * 3 + 2] = palette[p[i] * 3 + 2];
        }
    } else {
        *ppixelformat = PIXELFORMAT_PAL8;
        pixels = p;
        *ppalette = palette;
        *pncolors = colors;
        p = NULL;
        palette = NULL;
    }

cleanup:
    free(palette);
    free(p);

    return pixels;
}


static int
get_chunk(char const *filename, chunk_t *pchunk)
{
    if (filename != NULL && strstr(filename, "://")) {
# ifdef HAVE_LIBCURL
        return get_chunk_from_url(filename, pchunk);
# else
        fprintf(stderr, "To specify URI schemes, you have to "
                        "configure this program with --with-libcurl "
                        "option at compile time.\n");
        return (-1);
# endif  /* HAVE_LIBCURL */
    }
    return get_chunk_from_file(filename, pchunk);
}


static int
chunk_is_sixel(chunk_t const *chunk)
{
    unsigned char *p;
    unsigned char *end;

    p = chunk->buffer;
    end = p + chunk->size;

    if (chunk->size < 3) {
        return 0;
    }

    p++;
    if (p >= end) {
        return 0;
    }
    if (*(p - 1) == 0x90 || (*(p - 1) == 0x1b && *p == 0x50)) {
        while (p++ < end) {
            if (*p == 0x71) {
                return 1;
            } else if (*p == 0x18 || *p == 0x1a) {
                return 0;
            } else if (*p < 0x20) {
                continue;
            } else if (*p < 0x30) {
                return 0;
            } else if (*p < 0x40) {
                continue;
            }
        }
    }
    return 0;
}


static int
chunk_is_pnm(chunk_t const *chunk)
{
    if (chunk->size < 2) {
        return 0;
    }
    if (chunk->buffer[0] == 'P' &&
        chunk->buffer[1] >= '1' &&
        chunk->buffer[1] <= '6') {
        return 1;
    }
    return 0;
}


#if HAVE_LIBPNG
static int
chunk_is_png(chunk_t const *chunk)
{
    if (chunk->size < 8) {
        return 0;
    }
    if (png_check_sig(chunk->buffer, 8)) {
        return 1;
    }
    return 0;
}
#endif  /* HAVE_LIBPNG */


static int
chunk_is_gif(chunk_t const *chunk)
{
    if (chunk->size < 6) {
        return 0;
    }
    if (chunk->buffer[0] == 'G' &&
        chunk->buffer[1] == 'I' &&
        chunk->buffer[2] == 'F' &&
        chunk->buffer[3] == '8' &&
        (chunk->buffer[4] == '7' || chunk->buffer[4] == '9') &&
        chunk->buffer[5] == 'a') {
        return 1;
    }
    return 0;
}


#if HAVE_JPEG
static int
chunk_is_jpeg(chunk_t const *chunk)
{
    if (chunk->size < 2) {
        return 0;
    }
    if (memcmp("\xFF\xD8", chunk->buffer, 2) == 0) {
        return 1;
    }
    return 0;
}
#endif  /* HAVE_JPEG */



int
load_with_builtin(
    chunk_t const             /* in */     *pchunk,      /* image data */
    int                       /* in */     fstatic,      /* static */
    int                       /* in */     fuse_palette, /* whether to use palette if possible */
    int                       /* in */     reqcolors,    /* reqcolors */
    unsigned char             /* in */     *bgcolor,     /* background color */
    int                       /* in */     loop_control, /* one of enum loop_control */
    sixel_load_image_function /* in */     fn_load,      /* callback */
    void                      /* in/out */ *context      /* private data for callback */
)
{
    unsigned char *p;
    static stbi__context s;
    static stbi__gif g;
    int depth;
    sixel_frame_t *frame;
    int ret = (-1);
    int i;

    frame = sixel_frame_create();
    if (frame == NULL) {
        return SIXEL_FAILED;
    }

    if (chunk_is_sixel(pchunk)) {
        frame->pixels = load_sixel(pchunk->buffer,
                                   pchunk->size,
                                   &frame->width,
                                   &frame->height,
                                   fuse_palette ? &frame->palette: NULL,
                                   &frame->ncolors,
                                   reqcolors,
                                   &frame->pixelformat);
        if (frame->pixels == NULL) {
            goto error;
        }
    } else if (chunk_is_pnm(pchunk)) {
        /* pnm */
        frame->pixels = load_pnm(pchunk->buffer,
                                 pchunk->size,
                                 &frame->width,
                                 &frame->height,
                                 fuse_palette ? &frame->palette: NULL,
                                 &frame->ncolors,
                                 &frame->pixelformat);
        if (!frame->pixels) {
#if HAVE_ERRNO_H
            fprintf(stderr, "load_pnm failed.\n" "reason: %s.\n",
                    strerror(errno));
#endif  /* HAVE_ERRNO_H */
            goto error;
        }
    }
#if HAVE_JPEG
    else if (chunk_is_jpeg(pchunk)) {
        frame->pixels = load_jpeg(pchunk->buffer,
                                  pchunk->size,
                                  &frame->width,
                                  &frame->height,
                                  &frame->pixelformat);
        if (frame->pixels == NULL) {
            goto error;
        }
    }
#endif  /* HAVE_JPEG */
#if HAVE_LIBPNG
    else if (chunk_is_png(pchunk)) {
        frame->pixels = load_png(pchunk->buffer,
                                 pchunk->size,
                                 &frame->width,
                                 &frame->height,
                                 fuse_palette ? &frame->palette: NULL,
                                 &frame->ncolors,
                                 reqcolors,
                                 &frame->pixelformat,
                                 bgcolor,
                                 &frame->transparent);
        if (frame->pixels == NULL) {
            goto error;
        }
    }
#endif  /* HAVE_LIBPNG */
    else if (chunk_is_gif(pchunk)) {

        frame->loop_count = 0;

        for (;;) {

            stbi__start_mem(&s, pchunk->buffer, pchunk->size);
            memset(&g, 0, sizeof(g));
            g.loop_count = (-1);
            frame->frame_no = 0;

            for (;;) {
                p = stbi__gif_load_next(&s, &g, &depth, 4, bgcolor);
                if (p == (void *)&s) {
                    break;
                }
                if (p == NULL) {
                    frame->pixels = NULL;
                    break;
                }

                frame->width = g.w;
                frame->height = g.h;
                frame->delay = g.delay;
                frame->ncolors = 2 << (g.flags & 7);
                frame->palette = malloc(frame->ncolors * 3);
                if (frame->palette == NULL) {
                    goto error;
                }
                if (frame->ncolors <= reqcolors && fuse_palette) {
                    frame->pixelformat = PIXELFORMAT_PAL8;
                    frame->pixels = malloc(frame->width * frame->height);
                    memcpy(frame->pixels, g.out, frame->width * frame->height);
                    for (i = 0; i < frame->ncolors; ++i) {
                        frame->palette[i * 3 + 0] = g.color_table[i * 4 + 2];
                        frame->palette[i * 3 + 1] = g.color_table[i * 4 + 1];
                        frame->palette[i * 3 + 2] = g.color_table[i * 4 + 0];
                    }
                    if (g.lflags & 0x80) {
                        if (g.eflags & 0x01) {
                            if (bgcolor) {
                                frame->palette[g.transparent * 3 + 0]
                                    = bgcolor[0];
                                frame->palette[g.transparent * 3 + 1]
                                    = bgcolor[1];
                                frame->palette[g.transparent * 3 + 2]
                                    = bgcolor[2];
                            } else {
                                frame->transparent = g.transparent;
                            }
                        }
                    } else if (g.flags & 0x80) {
                        if (g.eflags & 0x01) {
                            if (bgcolor) {
                                frame->palette[g.transparent * 3 + 0]
                                    = bgcolor[0];
                                frame->palette[g.transparent * 3 + 1]
                                    = bgcolor[1];
                                frame->palette[g.transparent * 3 + 2]
                                    = bgcolor[2];
                            } else {
                                frame->transparent = g.transparent;
                            }
                        }
                    }
                } else {
                    frame->pixelformat = PIXELFORMAT_RGB888;
                    frame->pixels = malloc(g.w * g.h * 3);
                    for (i = 0; i < g.w * g.h; ++i) {
                        frame->pixels[i * 3 + 0] = g.color_table[p[i] * 4 + 2];
                        frame->pixels[i * 3 + 1] = g.color_table[p[i] * 4 + 1];
                        frame->pixels[i * 3 + 2] = g.color_table[p[i] * 4 + 0];
                    }
                }
                if (frame->pixels == NULL) {
                    fprintf(stderr, "stbi_load_from_file failed.\n" "reason: %s.\n",
                            stbi_failure_reason());
                    goto error;
                }
                frame->multiframe = (g.loop_count != (-1));

                ret = fn_load(frame, context);
                if (ret != 0) {
                    goto error;
                }
                ++frame->frame_no;
                if (fstatic) {
                    break;
                }
            }

            free(g.out);

            ++frame->loop_count;

            if (g.loop_count == (-1)) {
                break;
            }
            if (loop_control == LOOP_DISABLE || frame->frame_no == 1) {
                break;
            }
            if (loop_control == LOOP_AUTO && frame->loop_count == g.loop_count) {
                break;
            }
        }

        return 0;
    } else {
        stbi__start_mem(&s, pchunk->buffer, pchunk->size);
        frame->pixels = stbi_load_main(&s, &frame->width, &frame->height, &depth, 3);
        if (!frame->pixels) {
            fprintf(stderr, "stbi_load_from_file failed.\n" "reason: %s.\n",
                    stbi_failure_reason());
            goto error;
        }
        frame->loop_count = 1;

        switch (depth) {
        case 1:
        case 3:
        case 4:
            frame->pixelformat = PIXELFORMAT_RGB888;
            break;
        default:
            fprintf(stderr, "load_with_builtin() failed.\n"
                            "reason: unknown pixel-format.(depth: %d)\n", depth);
            goto error;
        }
    }

    ret = sixel_strip_alpha(frame, bgcolor);
    if (ret != 0) {
        goto error;
    }

    ret = fn_load(frame, context);
    if (ret != 0) {
        goto error;
    }

error:
    sixel_frame_unref(frame);

    return ret;
}


#ifdef HAVE_GDK_PIXBUF2
static int
load_with_gdkpixbuf(
    chunk_t const             /* in */     *pchunk,      /* image data */
    int                       /* in */     fstatic,      /* static */
    int                       /* in */     fuse_palette, /* whether to use palette if possible */
    int                       /* in */     reqcolors,    /* reqcolors */
    unsigned char             /* in */     *bgcolor,     /* background color */
    int                       /* in */     loop_control, /* one of enum loop_control */
    sixel_load_image_function /* in */     fn_load,      /* callback */
    void                      /* in/out */ *context      /* private data for callback */
)
{
    GdkPixbuf *pixbuf;
    GdkPixbufAnimation *animation;
    GdkPixbufLoader *loader;
#if 1
    GdkPixbufAnimationIter *it;
    GTimeVal time;
#endif
    sixel_frame_t *frame;
    int stride;
    int ret = SIXEL_FAILED;

    (void) fuse_palette;
    (void) reqcolors;
    (void) bgcolor;

    frame = sixel_frame_create();
    if (frame == NULL) {
        return SIXEL_FAILED;
    }

#if (!GLIB_CHECK_VERSION(2, 36, 0))
    g_type_init();
#endif
    g_get_current_time(&time);
    loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, pchunk->buffer, pchunk->size, NULL);
    animation = gdk_pixbuf_loader_get_animation(loader);
    if (!animation || fstatic || gdk_pixbuf_animation_is_static_image(animation)) {
        pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pixbuf == NULL) {
            goto end;
        }
        frame->frame_no = 0;
        frame->width = gdk_pixbuf_get_width(pixbuf);
        frame->height = gdk_pixbuf_get_height(pixbuf);
        stride = gdk_pixbuf_get_rowstride(pixbuf);
        frame->pixels = malloc(frame->height * stride);
        if (frame->pixels == NULL) {
            goto end;
        }
        memcpy(frame->pixels, gdk_pixbuf_get_pixels(pixbuf),
               frame->height * stride);
        if (gdk_pixbuf_get_has_alpha(pixbuf)) {
            frame->pixelformat = PIXELFORMAT_RGBA8888;
        } else {
            frame->pixelformat = PIXELFORMAT_RGB888;
        }
        ret = fn_load(frame, context);
        if (ret != SIXEL_SUCCESS) {
            goto end;
        }
    } else {
        g_get_current_time(&time);

        frame->frame_no = 0;

        it = gdk_pixbuf_animation_get_iter(animation, &time);
        for (;;) {
            while (!gdk_pixbuf_animation_iter_on_currently_loading_frame(it)) {
                frame->delay = gdk_pixbuf_animation_iter_get_delay_time(it);
                g_time_val_add(&time, frame->delay * 1000);
                frame->delay /= 10;
                pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(it);
                if (pixbuf == NULL) {
                    break;
                }
                frame->width = gdk_pixbuf_get_width(pixbuf);
                frame->height = gdk_pixbuf_get_height(pixbuf);
                stride = gdk_pixbuf_get_rowstride(pixbuf);
                frame->pixels = malloc(frame->height * stride);
                if (frame->pixels == NULL) {
                    goto end;
                }
                memcpy(frame->pixels, gdk_pixbuf_get_pixels(pixbuf),
                       frame->height * stride);
                if (gdk_pixbuf_get_has_alpha(pixbuf)) {
                    frame->pixelformat = PIXELFORMAT_RGBA8888;
                } else {
                    frame->pixelformat = PIXELFORMAT_RGB888;
                }
                frame->multiframe = 1;
                gdk_pixbuf_animation_iter_advance(it, &time);
                ret = fn_load(frame, context);
                if (ret != SIXEL_SUCCESS) {
                    goto end;
                }
                frame->frame_no++;
            }

            ++frame->loop_count;

            if (loop_control == LOOP_DISABLE || frame->frame_no == 1) {
                break;
            }
            /* TODO: get loop property */
            if (loop_control == LOOP_AUTO && frame->loop_count == 1) {
                break;
            }
        }
    }

    ret = SIXEL_SUCCESS;

end:
    gdk_pixbuf_loader_close(loader, NULL);
    g_object_unref(loader);
    free(frame->pixels);
    free(frame->palette);
    free(frame);

    return ret;

}
#endif  /* HAVE_GDK_PIXBUF2 */

#ifdef HAVE_GD
static int
detect_file_format(int len, unsigned char *data)
{
    if (memcmp("TRUEVISION", data + len - 18, 10) == 0) {
        return FORMAT_TGA;
    }

    if (memcmp("GIF", data, 3) == 0) {
        return FORMAT_GIF;
    }

    if (memcmp("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", data, 8) == 0) {
        return FORMAT_PNG;
    }

    if (memcmp("BM", data, 2) == 0) {
        return FORMAT_BMP;
    }

    if (memcmp("\xFF\xD8", data, 2) == 0) {
        return FORMAT_JPG;
    }

    if (memcmp("\x00\x00", data, 2) == 0) {
        return FORMAT_WBMP;
    }

    if (memcmp("\x4D\x4D", data, 2) == 0) {
        return FORMAT_TIFF;
    }

    if (memcmp("\x49\x49", data, 2) == 0) {
        return FORMAT_TIFF;
    }

    if (memcmp("\033P", data, 2) == 0) {
        return FORMAT_SIXEL;
    }

    if (data[0] == 0x90  && (data[len-1] == 0x9C || data[len-2] == 0x9C)) {
        return FORMAT_SIXEL;
    }

    if (data[0] == 'P' && data[1] >= '1' && data[1] <= '6') {
        return FORMAT_PNM;
    }

    if (memcmp("gd2", data, 3) == 0) {
        return FORMAT_GD2;
    }

    if (memcmp("8BPS", data, 4) == 0) {
        return FORMAT_PSD;
    }

    if (memcmp("#?RADIANCE\n", data, 11) == 0) {
        return FORMAT_HDR;
    }

    return (-1);
}


static int
load_with_gd(
    chunk_t const             /* in */     *pchunk,      /* image data */
    int                       /* in */     fstatic,      /* static */
    int                       /* in */     fuse_palette, /* whether to use palette if possible */
    int                       /* in */     reqcolors,    /* reqcolors */
    unsigned char             /* in */     *bgcolor,     /* background color */
    int                       /* in */     loop_control, /* one of enum loop_control */
    sixel_load_image_function /* in */     fn_load,      /* callback */
    void                      /* in/out */ *context      /* private data for callback */
)
{
    unsigned char *p;
    gdImagePtr im;
    int x, y;
    int c;
    sixel_frame_t *frame;

    (void) fstatic;
    (void) fuse_palette;
    (void) reqcolors;
    (void) bgcolor;
    (void) loop_control;

    frame = sixel_frame_create();
    if (frame == NULL) {
        return SIXEL_FAILED;
    }

    switch(detect_file_format(pchunk->size, pchunk->buffer)) {
#if 0
# if HAVE_DECL_GDIMAGECREATEFROMGIFPTR
        case FORMAT_GIF:
            im = gdImageCreateFromGifPtr(pchunk->size, pchunk->buffer);
            break;
# endif  /* HAVE_DECL_GDIMAGECREATEFROMGIFPTR */
#endif
#if HAVE_DECL_GDIMAGECREATEFROMPNGPTR
        case FORMAT_PNG:
            im = gdImageCreateFromPngPtr(pchunk->size, pchunk->buffer);
            break;
#endif  /* HAVE_DECL_GDIMAGECREATEFROMPNGPTR */
#if HAVE_DECL_GDIMAGECREATEFROMBMPPTR
        case FORMAT_BMP:
            im = gdImageCreateFromBmpPtr(pchunk->size, pchunk->buffer);
            break;
#endif  /* HAVE_DECL_GDIMAGECREATEFROMBMPPTR */
        case FORMAT_JPG:
#if HAVE_DECL_GDIMAGECREATEFROMJPEGPTREX
            im = gdImageCreateFromJpegPtrEx(pchunk->size, pchunk->buffer, 1);
#elif HAVE_DECL_GDIMAGECREATEFROMJPEGPTR
            im = gdImageCreateFromJpegPtr(pchunk->size, pchunk->buffer);
#endif  /* HAVE_DECL_GDIMAGECREATEFROMJPEGPTREX */
            break;
#if HAVE_DECL_GDIMAGECREATEFROMTGAPTR
        case FORMAT_TGA:
            im = gdImageCreateFromTgaPtr(pchunk->size, pchunk->buffer);
            break;
#endif  /* HAVE_DECL_GDIMAGECREATEFROMTGAPTR */
#if HAVE_DECL_GDIMAGECREATEFROMWBMPPTR
        case FORMAT_WBMP:
            im = gdImageCreateFromWBMPPtr(pchunk->size, pchunk->buffer);
            break;
#endif  /* HAVE_DECL_GDIMAGECREATEFROMWBMPPTR */
#if HAVE_DECL_GDIMAGECREATEFROMTIFFPTR
        case FORMAT_TIFF:
            im = gdImageCreateFromTiffPtr(pchunk->size, pchunk->buffer);
            break;
#endif  /* HAVE_DECL_GDIMAGECREATEFROMTIFFPTR */
#if HAVE_DECL_GDIMAGECREATEFROMGD2PTR
        case FORMAT_GD2:
            im = gdImageCreateFromGd2Ptr(pchunk->size, pchunk->buffer);
            break;
#endif  /* HAVE_DECL_GDIMAGECREATEFROMGD2PTR */
        default:
            return SIXEL_FAILED;
    }

    if (im == NULL) {
        return SIXEL_FAILED;
    }

    if (!gdImageTrueColor(im)) {
#if HAVE_DECL_GDIMAGEPALETTETOTRUECOLOR
        if (!gdImagePaletteToTrueColor(im)) {
            return SIXEL_FAILED;
        }
#else
        return SIXEL_FAILED;
#endif
    }

    frame->width = gdImageSX(im);
    frame->height = gdImageSY(im);
    frame->pixelformat = PIXELFORMAT_RGB888;
    p = frame->pixels = malloc(frame->width * frame->height * 3);
    if (frame->pixels == NULL) {
#if HAVE_ERRNO_H
        fprintf(stderr, "load_with_gd failed.\n" "reason: %s.\n",
                strerror(errno));
#endif  /* HAVE_ERRNO_H */
        gdImageDestroy(im);
        return SIXEL_FAILED;
    }
    for (y = 0; y < frame->height; y++) {
        for (x = 0; x < frame->width; x++) {
            c = gdImageTrueColorPixel(im, x, y);
            *p++ = gdTrueColorGetRed(c);
            *p++ = gdTrueColorGetGreen(c);
            *p++ = gdTrueColorGetBlue(c);
        }
    }
    gdImageDestroy(im);

    return fn_load(frame, context);
}

#endif  /* HAVE_GD */


/* load image from file */

int
sixel_helper_load_image_file(
    char const                /* in */     *filename,     /* source file name */
    int                       /* in */     fstatic,       /* whether to extract static image */
    int                       /* in */     fuse_palette,  /* whether to use paletted image */
    int                       /* in */     reqcolors,     /* requested number of colors */
    unsigned char             /* in */     *bgcolor,      /* background color */
    int                       /* in */     loop_control,  /* one of enum loopControl */
    sixel_load_image_function /* in */     fn_load,       /* callback */
    void                      /* in/out */ *context       /* private data */
)
{
    int ret = (-1);
    chunk_t chunk;

    ret = get_chunk(filename, &chunk);
    if (ret != 0) {
        return ret;
    }

    /* if input date is empty or 1 byte LF, ignore it and return successfully */
    if (chunk.size == 0 || (chunk.size == 1 && *chunk.buffer == '\n')) {
        return 0;
    }

    ret = (-1);
#ifdef HAVE_GDK_PIXBUF2
    if (ret != 0) {
        ret = load_with_gdkpixbuf(&chunk,
                                  fstatic,
                                  fuse_palette,
                                  reqcolors,
                                  bgcolor,
                                  loop_control,
                                  fn_load,
                                  context);
    }
#endif  /* HAVE_GDK_PIXBUF2 */
#if HAVE_GD
    if (!ret != 0) {
        ret = load_with_gd(&chunk,
                           fstatic,
                           fuse_palette,
                           reqcolors,
                           bgcolor,
                           loop_control,
                           fn_load,
                           context);
    }
#endif  /* HAVE_GD */
    if (ret != 0) {
        ret = load_with_builtin(&chunk,
                                fstatic,
                                fuse_palette,
                                reqcolors,
                                bgcolor,
                                loop_control,
                                fn_load,
                                context);
    }
    free(chunk.buffer);

    if (ret != 0) {
        goto end;
    }

    ret = 0;

end:
    return ret;
}

/* emacs, -*- Mode: C; tab-width: 4; indent-tabs-mode: nil -*- */
/* vim: set expandtab ts=4 : */
/* EOF */
