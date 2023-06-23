/**
* This is a lightweight wrapper around libmupdf (hey Robin!) to make it easier to just get a page, or 2 pages together, into an existing framebuffer.
* See https://github.com/ArtifexSoftware/mupdf and https://www.mupdf.com/
* As such, this is under APL3: https://www.gnu.org/licenses/agpl-3.0.en.html
*/

#include <Windows.h>
#include <stdbool.h>

#include "mupdf/fitz.h"
#pragma comment( lib, "libmupdf" )

typedef struct Pdf
{
	fz_context  *context;
	fz_document *document;
    int          pageCount;
} Pdf;

// yoinked from `utils.c` so I didn't have to include another library
fz_pixmap *
_fz_new_pixmap_from_page_with_separations(fz_context *ctx, fz_page *page, fz_matrix ctm, fz_colorspace *cs, fz_separations *seps, int alpha)
{
	fz_rect rect;
	fz_irect bbox;
	fz_pixmap *pix;
	fz_device *dev = NULL;
    const fz_matrix fz_identity = {1, 0, 0, 1, 0, 0};

	fz_var(dev);

	rect = fz_bound_page(ctx, page);
	rect = fz_transform_rect(rect, ctm);
	bbox = fz_round_rect(rect);

	pix = fz_new_pixmap_with_bbox(ctx, cs, bbox, seps, alpha);

	fz_try(ctx)
	{
		if (alpha)
			fz_clear_pixmap(ctx, pix);
		else
			fz_clear_pixmap_with_value(ctx, pix, 0xFF);

		dev = fz_new_draw_device(ctx, ctm, pix);
		fz_run_page(ctx, page, dev, fz_identity, NULL);
		fz_close_device(ctx, dev);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_drop_pixmap(ctx, pix);
		fz_rethrow(ctx);
	}

	return pix;
}
fz_pixmap *
_fz_new_pixmap_from_page_number_with_separations(fz_context *ctx, fz_document *doc, int number, fz_matrix ctm, fz_colorspace *cs, fz_separations *seps, int alpha)
{
	fz_page *page;
	fz_pixmap *pix = NULL;

	page = fz_load_page(ctx, doc, number);
	fz_try(ctx)
		pix = _fz_new_pixmap_from_page_with_separations(ctx, page, ctm, cs, seps, alpha);
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		fz_rethrow(ctx);
	return pix;
}


/**
* This gets a page from the PDF at 72dpi
* 
* Usage:
*   call the function with `outBuffer` set to `NULL`
*   `width` and `height` will be set to the required width and height of the document
*   allocate a buffer of size `width * height * 3` and call the function again to have it populated with the contents of the page
*/
__declspec(dllexport) int __cdecl Pdf_getPageRGB(Pdf *pdf, int pageNumber, int *width, int *height, unsigned char *outBuffer)
{
    fz_pixmap *pixmap     = NULL;
    fz_matrix  viewMatrix = fz_scale(1.0, 1.0);

    if (!pdf)
        return false;

    fz_try(pdf->context)
        pixmap = _fz_new_pixmap_from_page_number_with_separations(pdf->context, pdf->document, pageNumber, viewMatrix, fz_device_rgb(pdf->context), NULL, 0);
    fz_catch(pdf->context)
        return false;

    if (!outBuffer)
    {
        if (width)  *width  = pixmap->w;
        if (height) *height = pixmap->h;
    }
    else
    {
        int y;
        // could this just be one single memcpy? can `->stride` ever NOT be just `->w`?
        // I'm not going to find out, so just be safe
        for (y = 0; y < pixmap->h; y++)
        {
            unsigned char *srcLine = &pixmap->samples[y * pixmap->stride];
            unsigned char *dstLine = &outBuffer[y * pixmap->w];
            memcpy(dstLine, srcLine, pixmap->w);
        }
    }

    if (pixmap) fz_drop_pixmap(pdf->context, pixmap);

    return true;
}


static int getPagePixmap(Pdf *pdf, int pageNumber, int availableWidth, int availableHeight, int *resultingWidth, int *resultingHeight, fz_pixmap **outPixmap)
{
    bool       result = true;
    fz_pixmap *pixmap = NULL;
    fz_page   *page   = NULL;
    fz_rect    bbox;

    if (!pdf || !outPixmap)
        return false;

    *outPixmap = NULL;
    page = fz_load_page(pdf->context, pdf->document, pageNumber);
    bbox = fz_bound_page(pdf->context, page);
    {
        fz_matrix viewMatrix;
        float     bboxWidth   = bbox.x1 - bbox.x0;
        float     bboxHeight  = bbox.y1 - bbox.y0;
        float     aspectRatio = bboxWidth / bboxHeight;
        float     zoomFactor  = availableWidth / bboxWidth;

        if (bboxHeight * zoomFactor > availableHeight)
            zoomFactor = availableHeight / bboxHeight;

        viewMatrix = fz_scale(zoomFactor, zoomFactor);

	    fz_try(pdf->context)
		    pixmap = _fz_new_pixmap_from_page_with_separations(pdf->context, page, viewMatrix, fz_device_rgb(pdf->context), NULL, 0);
	    fz_catch(pdf->context)
		    result = false;
    }

    if (result)
    {
        if (resultingWidth)  *resultingWidth  = pixmap->w;
        if (resultingHeight) *resultingHeight = pixmap->h;
        *outPixmap = pixmap;
    }

    if (page) fz_drop_page(pdf->context, page);
    return result;
}


/**
* This will scale a page to fit the available space specified and return it as RGB.
* It keeps the original aspect ratio of the page, so the resulting buffer might end up with space to the right, or bottom.
* If `resultingWidth` and `resultingHeight` are non `NULL`, then they will be set to the actual dimensions of the contents within the buffer.
* If you like, you can then e.g., zero out the unused contents
* 
* NOTE! do NOT use the same width and height variables for specifying the available space and the resulting space, things will go wrong.
*/
__declspec(dllexport) int __cdecl Pdf_getPageFittedRGB(Pdf *pdf, int pageNumber, int availableWidth, int availableHeight, int *resultingWidth, int *resultingHeight, unsigned char *outBuffer)
{
    fz_pixmap *pixmap = NULL;

    if (!pdf || !outBuffer)
        return false;

    if (!getPagePixmap(pdf, pageNumber, availableWidth, availableHeight, resultingWidth, resultingHeight, &pixmap))
        return false;

    // write the RGB pixmap to the BGRA outBuffer in the slowest way possible
    {
        int y;
        int maxWidth = min(availableWidth, pixmap->w);
        int maxHeight = min(availableHeight, pixmap->h);

        // could this just be one single memcpy? can `->stride` ever NOT be just `->w`?
        // I'm not going to find out, so just be safe
        for (y = 0; y < maxHeight; y++)
        {
            unsigned char *srcLine = &pixmap->samples[y * pixmap->stride];
            unsigned char* dstLine = &outBuffer[y * pixmap->w * 3];
            memcpy(dstLine, srcLine, maxWidth * 3);
        }
    }

    if (pixmap) fz_drop_pixmap(pdf->context, pixmap);

    return true;
}


/**
* This will scale a page to fit the available space specified and return it as BGRA (where alpha will always be 255).
* It keeps the original aspect ratio of the page, so the resulting buffer might end up with space to the right, or bottom.
* If `resultingWidth` and `resultingHeight` are non `NULL`, then they will be set to the actual dimensions of the contents within the buffer.
* If you like, you can then e.g., zero out the unused contents
* 
* NOTE! do NOT use the same width and height variables for specifying the available space and the resulting space, things will go wrong.
*/
__declspec(dllexport) int __cdecl Pdf_getPageFittedBGRA(Pdf *pdf, int pageNumber, int availableWidth, int availableHeight, int *resultingWidth, int *resultingHeight, unsigned char *outBuffer)
{
    fz_pixmap *pixmap = NULL;

    if (!pdf || !outBuffer)
        return false;

    if (!getPagePixmap(pdf, pageNumber, availableWidth, availableHeight, resultingWidth, resultingHeight, &pixmap))
        return false;

    // write the RGB pixmap to the BGRA outBuffer in the slowest way possible
    {
        int x, y;
        int maxWidth = min(availableWidth, pixmap->w);
        int maxHeight = min(availableHeight, pixmap->h);
        for (y = 0; y < maxHeight; y++)
        {
            unsigned char *src = &pixmap->samples[y * pixmap->stride];
            unsigned char *dst = &outBuffer[y * availableWidth * 4];
            for (x = 0; x < maxWidth; x++)
            {
                *(dst + 2) = *(src + 0);
                *(dst + 1) = *(src + 1);
                *(dst + 0) = *(src + 2);
                *(dst + 3) = 255;

                src += 3;
                dst += 4;
            }
        }
    }

    if (pixmap) fz_drop_pixmap(pdf->context, pixmap);

    return true;
}


/**
* This will scale 2 pages to fit the available space specified and return it as BGRA (where alpha will always be 255).
* It keeps the original aspect ratio of the pages, so the resulting buffer might end up with space to the right, or bottom.
* If `resultingWidth` and `resultingHeight` are non `NULL`, then they will be set to the actual dimensions of the contents within the buffer.
* If you like, you can then e.g., zero out the unused contents
* 
* It rather naively first allocates half the width to the first page, which works for most books, but sometimes comic books have
* two pages stuck together as a single page already, meaning that the left page will have a lot of wasted space below it, and look out of place.
* Feel free to rework and get the bounding boxes of both pages and do something more sensible in that case.
* 
* NOTE! do NOT use the same width and height variables for specifying the available space and the resulting space, things will go wrong.
*/
__declspec(dllexport) int __cdecl Pdf_get2PagesFittedBGRA(Pdf *pdf, int startPageNumber, int availableWidth, int availableHeight, int *resultingWidth, int *resultingHeight, unsigned char *outBuffer)
{
    int index;
    int assignedWidth = availableWidth / 2;
    int leftOffset    = 0;
    int bottomOffset  = 0;

    if (!pdf || !outBuffer)
        return false;

    for (index = 0; index < 2; index++)
    {
        fz_matrix viewMatrix; // don't affect the context one
        fz_page  *page         = fz_load_page(pdf->context, pdf->document, startPageNumber + index);
        fz_rect   bbox         = fz_bound_page(pdf->context, page);
        float     bboxWidth    = bbox.x1 - bbox.x0;
        float     bboxHeight   = bbox.y1 - bbox.y0;
        float     aspectRatio  = bboxWidth / bboxHeight;
        float     zoomFactor   = assignedWidth / bboxWidth;

        if (bboxHeight * zoomFactor > availableHeight)
            zoomFactor = availableHeight / bboxHeight;

        viewMatrix = fz_scale(zoomFactor, zoomFactor);

	    fz_try(pdf->context)
        {
		    fz_pixmap *pagePixmap = _fz_new_pixmap_from_page_with_separations(pdf->context, page, viewMatrix, fz_device_rgb(pdf->context), NULL, 0);
            {
                int x, y;
                int maxWidth = min(assignedWidth, pagePixmap->w);
                int maxHeight = min(availableHeight, pagePixmap->h);
                for (y = 0; y < maxHeight; y++)
                {
                    unsigned char *src = (pagePixmap->samples + y * pagePixmap->w * 3);
                    unsigned char *dst = (outBuffer + y * availableWidth * 4) + (leftOffset * 4);
                    for (x = 0; x < maxWidth; x++)
                    {
                        *(dst + 2) = *(src + 0);
                        *(dst + 1) = *(src + 1);
                        *(dst + 0) = *(src + 2);
                        *(dst + 3) = 255;

                        src += 3;
                        dst += 4;
                    }
                }
            }
            leftOffset += pagePixmap->w;
            assignedWidth = availableWidth - leftOffset;
            if (pagePixmap->h > bottomOffset)
                bottomOffset = pagePixmap->h;
            fz_drop_pixmap(pdf->context, pagePixmap);
        }
	    fz_catch(pdf->context)
        {
            if (page) fz_drop_page(pdf->context, page);
            return false;
        }

        if (page) fz_drop_page(pdf->context, page);
    }

    if (resultingWidth)  *resultingWidth  = leftOffset;
    if (resultingHeight) *resultingHeight = bottomOffset;

    return true;
}

__declspec(dllexport) int __cdecl Pdf_destroy(Pdf *pdf)
{
    if (!pdf)
        return false;

	if (pdf->document) fz_drop_document(pdf->context, pdf->document);
	if (pdf->context)  fz_drop_context(pdf->context);
    free(pdf);
	return false;
}

__declspec(dllexport) int __cdecl Pdf_create(Pdf **newPdf, const char *filePath)
{
    Pdf *pdf = NULL;

    if (!newPdf || !filePath)
        return false;

    *newPdf = NULL;

    pdf = (Pdf*)calloc(sizeof(Pdf), 1);
    if (!pdf)
        goto error;

	// Create a context to hold the exception stack and various caches.
	pdf->context = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!pdf->context)
        goto error;

	// Register document handlers for the default file types we support.
    fz_try(pdf->context)
	    fz_register_document_handlers(pdf->context);
    fz_catch(pdf->context)
        goto error;

	// Open the PDF, XPS or CBZ document.
    fz_try(pdf->context)
    	pdf->document = fz_open_document(pdf->context, filePath);
    fz_catch(pdf->context)
        goto error;

	// Retrieve the number of pages.
    fz_try(pdf->context)
    	pdf->pageCount = fz_count_pages(pdf->context, pdf->document);
    fz_catch(pdf->context)
        goto error;

    *newPdf = pdf;

	return true;

error:
    return Pdf_destroy(pdf);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

