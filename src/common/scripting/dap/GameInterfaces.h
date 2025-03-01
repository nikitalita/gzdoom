#pragma once

#include <common/scripting/vm/vmintern.h>
#include <common/scripting/vm/vm.h>
#include <common/scripting/core/types.h>
#include "Utilities.h"
#include "dobject.h"
#include "zstring.h"

struct TT : PType{
    using TypeFlags = PType::ETypeFlags;
};

enum BasicType {
    BASIC_NONE,
    BASIC_uint32,
    BASIC_int32,
    BASIC_uint16,
    BASIC_int16,
    BASIC_uint8,
    BASIC_int8,
    BASIC_float,
    BASIC_double,
    BASIC_bool,
    BASIC_string,
    BASIC_name,
    BASIC_SpriteID,
    BASIC_TextureID,
    BASIC_TranslationID,
    BASIC_Sound,
    BASIC_Color,
    BASIC_Enum,
    BASIC_StateLabel,
    BASIC_pointer
};


static bool IsFunctionStatic(VMFunction* func)
{
    return func->VarFlags & VARF_Static || !(func->VarFlags & VARF_Method);
}

static bool IsFunctionNative(VMFunction* func)
{
    return func->VarFlags & VARF_Native;
}

static bool IsBasicType(PType* type)
{
    return type->Flags & TT::TypeFlags::TYPE_Scalar;
}

// TODO: for some reason, unitialized fields will have their lower 32-bit set to 0x00000000, but their upper 32-bit will be random;
// this is probably a bug, but for now, just check if the lower 32-bit is 0
static bool IsVMValueValid(const VMValue* val)
{
    return !(!val || !val->a || !val->i);
}

static bool isFStringValid(const FString& str)
{
    auto chars = str.GetChars();
    // check the lower 32-bits of the char pointer
    auto ptr = *(uint32_t*)&chars;
    return ptr != 0;
}

static bool isValidDobject(DObject* obj)
{
    return obj && obj->MagicID == DObject::MAGIC_ID;
}

static bool IsVMValValidDObject(const VMValue* val)
{
    return IsVMValueValid(val) && isValidDobject(static_cast<DObject*>(val->a));
}


static VMValue GetVMValueVar(DObject* obj, FName field, PType* type)
{
    if (!isValidDobject(obj))
        return VMValue();
    auto var = obj->ScriptVar(field, type);
    if (type == TypeString)
    {
        auto str = static_cast<FString*>(var);
        if (!isFStringValid(*str))
            return VMValue();
        return VMValue(str);
    // } else if (type->isStruct()){
    //     return VMValue(var);
    } else if (!type->isScalar()){
        return VMValue(var);
    }
    auto vmvar = static_cast<VMValue*>(var);
    return *vmvar;
}

static VMValue DerefValue(const VMValue * val, BasicType pointed_type){
    if (!IsVMValueValid(val))
        return VMValue();
    switch(pointed_type){
        case BASIC_SpriteID:
        case BASIC_TextureID:
        case BASIC_TranslationID:
        case BASIC_Sound:
        case BASIC_Color:
        case BASIC_name:
        case BASIC_StateLabel:
        case BASIC_Enum:
        case BASIC_uint32:
           return VMValue((int)*(uint32_t*)val->a);
        case BASIC_int32:
            return VMValue((int)*(int32_t*)val->a);
        case BASIC_uint16:
            return VMValue((int)*(uint16_t*)val->a);
        case BASIC_int16:
            return VMValue((int)*(int16_t*)val->a);
        case BASIC_uint8:
            return VMValue((int)*(uint8_t*)val->a);
        case BASIC_int8:
            return VMValue((int)*(int8_t*)val->a);
        case BASIC_float:
            return VMValue((float)*(float*)val->a);
        case BASIC_double:
            return VMValue((double)*(double*)val->a);
        case BASIC_bool:
            return VMValue((bool)*(bool*)val->a);
        case BASIC_string:{
            FString ** str = (FString**)val->a;
            return VMValue(*str);
        }
        case BASIC_pointer:
            return VMValue((void*)*(void**)val->a);
        default:
            break;
    }
    return VMValue();
}

static BasicType GetBasicType(PType* type)
{
    if (type == TypeUInt32)
        return BASIC_uint32;
    if (type == TypeSInt32)
        return BASIC_int32;
    if (type == TypeUInt16)
        return BASIC_uint16;
    if (type == TypeSInt16)
        return BASIC_int16;
    if (type == TypeUInt8)
        return BASIC_uint8;
    if (type == TypeSInt8)
        return BASIC_int8;
    if (type == TypeFloat32)
        return BASIC_float;
    if (type == TypeFloat64)
        return BASIC_double;
    if (type == TypeBool)
        return BASIC_bool;
    if (type == TypeString)
        return BASIC_string;
    if (type == TypeName)
        return BASIC_name;
    if (type == TypeSpriteID)
        return BASIC_SpriteID;
    if (type == TypeTextureID)
        return BASIC_TextureID;
    if (type == TypeTranslationID)
        return BASIC_TranslationID;
    if (type == TypeSound)
        return BASIC_Sound;
    if (type == TypeColor)
        return BASIC_Color;
    if (type->isPointer())
        return BASIC_pointer;
    if (type->Flags & TT::TypeFlags::TYPE_IntNotInt){
        std::string name = type->mDescriptiveName.GetChars();
        // if name begins with "Enum"
        if (name.find("Enum") == 0)
            return BASIC_Enum;
        if (type->Size == 4)
            return BASIC_uint32;
        if (type->Size == 2)
            return BASIC_uint16;
        if (type->Size == 1)
            return BASIC_uint8;
    }
    return BASIC_NONE;
}

static bool IsBasicNonPointerType(PType* type)
{
    return IsBasicType(type) && !(type->Flags & TT::TypeFlags::TYPE_Pointer);
}