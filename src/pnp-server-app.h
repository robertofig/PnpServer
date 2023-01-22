#ifndef PNP_SERVER_APP_H
//=========================================================================
// pnp-server-app.h
//
// Include this file in the app. It allows the app to get information on
// the request and setup the response. There are three parts to it: the
// "Auxiliary structs", used for reading data and setting up the response;
// the "Main struct", carrying fields with basic data on request and body,
// and response fields that must be set by the app; and "Methods", which
// has functions for accessing data not present in the fields, and calling
// functionalities from the server.
//
// Workflow of the app:
//   1. The [Http] object has basic info on the request header; to access
//      individual headers, use GetHeaderByName() or GetHeaderByIdx().
//   2. If the [.Body] member of [Http] is not NULL, means the request
//      sent an entity body. The server does not guarantee that the entire
//      body has been received prior to entering the app. This is so the
//      app can choose to accept or reject receiving the body. If accepts,
//      the RecvFullRequestBody() method MUST be called.
//   3. (optional) If the body is of type "multipart/form-data", it can
//      be parsed with ParseFormData(), and then using the methods
//      GetFormFieldByName() and GetFormFieldByIdx() to access its fields.
//   4. Do the app processing.
//   5. (optional) If  the app is returning data, use AllocPayload() to
//      allocate memory for it, which the app can write into. Then, set
//      [.PayloadType] member in [Http] to a C-string with the MIME type
//      of the returning data, and [.PayloadSize] with the size of it.
//   6. (optional) If the app is creating cookies, use AllocCookies() to
//      allocate memory for them, which the app can write into. Then, set
//      [.CookiesSize] member in [Http] to the size of it. Cookies MUST
//      end in a blank /r/n line.
//   7. Set the [.ReturnCode] member in [Http] with the appropriate HTTP
//      status code - 200 if successful, one of the other numbers if not.
//=========================================================================
#define PNP_SERVER_APP_H


//==============================
// Auxiliary structs
//==============================

typedef struct app_arena
{
    char* Ptr;       // Pointer to beginning of App arena.
    size_t WriteCur; // Amount written.
    size_t Size;     // Total size of arena.
} app_arena;

typedef struct http_buf
{
    char* Ptr;   // Pointer to a string.
    size_t Size; // Number of chars in [Ptr].
} http_buf;

typedef enum http_verb
{
    Verb_Get    = 1,
    Verb_Post   = 3,
    Verb_Put    = 4,
    Verb_Delete = 5
} http_verb;

typedef struct http_form
{
    void* Http;       // Internal.
    size_t NumFields; // Number of fields in form.
    void* FirstField; // Pointer to first field (do not read from this).
} http_form;

typedef struct http_form_field
{
    http_buf Field;    // Name of field.
    http_buf Filename; // Name of file (empty if field is not a file).
    http_buf Charset;  // Charset of data (empty if not specified).
    http_buf Data;     // Data of the dield.
} http_form_field;

typedef struct cookie_time
{
    unsigned short Year;
    unsigned char  Month;
    unsigned char  Day;
    unsigned char  Hour;
    unsigned char  Minute;
    unsigned char  Second;
} cookie_time;

typedef enum cookie_attr
{
    Cookie_ExpDate        = 0x1,
    Cookie_MaxAge         = 0x2,
    Cookie_Domain         = 0x4,
    Cookie_Path           = 0x8,
    Cookie_Secure         = 0x10,
    Cookie_HttpOnly       = 0x20,
    Cookie_SameSiteStrict = 0x40,  // Only one of the SameSite flags must be used.
    Cookie_SameSiteLax    = 0x80,  // ^
    Cookie_SameSiteNone   = 0x100  // ^
} cookie_attr;

typedef struct cookie
{
    char* Key;                // Name of key (must be nul-terminated).
    char* Value;              // Pointer to Value data.
    unsigned short ValueSize; // Size of Value data.
    
    cookie_attr AttrFlags; // Set flags in cookie.
    cookie_time ExpDate;   // Expiration date of cookie.                      [Cookie_ExpDate]
    unsigned int MaxAge;   // Amount of seconds before cookies expires.       [Cookie_MaxAge]
    char* Domain;          // Domain for the cookie (must be nul-terminated). [Cookie_Domain]
    char* Path;            // Path for the cookie (must be nul-terminated).   [Cookie_Path]
} cookie;

//==============================
// Main struct
//==============================

typedef struct http
{
    void* Object;     // Internal (do not use).
    app_arena* Arena; // App global memory.
    
    // Request header.
    http_verb Verb;
    char* Path;               // String with path part of URI.
    char* Query;              // String with query part of URI.
    unsigned short PathSize;  // Number of bytes in [Path].
    unsigned short QuerySize; // Number of bytes in [Query].
    unsigned int HeaderCount; // Number of headers in request.
    
    // Request body.
    unsigned char* Body;   // Pointer to request body (NULL if request is header-only).
    size_t BodySize;       // Number of bytes of body (as specified in Content-Size header).
    char* ContentType;     // String with the MIME type of body.
    short ContentTypeSize; // Number of bytes in [ContentType].
    
    // Response.
    char* PayloadType;              // C-string with the MIME type of the payload (if there's any).
    unsigned long long PayloadSize; // Size of the payload data.
    unsigned short CookiesSize;     // Size of cookies data.
    unsigned short ReturnCode;      // Status code of the response.
    
    // Function pointers (ignore, use methods below).
    http_buf (*GetHeaderByKey)(struct http*, char*);
    http_buf (*GetHeaderByIdx)(struct http*, unsigned int);
    int (*RecvFullRequestBody)(struct http*, size_t);
    http_form (*ParseFormData)(struct http*);
    http_form_field (*GetFormFieldByName)(http_form, char*);
    http_form_field (*GetFormFieldByIdx)(http_form, size_t);
    void* (*AllocPayload)(struct http*, size_t);
    void* (*AllocCookies)(struct http*, size_t);
} http;

//==============================
// Methods
//==============================

http_buf GetHeaderByKey(http* Http, char* Key)
{ return Http->GetHeaderByKey(Http, Key); }

/* Gets the value of the [Key] header. [Key] must be a nul-terminated string.
  |--- Return: http_buf with Value, or empty object if [Key] not found. */

http_buf GetHeaderByIdx(http* Http, unsigned int Idx)
{ return Http->GetHeaderByIdx(Http, Idx); }

/* Gets the value of the header at index [Idx]. [Idx] goes from 0..HeaderCount-1.
  |--- Return: http_buf with Value, or empty object if [Idx] overflows. */

int RecvFullRequestBody(http* Http, size_t MaxBodySize)
{ return Http->RecvFullRequestBody(Http, MaxBodySize); }

/* Calls recv on the socket until the entirety of the Body is received. If the incoming
|  data is larger than [MaxBodySize], nothing is read and flag is set to forcebly
|  shut down the connection. If the connection stalls, function exits on failure.
 |--- Return: true if entire body was received, false otherwise. */

http_form ParseFormData(http* Http)
{ return Http->ParseFormData(Http); }

/* Parses the request body if it's of type "multipart/form-data". Body must be
 |  complete before calling this function.
|--- Return: object with form info, or empty object if parsing failed. */

http_form_field GetFormFieldByName(http_form Form, char* FieldName)
{ return ((http*)(Form.Http))->GetFormFieldByName(Form, FieldName); }

/* Given a parsed [Form], gets the field data of [FieldName] field. [FieldName]
 |  must be a nul-terminated C-string.
|--- Return: object with field data, or empty object if [FieldName] not found. */

http_form_field GetFormFieldByIdx(http_form Form, size_t FieldIdx)
{ return ((http*)(Form.Http))->GetFormFieldByIdx(Form, FieldIdx); }

/* Given a parsed [Form], gets the field data at index [FieldIdx]. [FieldIdx] goes
 |  from 0..NumFields-1 in the http_form object.
|--- Return: object with field data, or empty object if [FieldIdx] overflows. */

void* AllocPayload(http* Http, size_t Size)
{ return Http->AllocPayload(Http, Size); }

/* Allocates a block of memory of [Size] bytes, and sets it internally as the
 |  buffer to be sent as response payload. Must not be freed by app, nor attempted
|  to write past [Size].
|--- Return: pointer of memory block, or NULL if allocation failed. */

void* AllocCookies(http* Http, size_t Size)
{ return Http->AllocCookies(Http, Size); }

/* Allocates a block of memory of [Size] bytes, and sets it internally as the
 |  buffer to be sent as response cookies. Must not be freed by app, nor attempted
|  to write past [Size].
|--- Return: pointer of memory block, or NULL if allocation failed. */


#endif //PNP_SERVER_APP_H
