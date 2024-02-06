#include "pnp-server-app.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define Equals(A, B, Size) !memcmp(A, B, Size)
#define QUERY_KEY "number"
#define INT_MAX_DIGITS 11

#define RET_NEGATIVE "{\"err\": \"negative\", \"ret\": 0}"
#define RET_TOO_BIG "{\"err\": \"too-big\", \"ret\": 0}"
#define RET_NOT_PRIME "{\"err\": \"not-prime\", \"ret\": "
#define RET_PRIME "{\"err\": \"prime\", \"ret\": "
#define RET_SIZE 50

struct kv_pair
{
    char* Key;
    char* Value;
    unsigned short KeySize;
    unsigned short ValueSize;
};

static struct kv_pair
GetNextKVPair(char* Query, unsigned short QuerySize)
{
    struct kv_pair Result = { Query, 0, 0, 0 };
    for (int Idx = 0; Idx < QuerySize; Idx++)
    {
        if (Query[Idx] == '=')
        {
            Result.KeySize = Idx; 
            Result.Value = Query + Idx + 1;
            for (int Idx2 = 0; Idx2 < QuerySize - Idx - 1; Idx2++)
            {
                if (Result.Value[Idx2] == '&')
                {
                    Result.ValueSize = Idx2;
                    return Result;
                }
            }
            
            Result.ValueSize = QuerySize - Idx - 1;
            return Result;
        }
    }
    return Result;
}

static int
GetClosestPrime(int Number, app_arena* Arena)
{
    if (Number <= 1) return 2;
    
    int* Ptr = (int*)Arena->Ptr;
    int PrimeCount = Arena->WriteCur / 4;
    for (int Idx = PrimeCount / 2, Bottom = 0, Top = PrimeCount;;)
    {
        int Prime = Ptr[Idx];
        
        if (Number == Prime) return Number;
        else if (Top - Bottom == 1)
        {
            int NextPrime = Ptr[Top];
            int PrevPrime = Ptr[Bottom];
            
            int Closest = (NextPrime - Number <= Number - PrevPrime) ? NextPrime : PrevPrime;
            return Closest;
        }
        else if (Number > Prime)
        {
            Bottom = Idx;
            Idx += (Top - Bottom) / 2;
        }
        else
        {
            Top = Idx;
            Idx -= (Top - Bottom) / 2;
        }
    }
}

void
AppInit(void* Param)
{
    app_arena* Arena = (app_arena*)Param;
    
    FILE* File = fopen("../example/prime/primes", "rb");
    if (File)
    {
        fseek(File, 0, SEEK_END);
        long FileSize = ftell(File);
        fseek(File, 0, SEEK_SET);
        
        if (FileSize < Arena->Size)
        {
            size_t BytesWritten = fread(Arena->Ptr, 1, FileSize, File);
            Arena->WriteCur = BytesWritten;
        }
    }
}

void
ModuleMain(http* Http)
{
	if (Http->Verb == Verb_Get
        && Http->QuerySize > 0)
    {
        struct kv_pair KV = GetNextKVPair(Http->Query, Http->QuerySize);
        if (KV.KeySize == strlen(QUERY_KEY)
            && Equals(QUERY_KEY, KV.Key, KV.KeySize)
            && KV.ValueSize < INT_MAX_DIGITS)
        {
            char* Payload = (char*)AllocPayload(Http, RET_SIZE);
            
            char NumberBuf[INT_MAX_DIGITS] = {0};
            memcpy(NumberBuf, KV.Value, KV.ValueSize);
            int Number = atoi(NumberBuf);
            
            if (Number < 0)
            {
                memcpy(Payload, RET_NEGATIVE, sizeof(RET_NEGATIVE));
            }
            else if (Number > 1000000)
            {
                memcpy(Payload, RET_TOO_BIG, sizeof(RET_TOO_BIG));
            }
            else
            {
                int ClosestPrime = GetClosestPrime(Number, Http->Arena);
                if (ClosestPrime == Number)
                {
                    memcpy(Payload, RET_PRIME, sizeof(RET_PRIME));
                    char* Ptr = itoa(ClosestPrime, Payload + sizeof(RET_PRIME)-1, 10);
                    Ptr[strlen(Ptr)] = '}';
                }
                else
                {
                    memcpy(Payload, RET_NOT_PRIME, sizeof(RET_NOT_PRIME));
                    char* Ptr = itoa(ClosestPrime, Payload + sizeof(RET_NOT_PRIME)-1, 10);
                    Ptr[strlen(Ptr)] = '}';
                }
            }
            
            Http->PayloadSize = strlen(Payload);
            Http->PayloadType = "application/json";
            Http->ReturnCode = 200;
        }
        else
        {
            Http->ReturnCode = 400;
        }
    }
    else
    {
        Http->ReturnCode = 405;
    }
}