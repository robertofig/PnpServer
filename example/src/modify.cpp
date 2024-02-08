#define STB_IMAGE_IMPLEMENTATION
#define STB_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "pnp-server-app.h"

#define MAX_FILE_SIZE (10 * 1024 * 1024) // 10 Megabytes

static unsigned char
Min(float A, float B)
{
    return (unsigned char)((A < B) ? A : B);
}

void WriteFunc(void* Context, void* In, int InSize)
{
    app_arena* Out = (app_arena*)Context;
    if (memcpy(Out->Ptr + Out->WriteCur, In, InSize))
    {
        Out->WriteCur += InSize;
    }
}

void ToSepia(unsigned char* In, unsigned char* Out, int Idx)
{
    // Weights taken from: https://www.techrepublic.com/article/how-do-i-convert-images-to-grayscale-and-sepia-tone-using-c/
    Out[Idx] = Min(0.393 * In[Idx] + 0.769 * In[Idx+1] + 0.189 * In[Idx+2], 255.0);
    Out[Idx+1] = Min(0.349 * In[Idx] + 0.686 * In[Idx+1] + 0.168 * In[Idx+2], 255.0);
    Out[Idx+2] = Min(0.272 * In[Idx] + 0.534 * In[Idx+1] + 0.131 * In[Idx+2], 255.0);
}

void ToGreyscale(unsigned char* In, unsigned char* Out, int Idx)
{
    unsigned char Colour = (unsigned char)(((short)In[Idx] + (short)In[Idx+1] + (short)In[Idx+2]) / 3);
    Out[Idx] = Out[Idx+1] = Out[Idx+2] = Colour;
}

typedef void (*modify)(unsigned char*, unsigned char*, int);

void ModifyImage(unsigned char* In, int InSize, unsigned char* Out, int Channels, modify Func)
{
    if (Channels == 3)
    {
        for (int Idx = 0; Idx < InSize; Idx += Channels)
        {
            Func(In, Out, Idx);
        }
    }
    else
    {
        for (int Idx = 0; Idx < InSize; Idx += Channels)
        {
            Func(In, Out, Idx);
            Out[Idx+3] = In[Idx+3];
        }
    }
}

#define EXT_MAX_SIZE 10

bool IsAllowedImageExt(http_buf Filename)
{
    bool Result = true;
    
    int DotIdx = Filename.Size-1;
    while (DotIdx >= 0)
    {
        if (Filename.Ptr[DotIdx] == '.') break;
        DotIdx--;
    }
    
    if (DotIdx > 0 && (Filename.Size - DotIdx) < EXT_MAX_SIZE)
    {
        char Ext[EXT_MAX_SIZE] = {0};
        memcpy(Ext, Filename.Ptr + DotIdx, Filename.Size - DotIdx);
        
        Result = (!memcmp(Ext, ".jpg", 4)
                  || !memcmp(Ext, ".JPG", 4)
                  || !memcmp(Ext, ".jpeg", 5)
                  || !memcmp(Ext, ".JPEG", 5)
                  || !memcmp(Ext, ".png", 4)
                  || !memcmp(Ext, ".PNG", 4)
                  || !memcmp(Ext, ".bmp", 4)
                  || !memcmp(Ext, ".BMP", 4));
    }
    
    return Result;
}

extern "C" void
ModuleMain(http* Http)
{
	if (Http->Verb == Verb_Post)
    {
        if (RecvFullRequestBody(Http, MAX_FILE_SIZE))
        {
            http_form Form = ParseFormData(Http);
            http_form_field ModifyInfo = GetFormFieldByName(Form, "modify");
            http_form_field ImageInfo = GetFormFieldByName(Form, "image");
            if (ModifyInfo.Data.Size == 1
                && (ModifyInfo.Data.Ptr[0] == 's' || ModifyInfo.Data.Ptr[0] == 'g')
                && ImageInfo.Data.Size > 0)
            {
                if (!IsAllowedImageExt(ImageInfo.Filename))
                {
                    Http->ReturnCode = 406;
                }
                else
                {
                    int Width, Height, Channels;
                    stbi_uc* InImage = stbi_load_from_memory((stbi_uc*)ImageInfo.Data.Ptr, ImageInfo.Data.Size, &Width, &Height, &Channels, 0);
                    int ImageSize = Width * Height * Channels;
                    void* OutImage = calloc(1, ImageSize);
                    
                    modify Func = (ModifyInfo.Data.Ptr[0] == 's') ? ToSepia : ToGreyscale;
                    ModifyImage((unsigned char*)InImage, ImageSize, (unsigned char*)OutImage, Channels, Func);
                    
                    void* Payload = AllocPayload(Http, ImageInfo.Data.Size);
                    app_arena PayloadArena = { (char*)Payload, 0, ImageInfo.Data.Size };
                    stbi_write_jpg_to_func(WriteFunc, &PayloadArena, Width, Height, Channels, OutImage, 90);
                    
                    Http->PayloadSize = PayloadArena.WriteCur;
                    Http->PayloadType = "image/jpeg";
                    Http->ReturnCode = 200;
                    
                    free(OutImage);
                }
            }
            else
            {
                Http->ReturnCode = 400;
            }
        }
        else
        {
            // RecvFullRequestBody() already sets the return code on failure.
            // Do not set it again here!
        }
    }
    else
    {
        Http->ReturnCode = 405;
    }
}