#include "pnp-server-app.h"

internal http_buf
AppGetHeaderByKey(http* Http, char* Key)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_info* Info = (io_info*)&Conn[1];
    
    string Header = GetHeaderByKey(&Info->Request, Key);
    http_buf Result = { Header.Base, Header.WriteCur };
    return Result;
}

internal http_buf
AppGetHeaderByIdx(http* Http, u32 Idx)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_info* Info = (io_info*)&Conn[1];
    
    string Header = GetHeaderByIdx(&Info->Request, Idx);
    http_buf Result = { Header.Base, Header.WriteCur };
    return Result;
}

internal int
AppRecvFullRequestBody(http* Http, size_t MaxBodySize)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_info* Info = (io_info*)&Conn[1];
    
    if (!RecvFullRequestBody(Conn, &Info->Body, MaxBodySize))
    {
        Http->ReturnCode = Info->Response.StatusCode;
        return 0;
    }
    return 1;
}

internal http_form
AppParseFormData(http* Http)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_info* Info = (io_info*)&Conn[1];
    
    ts_multiform Form = ParseFormData(Info->Body);
    http_form Result = { Http, Form.FieldCount, Form.FirstField };
    return Result;
}

internal http_form_field
_TsFormFieldToHttpFormField(ts_form_field Field)
{
    http_form_field Result = {0};
    if (Field.FieldName)
    {
        Result.Field = { Field.FieldName, Field.FieldNameSize };
        Result.Filename = { Field.Filename, Field.FilenameSize };
        Result.Charset = { Field.Charset, Field.CharsetSize };
        Result.Data = { (char*)Field.Data, Field.DataLen };
    }
    return Result;
}

internal http_form_field
AppGetFormFieldByName(http_form Form, char* TargetName)
{
    ts_multiform _Form = { Form.NumFields, Form.FirstField, 0 };
    ts_form_field Field = GetFormFieldByName(_Form, TargetName);
    return _TsFormFieldToHttpFormField(Field);
}

internal http_form_field
AppGetFormFieldByIdx(http_form Form, size_t TargetIdx)
{
    ts_multiform _Form = { Form.NumFields, Form.FirstField, 0 };
    ts_form_field Field = GetFormFieldByIdx(_Form, TargetIdx);
    return _TsFormFieldToHttpFormField(Field);
}

internal void*
AppAllocPayload(http* Http, usz Size)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_info* Info = (io_info*)&Conn[1];
    
    void* Mem = GetMemory(Size, 0, MEM_READ|MEM_WRITE);
    if (Mem)
    {
        Info->Response.Payload = (char*)Mem;
    }
    return Mem;
}

internal void*
AppAllocCookies(http* Http, usz Size)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_info* Info = (io_info*)&Conn[1];
    
    void* Mem = GetMemory(Size, 0, MEM_READ|MEM_WRITE);
    if (Mem)
    {
        Info->Response.Cookies = (char*)Mem;
    }
    return Mem;
}
