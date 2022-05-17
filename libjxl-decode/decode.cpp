// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// This C++ example decodes a JPEG XL image in one shot (all input bytes
// available at once). The example outputs the pixels and color information to a
// floating point image and an ICC profile on disk.
#pragma warning(disable:4996)
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <io.h>
#include <windows.h>
#include <iostream>
#include <vector>

#include "jxl/decode.h"
#include "jxl/decode_cxx.h"
#include "jxl/resizable_parallel_runner.h"
#include "jxl/resizable_parallel_runner_cxx.h"

clock_t start, end;

bool DecodeJpegXlOneShot(const uint8_t* jxl, size_t size, std::vector<uint8_t>* pixels, size_t* xsize, size_t* ysize) {
    // Multi-threaded parallel runner.
    auto runner = JxlResizableParallelRunnerMake(nullptr);

    auto dec = JxlDecoderMake(nullptr);
    if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING |  JXL_DEC_FULL_IMAGE)) 
    {
        fprintf(stderr, "JxlDecoderSubscribeEvents failed\n");
        return false;
    }

    if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get())) 
    {
        fprintf(stderr, "JxlDecoderSetParallelRunner failed\n");
        return false;
    }

    start = clock();

    JxlBasicInfo info;
    JxlPixelFormat format = { 3, JXL_TYPE_UINT8, JXL_LITTLE_ENDIAN, 0 };
    std::vector<uint8_t> icc_profile;

    JxlDecoderSetInput(dec.get(), jxl, size);

    for (;;) 
    {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

        if (status == JXL_DEC_ERROR) 
        {
            fprintf(stderr, "Decoder error\n");
            return false;
        }
        else if (status == JXL_DEC_NEED_MORE_INPUT) 
        {
            fprintf(stderr, "Error, already provided all input\n");
            return false;
        }
        else if (status == JXL_DEC_BASIC_INFO) 
        {
            if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info)) 
            {
                fprintf(stderr, "JxlDecoderGetBasicInfo failed\n");
                return false;
            }
            *xsize = info.xsize;
            *ysize = info.ysize;
            JxlResizableParallelRunnerSetThreads(runner.get(), JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
        }
        else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) 
        {
            size_t buffer_size;
            if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size)) 
            {
                fprintf(stderr, "JxlDecoderImageOutBufferSize failed\n");
                return false;
            }
            std::cout << "buffer size = " << buffer_size << std::endl;
            if (buffer_size != *xsize * *ysize * 3) 
            {
                fprintf(stderr, "Invalid out buffer size %zu %zu\n", buffer_size, *xsize * *ysize * 3);
                return false;
            }
            pixels->resize(*xsize * *ysize * 3);
            void* pixels_buffer = (void*)pixels->data();
            size_t pixels_buffer_size = pixels->size() * sizeof(uint8_t);
            if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec.get(), &format, pixels_buffer, pixels_buffer_size)) 
            {
                fprintf(stderr, "JxlDecoderSetImageOutBuffer failed\n");
                return false;
            }
        }
        else if (status == JXL_DEC_COLOR_ENCODING)
        {
            // Get the ICC color profile of the pixel data
            size_t icc_size;
            if (JXL_DEC_SUCCESS != JxlDecoderGetICCProfileSize(dec.get(), &format, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size)) 
            {
                fprintf(stderr, "JxlDecoderGetICCProfileSize failed\n");
                return false;
            }
            icc_profile.resize(icc_size);
            if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(dec.get(), &format, JXL_COLOR_PROFILE_TARGET_DATA, icc_profile.data(), icc_profile.size())) 
            {
                fprintf(stderr, "JxlDecoderGetColorAsICCProfile failed\n");
                return false;
            }
        }
        else if (status == JXL_DEC_FULL_IMAGE){}
        else if (status == JXL_DEC_SUCCESS) 
        {
            end = clock();

            std::cout << "time: " << end - start << "ms" << std::endl;
            // All decoding successfully finished.
            // It's not required to call JxlDecoderReleaseInput(dec.get()) here since
            // the decoder will be destroyed.
            return true;
        }
        else 
        {
            fprintf(stderr, "Unknown decoder status\n");
            return false;
        }
    }

}

//3-channel BMP
bool WriteBMP(const char* filename, std::vector<uint8_t>* pixels, size_t xsize, size_t ysize) 
{
    FILE* fp = fopen(filename, "wb");
    if (!fp) 
    {
        fprintf(stderr, "Could not open %s for writing", filename);
        return false;
    }

    BOOL padding = (xsize % 4) == 0 ? TRUE : FALSE;
    int skip;

    if (FALSE == padding)
    {
        skip = 4 - ((xsize * 24) >> 3) & 3;
    }
    else
    {
        skip = 0;
    }

    BITMAPFILEHEADER bmpheader;
    BITMAPINFOHEADER bmpinfo;

    bmpheader.bfType = 0x4d42;
    bmpheader.bfReserved1 = 0;
    bmpheader.bfReserved2 = 0;
    bmpheader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmpheader.bfSize = bmpheader.bfOffBits + xsize * ysize * 3 + skip * ysize;

    bmpinfo.biSize = sizeof(BITMAPINFOHEADER);
    bmpinfo.biWidth = xsize;
    bmpinfo.biHeight = ysize;
    bmpinfo.biPlanes = 1;
    bmpinfo.biBitCount = 0x18;
    bmpinfo.biCompression = 0;//BI_RGB;
    bmpinfo.biSizeImage = xsize * ysize;
    bmpinfo.biXPelsPerMeter = 100;
    bmpinfo.biYPelsPerMeter = 100;
    bmpinfo.biClrUsed = 0;
    bmpinfo.biClrImportant = 0;

    fwrite(&bmpheader.bfType, sizeof(bmpheader.bfType), 1, fp);
    fwrite(&bmpheader.bfSize, sizeof(bmpheader.bfSize), 1, fp);
    fwrite(&bmpheader.bfReserved1, sizeof(bmpheader.bfReserved1), 1, fp);
    fwrite(&bmpheader.bfReserved2, sizeof(bmpheader.bfReserved2), 1, fp);
    fwrite(&bmpheader.bfOffBits, sizeof(bmpheader.bfOffBits), 1, fp);

    fwrite(&bmpinfo.biSize, sizeof(bmpinfo.biSize), 1, fp);
    fwrite(&bmpinfo.biWidth, sizeof(bmpinfo.biWidth), 1, fp);
    fwrite(&bmpinfo.biHeight, sizeof(bmpinfo.biHeight), 1, fp);
    fwrite(&bmpinfo.biPlanes, sizeof(bmpinfo.biPlanes), 1, fp);
    fwrite(&bmpinfo.biBitCount, sizeof(bmpinfo.biBitCount), 1, fp);
    fwrite(&bmpinfo.biCompression, sizeof(bmpinfo.biCompression), 1, fp);
    fwrite(&bmpinfo.biSizeImage, sizeof(bmpinfo.biSizeImage), 1, fp);
    fwrite(&bmpinfo.biXPelsPerMeter, sizeof(bmpinfo.biXPelsPerMeter), 1, fp);
    fwrite(&bmpinfo.biYPelsPerMeter, sizeof(bmpinfo.biYPelsPerMeter), 1, fp);
    fwrite(&bmpinfo.biClrUsed, sizeof(bmpinfo.biClrUsed), 1, fp);
    fwrite(&bmpinfo.biClrImportant, sizeof(bmpinfo.biClrImportant), 1, fp);

    std::vector<uint8_t> data;
    data.resize(bmpheader.bfSize - bmpheader.bfOffBits);
    data.assign(data.size(), 0);//置0

    for (int y = 0; y < (int)ysize; y++) {
        for (int x = 0; x < (int)xsize; x++) {
            for (int c = 2; c >= 0; c--) {
                memcpy(data.data() + ((ysize - y - 1) * xsize + x) * 3 + (2 - c) + (ysize - y - 1) * skip,
                    //(2 - c)换RGB->BGR,(ysize - y - 1)转图像方向,(ysize - y - 1) * skip跳过0的填充
                    pixels->data() + (y * xsize + x) * 3 + c,                   
                    sizeof(uint8_t));
            }
        }
    }

    //for (int y = 0; y < (int)ysize; y++) {
    //    for (int x = 0; x < (int)xsize; x++) {
    //        for (int c = 0; c < 3; c++) {
    //            memcpy(pixels->data() + (y * xsize + x) * 3 + c, data.data() + (y * xsize + x) * 3 + c, sizeof(uint8_t));
    //        }
    //    }
    //}

    fwrite(data.data(), data.size(), 1, fp);

    //for (int y = ysize - 1; y >= 0; y--) {
    //    for (size_t x = 0; x < xsize; x++) {
    //        for (size_t c = 0; c < 3; c++) {
    //            const float* f = &pixels[(y * xsize + x) * 4 + c];
    //            fwrite(f, 4, 1, file);
    //        }
    //    }
    //}

    if (fclose(fp) != 0) {
        return false;
    }
    return true;
}

bool LoadFile(const char* filename, std::vector<uint8_t>* out) 
{
    FILE* file = fopen(filename, "rb");
    if (!file) 
    {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) 
    {
        fclose(file);
        return false;
    }

    long size = ftell(file);
    // Avoid invalid file or directory.
    if (size >= LONG_MAX || size < 0) 
    {
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) 
    {
        fclose(file);
        return false;
    }

    out->resize(size);
    size_t readsize = fread(out->data(), 1, size, file);
    if (fclose(file) != 0) 
    {
        return false;
    }

    return readsize == static_cast<size_t>(size);
}

int main(int argc, char* argv[]) 
{
    char inImagePath[40] = "C:\\image\\jxl\\people\\*.jxl";
    char inFile[40] = "C:\\image\\jxl\\people\\";
    char outImagePath[40] = "C:\\image\\output\\jxl\\people\\";

    intptr_t handle;
    struct _finddata_t fileInfo;

    int i = 1;
    char num[10] = { 0 };
    itoa(i, num, 10);

    std::vector<uint8_t> pixels;

    handle = _findfirst(inImagePath, &fileInfo);
    char outFileName[40];
    strcpy(outFileName, outImagePath);
    strcat(outFileName, num);
    strcat(outFileName, ".bmp");
    char inFilename[40] = { 0 };
    strcpy(inFilename, inFile);
    strcat(inFilename, fileInfo.name);
    //std::cout << fileInfo.name << " bmpSize:" << fileInfo.size << std::endl;

    if (LoadFile(inFilename, &pixels))
    {
        std::vector<uint8_t> decode;
        size_t xsize = 0, ysize = 0;
        if (!DecodeJpegXlOneShot(pixels.data(), pixels.size(), &decode, &xsize, &ysize))
        {
            fprintf(stderr, "Error while decoding the jxl file\n");
            return 1;
        }
        if (!WriteBMP(outFileName, &decode, xsize, ysize)) {
            fprintf(stderr, "Error while writing the PFM image file\n");
            return 1;
        }
        printf("Successfully wrote %s\n", outFileName);
    }
    else
    {
        std::cout << "error" << std::endl;
        return 0;
    }

    pixels.clear();

    if (-1 == handle)
    {
        printf("[error]");
        return 0;
    }
    while (0 == _findnext(handle, &fileInfo))
    {
        //std::cout << fileInfo.name << " bmpSize:" << fileInfo.size << std::endl;
        memset(outFileName, 0, 20);
        memset(inFilename, 0, 20);
        char numTemp[10] = { 0 };
        i++;
        itoa(i, numTemp, 10);
        strcpy(outFileName, outImagePath);
        strcat(outFileName, numTemp);
        strcat(outFileName, ".bmp");
        strcpy(inFilename, inFile);
        strcat(inFilename, fileInfo.name);
        if (LoadFile(inFilename, &pixels))
        {
            std::vector<uint8_t> decode;
            size_t xsize = 0, ysize = 0;
            if (!DecodeJpegXlOneShot(pixels.data(), pixels.size(), &decode, &xsize, &ysize))
            {
                fprintf(stderr, "Error while decoding the jxl file\n");
                return 1;
            }
            if (!WriteBMP(outFileName, &decode, xsize, ysize)) {
                fprintf(stderr, "Error while writing the PFM image file\n");
                return 1;
            }
            printf("Successfully wrote %s\n", outFileName);
        }
        else
        {
            std::cout << "error" << std::endl;
            return 0;
        }

        pixels.clear();
    }

    _findclose(handle);

    return 0;
}

