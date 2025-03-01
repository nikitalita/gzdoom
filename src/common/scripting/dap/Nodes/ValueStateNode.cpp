#include "ValueStateNode.h"
#include <common/scripting/dap/Utilities.h>
#include "common/scripting/dap/GameInterfaces.h"
#include "types.h"
#include "vm.h"

namespace DebugServer
{
	ValueStateNode::ValueStateNode(std::string name, VMValue variable, PType* type) :
		m_name(name), m_variable(variable), m_type(type)
	{
	}

	dap::Variable ValueStateNode::ToVariable(const VMValue& m_variable, PType * m_type){
		dap::Variable variable;

		// if (!m_variable){
		// 	variable.type = m_type->DescriptiveName();
		// 	variable.value = "NULL";
		// 	return variable;
		// }
		if (m_type == TypeString){
				variable.type = "string";
				if (IsVMValueValid(&m_variable)){
					const FString &str = m_variable.s();
					auto chars = isFStringValid(str) ? str.GetChars() : "";
					variable.value = fmt::format("\"{}\"", chars);
				} else {
					variable.value = "<EMPTY>";
				}
		} else if (m_type->isPointer()){
			if (m_type->isClassPointer()){
				variable.type = "ClassPointer";
				auto type = PType::toClassPointer(m_type)->PointedType;
				variable.value = type->DescriptiveName();
			} else if (m_type->isFunctionPointer()){
				variable.type = "FunctionPointer";
				auto fake_function = PType::toFunctionPointer(m_type)->FakeFunction;
				variable.value = fake_function->SymbolName.GetChars();
			} else {
				auto type = m_type->toPointer()->PointedType;
				variable.type = std::string("Pointer(") + type->DescriptiveName() + ")";
				if (!IsVMValueValid(&m_variable)) {
					variable.value = "NULL";
				} else {
					// TODO: fix this
					auto val = DerefValue(&m_variable, GetBasicType(type));
					auto deref_var = ToVariable(&val, type);
					variable.value = StringFormat("0x%08x {%s}", (uint64_t)(m_variable.a), deref_var.value.c_str());
				}
			}
		} else if (m_type->isInt()){ // explicitly not TYPE_IntNotInt
			auto basic_type = GetBasicType(m_type);
			if (basic_type == BASIC_uint32){
				variable.type = "uint32";
			} else if (basic_type == BASIC_int32){
				variable.type = "int32";
			} else if (basic_type == BASIC_uint16){
				variable.type = "uint16";
			} else if (basic_type == BASIC_int16){
				variable.type = "int16";
			} else if (basic_type == BASIC_uint8){
				variable.type = "uint8";
			} else if (basic_type == BASIC_int8){
				variable.type = "int8";
			} else {
				variable.type = "int";
			}
			variable.value = StringFormat("%d", basic_type == BASIC_uint32 ? static_cast<uint32_t>(m_variable.i) : m_variable.i);
		} else if (m_type->isFloat()){
			if (m_type == TypeFloat32){
				variable.type = "float";
			} else {
				variable.type = "double";
			}
			variable.value = StringFormat("%f", m_variable.f);
		} else if (m_type->Flags & TT::TypeFlags::TYPE_IntNotInt){
			// PBool
			// PName
			// PSpriteID
			// PTextureID
			// PTranslationID
			// PSound
			// PColor
			// PStateLabel
			// PEnum
			if (m_type == TypeBool){
				variable.type = "bool";
				variable.value = m_variable.i ? "true" : "false";
			} else if (m_type == TypeName){
				variable.type = "Name";
				// TODO: how to get the name?
				variable.value = StringFormat("Name(%d)", m_variable.i);
			} else if (m_type == TypeSpriteID){
				variable.type = "SpriteID";
				variable.value = StringFormat("SpriteID(%d)", m_variable.i);
			} else if (m_type == TypeTextureID){
				variable.type = "TextureID";
				variable.value = StringFormat("TextureID(%d)", m_variable.i);
			} else if (m_type == TypeTranslationID){
				variable.type = "TranslationID";
				variable.value = StringFormat("TranslationId(%d)", m_variable.i);
			} else if (m_type == TypeSound){
				variable.type = "Sound";
				variable.value = StringFormat("Sound(%d)", m_variable.i);
			} else if (m_type == TypeColor){
				variable.type = "Color";
				// hex format
				variable.value = StringFormat("#%04x", m_variable.i);
			} else if (m_type == TypeStateLabel){
				variable.type = "StateLabel";
				variable.value = StringFormat("%d", m_variable.i);
			} else {
				variable.type = m_type->DescriptiveName();
				// check if it begins with "Enum"
				if (ToLowerCopy(variable.type.value("")).find("enum") == 0){
					auto enum_type = static_cast<PEnum*>(m_type);
					// TODO: How to get the enum value names?
					variable.value = StringFormat("%d", m_variable.i);
				} else {
					variable.value = StringFormat("%d", m_variable.i);
				}
				variable.type = "Enum";
				variable.value = StringFormat("%d", m_variable.i);
			}
		} else {
				variable.type = m_type->DescriptiveName();
				variable.value = "<ERROR?>";
		}
		return variable;
	}

	bool ValueStateNode::SerializeToProtocol(dap::Variable& variable)
	{
		variable.name = m_name;
		auto var = ToVariable(m_variable, m_type);
		variable.type = var.type;
		variable.value = var.value;
		return true;
	}
}
