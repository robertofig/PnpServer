#include "pnp-server-app.h"

// Forward declaration.
internal bool RecvFullRequestBody(ts_io*, ts_body*, usz);


internal http_buf
AppGetHeaderByKey(http* Http, char* Key)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_aux* Aux = (io_aux*)&Conn[1];
    
    string Header = GetHeaderByKey(&Aux->Request, Key);
    http_buf Result = { Header.Base, Header.WriteCur };
    return Result;
}

internal http_buf
AppGetHeaderByIdx(http* Http, u32 Idx)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_aux* Aux = (io_aux*)&Conn[1];
    
    string Header = GetHeaderByIdx(&Aux->Request, Idx);
    http_buf Result = { Header.Base, Header.WriteCur };
    return Result;
}

internal int
AppRecvFullRequestBody(http* Http, size_t MaxBodySize)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_aux* Aux = (io_aux*)&Conn[1];
    
    if (!RecvFullRequestBody(Conn, &Aux->Body, MaxBodySize))
    {
        Http->ReturnCode = Aux->Response.StatusCode;
        return 0;
    }
    return 1;
}

internal http_form
AppParseFormData(http* Http)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_aux* Aux = (io_aux*)&Conn[1];
    
    ts_multiform Form = ParseFormData(Aux->Body);
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
    io_aux* Aux = (io_aux*)&Conn[1];
    
    buffer Mem = GetMemory(Size, 0, MEM_WRITE);
    if (Mem.Base)
    {
        Aux->Response.Payload = (char*)Mem.Base;
    }
    return Mem.Base;
}

internal void*
AppAllocCookies(http* Http, u16 Size)
{
    ts_io* Conn = (ts_io*)Http->Object;
    io_aux* Aux = (io_aux*)&Conn[1];
    
    buffer Mem = GetMemory(Size, 0, MEM_WRITE);
    if (Mem.Base)
    {
        Aux->Response.Cookies = (char*)Mem.Base;
    }
    return Mem.Base;
}
