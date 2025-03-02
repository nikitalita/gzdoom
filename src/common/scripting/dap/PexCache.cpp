#include "PexCache.h"
#include "Utilities.h"
#include "GameInterfaces.h"

#include <functional>
#include <algorithm>
#include <string>
#include <common/engine/filesystem.h>
#include <zcc_parser.h>
#include "resourcefile.h"
#include "RuntimeState.h"

namespace DebugServer
{
  bool PexCache::HasScript(const int scriptReference)
  {
		scripts_lock scriptLock(m_scriptsMutex);
    return m_scripts.find(scriptReference) != m_scripts.end();
  }
  bool PexCache::HasScript(const std::string &scriptName)
  {
    return HasScript(GetScriptReference(scriptName));
  }

  PexCache::BinaryPtr PexCache::GetCachedScript(const int ref)
  {
		scripts_lock scriptLock(m_scriptsMutex);
		const auto entry = m_scripts.find(ref);
    return entry != m_scripts.end() ? entry->second : nullptr;
  }
	PexCache::BinaryPtr PexCache::GetScript(const dap::Source &source)
  {
    auto binary = GetCachedScript(GetSourceReference(source));
    if (binary)
    {
      return binary;
    }
    return GetScript(GetScriptWithQual(source.path.value(""), source.origin.value("")));
  }


PexCache::BinaryPtr PexCache::makeEmptyBinary(const std::string &scriptPath){
	auto binary = std::make_shared<Binary>();
	auto colonPos = scriptPath.find(':');
	auto truncScriptPath = scriptPath;
	if (colonPos != std::string::npos)
	{
		truncScriptPath = scriptPath.substr(colonPos + 1);
	}
	binary->lump = fileSystem.FindFile(truncScriptPath.c_str());
	int wadnum = fileSystem.GetFileContainer(binary->lump);
	binary->scriptName = truncScriptPath.substr(truncScriptPath.find_last_of("/\\") + 1);
	binary->scriptPath = truncScriptPath;
	// check for the archive name in the script path
	binary->archiveName = fileSystem.GetResourceFileName(wadnum);
	binary->archivePath = fileSystem.GetResourceFileFullName(wadnum);
	binary->sourceData = {
					.name = binary->scriptName,
					.path = binary->scriptPath,
					.origin = binary->archiveName,
					.sourceReference = GetScriptReference(binary->GetQualifiedPath()),
	};
	return binary;
}

void populateCodeMap(PexCache::BinaryPtr binary, Binary::FunctionCodeMap &functionCodeMap){
	if (!binary){
		return;
	}
	auto qualPath = binary->GetQualifiedPath();
	int i = 0;
	for (auto & func : binary->functions){
		i++;
		for (auto & variant : func.second->Variants) {
			auto vmfunc = variant.Implementation;
			if (IsFunctionNative(vmfunc) || IsFunctionAbstract(vmfunc)) {
				continue;
			}
			auto scriptFunc = static_cast<VMScriptFunction *>(vmfunc);
//			if (!CaseInsensitiveEquals(scriptFunc->SourceFileName.GetChars(), qualPath)) {
//				continue;
//			}

			void *code = scriptFunc->Code;
			void *end = scriptFunc->Code + scriptFunc->CodeSize;
			Binary::FunctionCodeMap::range_type codeRange(code, end, scriptFunc);
			auto ret = functionCodeMap.insert(true, codeRange);
			if (!ret.second){
				continue;
			}
		}
	}
}

void PexCache::ScanAllScripts(){
	scripts_lock scriptLock(m_scriptsMutex);
	m_scripts.clear();
	ScanScriptsInContainer(-1, m_scripts);
	m_globalCodeMap.clear();
	for (auto &bin: m_scripts){
		populateCodeMap(bin.second, m_globalCodeMap);
	}
	// TODO: do this dynamically
	for (auto &pair: m_globalCodeMap){
		AddDisassemblyLines(pair.mapped(), m_disassemblyMap);
	}
}

void PexCache::PopulateFromPaths(const std::vector<std::string> &scripts, BinaryMap &p_scripts, bool clobber) {
  for (auto &scriptPath: scripts){
		auto ref = GetScriptReference(scriptPath);
		if (clobber || p_scripts.find(ref) == p_scripts.end()) {
			p_scripts[ref] = makeEmptyBinary(scriptPath);
		}
	}
}


void PexCache::ScanScriptsInContainer(int baselump, BinaryMap &p_scripts, const std::string &filter){
		// TODO: Get the modified times from the unlinked objects?
		TArray<PNamespace*> namespaces;
		std::string filterPath = filter;
  	std::vector<int> filterRefs;
		if (!filter.empty()) {
			auto found = FindScripts(filter, baselump);
			if (found.empty()) {
				return;
			}
			for (auto &script: found) {
				filterRefs.push_back(GetScriptReference(script));
			}
			PopulateFromPaths(found, p_scripts, true);
		} else {
			auto found = FindAllScripts(baselump);
			if (found.empty()) {
				return;
			}
			PopulateFromPaths(found, p_scripts, true);
		}
		if (baselump == -1){
			namespaces = Namespaces.AllNamespaces;
		} else {
			for (auto ns: Namespaces.AllNamespaces) {
				if (ns->FileNum == baselump) {
					namespaces.Push(ns);
					break;
				}
			}
		}
		if (namespaces.Size() == 0){
			return;
		}
		auto addEmptyBinIfNotExists = [&](int ref, const char * scriptPath){
			if (p_scripts.find(ref) == p_scripts.end()) {
				p_scripts[ref] = makeEmptyBinary(scriptPath);
			}
		};
		for (auto ns: namespaces) {
			if (!ns){
				continue;
			}

			auto it = ns->Symbols.GetIterator();
			TMap<FName, PSymbol *>::Pair *cls_pair = nullptr;
			bool cls_found = it.NextPair(cls_pair);
			for (; cls_found; cls_found = it.NextPair(cls_pair)) {
				auto &cls_name = cls_pair->Key;
				auto &cls_sym = cls_pair->Value;
				if (!cls_sym->IsKindOf(RUNTIME_CLASS(PSymbolType))) {
					continue;
				}
				auto cls_type = static_cast<PSymbolType *>(cls_sym)->Type;
				if (!cls_type || (!cls_type->isClass() && !cls_type->isStruct())) {
					continue;
				}
				int cls_ref = -1;
				FName srclmpname = NAME_None;
				if (cls_type->isClass()) {
					auto class_type = static_cast<PClassType *>(cls_type);
					srclmpname = class_type->Descriptor->SourceLumpName;
					if (srclmpname == NAME_None) {
						// skip
						continue;
					}
					cls_ref = GetScriptReference(srclmpname.GetChars());
					// TODO: Fix for mixins, this will currently not hit if the script that contains the mixin gets added after the initial scan
					if (!filterRefs.empty() && std::find(filterRefs.begin(), filterRefs.end(), cls_ref) == filterRefs.end()){
						continue;
					}
					addEmptyBinIfNotExists(cls_ref, srclmpname.GetChars());
					p_scripts[cls_ref]->classes[cls_name] = static_cast<PClassType *>(cls_type);
				} else {
				}

				auto &cls_fields = cls_type->isClass() ? static_cast<PClassType *>(cls_type)->Symbols
																							 : static_cast<PStruct *>(cls_type)->Symbols;
				auto cls_member_it = cls_fields.GetIterator();
				TMap<FName, PSymbol *>::Pair *cls_member_pair = nullptr;
				bool cls_member_found = cls_member_it.NextPair(cls_member_pair);
				for (; cls_member_found; cls_member_found = cls_member_it.NextPair(cls_member_pair)) {
					auto &cls_member_sym = cls_member_pair->Value;
					std::vector<std::string> scriptNames;
					if (cls_member_sym->IsKindOf(RUNTIME_CLASS(PFunction))) {
						auto pfunc = static_cast<PFunction *>(cls_member_sym);
						for (auto &variant: pfunc->Variants) {
							if (variant.Implementation) {
								auto vmfunc = variant.Implementation;
								if (IsFunctionNative(vmfunc)) {
									continue;
								}
								auto scriptFunc = static_cast<VMScriptFunction *>(vmfunc);
								if (scriptFunc) {
									if (scriptFunc->SourceFileName.IsEmpty()){
										// abstract function
										if (cls_ref == -1 || !IsFunctionAbstract(vmfunc)){
											continue;
										}
										addEmptyBinIfNotExists(cls_ref, srclmpname.GetChars());
										p_scripts[cls_ref]->functions[scriptFunc->QualifiedName] = pfunc;
										continue;
									}
									auto script_ref = GetScriptReference(scriptFunc->SourceFileName.GetChars());
									if (!filterRefs.empty() && std::find(filterRefs.begin(), filterRefs.end(), script_ref) == filterRefs.end()){
										continue;
									}
									addEmptyBinIfNotExists(script_ref, scriptFunc->SourceFileName.GetChars());
									auto this_bin = p_scripts[script_ref];
									this_bin->functions[scriptFunc->QualifiedName] = pfunc;
									if (cls_type->isStruct() && this_bin->structs.find(cls_name) == this_bin->structs.end()) {
										this_bin->structs[cls_name] = static_cast<PStruct *>(cls_type);
									}
									break;
								}
							}
						}
					}
				}
			}
		}

		for (auto &bin: p_scripts) {
			bin.second->populateFunctionMaps();
		}
	}


std::shared_ptr<Binary> PexCache::AddScript(const std::string &scriptPath) {
	scripts_lock scriptLock(m_scriptsMutex);
	return _AddScript(scriptPath);
}

	std::shared_ptr<Binary> PexCache::_AddScript(const std::string &scriptPath){
		std::shared_ptr<Binary> bin;

		ScanScriptsInContainer(-1, m_scripts, scriptPath);
		bin = GetCachedScript(GetScriptReference(scriptPath));
		populateCodeMap(bin, m_globalCodeMap);

		return bin;
	}
  std::shared_ptr<Binary> PexCache::GetScript(std::string fqsn)
  {
		uint32_t reference = GetScriptReference(fqsn);
		auto binary = GetCachedScript(reference);
		if (binary){
			return binary;
		}
		return AddScript(fqsn);
  }

  inline bool GetSourceContent(const std::string &scriptPath, std::string &decompiledSource)
  {
    auto lump = GetScriptFileID(scriptPath);
    if (lump == -1)
    {
      return false;
    }
    auto size = fileSystem.FileLength(lump);

    std::vector<uint8_t> buffer(size);
    // Godspeed, you magnificent bastard
    fileSystem.ReadFile(lump, buffer.data());
    decompiledSource = std::string(buffer.begin(), buffer.end());
    return true;
  }

  bool PexCache::GetDecompiledSource(const dap::Source &source, std::string &decompiledSource)
  {
    auto binary = this->GetScript(source);
    if (!binary)
    {
      return false;
    }
    return GetSourceContent(binary->scriptPath, decompiledSource);
  }

  bool PexCache::GetDecompiledSource(const std::string &fqpn, std::string &decompiledSource)
  {
    const auto binary = this->GetScript(fqpn);
    if (!binary)
    {
      return false;
    }
    return GetSourceContent(binary->scriptPath, decompiledSource);
  }

  bool PexCache::GetSourceData(const std::string &scriptName, dap::Source &data)
  {
    auto binary = GetScript(scriptName);
    if (!binary)
    {
      return false;
    }
		data = binary->sourceData;
    return true;
  }

  void PexCache::Clear()
  {
		scripts_lock scriptLock(m_scriptsMutex);
    m_scripts.clear();
		m_globalCodeMap.clear();
		m_disassemblyMap.clear();
  }

	dap::ResponseOrError<dap::LoadedSourcesResponse> PexCache::GetLoadedSources(const dap::LoadedSourcesRequest &request){
		dap::LoadedSourcesResponse response;
		ScanAllScripts();
		scripts_lock scriptLock(m_scriptsMutex);
		for (auto &bin: m_scripts){
			response.sources.push_back(bin.second->sourceData);
		}
		return response;
	}


uint64_t PexCache::AddDisassemblyLines(VMScriptFunction* func, DisassemblyMap &instructions){
#if defined(_WIN32) || defined(_WIN64)
	// TODO: add a windows-compatible fmemopen
	 return 0;
#else
	if (!func || IsFunctionAbstract(func) || IsFunctionNative(func)){
		return 0;
	}
	// we need to create a temporary FILE* to pass to Disassemble
	// we can't use a string because Disassemble expects a FILE*
	// assume 256 bytes per instruction
	size_t buf_size = func->CodeSize * 256;
	std::vector<uint8_t> buffer(buf_size);
	FILE *f = fmemopen(buffer.data(), buf_size, "w");
	if (!f)
	{
		LogError("Failed to create a temporary file for disassembly");
		return 0;
	}
	auto ref = GetScriptReference(func->SourceFileName.GetChars());
	auto startPointer = func->Code;
	auto endPointer = func->Code + func->CodeSize;
	auto currCodePointer = func->Code;

	VMDisasm(f, func->Code, func->CodeSize, func, (uint64_t)func->Code);
	// close the file
	fclose(f);

	// now we can read the disassembled code from CodeBytes
	std::string disassembly = std::string(buffer.begin(), std::find(buffer.begin(), buffer.end(), 0));
	// split it into lines
	auto lines = Split(disassembly, "\n");


	auto ret = m_disassemblyMap.insert(true, {startPointer, endPointer, std::vector<std::shared_ptr<DisassemblyLine>>()});
	if (!ret.second){
		if (!(ret.first->start_pt() == startPointer && ret.first->end_pt() == endPointer)){
			LogError("Failed to insert the disassembly lines into the map");
			return 0;
		}
		// else, just use the already existing one but clear it
		ret.first->mapped().clear();
	}
	auto &lines_vec = ret.first->mapped();
	size_t lines_added = 0;
	// check if the last line in lines is empty; if so, remove it
	std::vector<size_t> lines_to_remove;
	for (size_t i = 0; i < lines.size(); i++)
	{
		auto &line = lines[i];
		if (line.empty())
		{
			continue;
		}
		auto comment_pos = line.find(';');
		if (comment_pos == std::string::npos)
		{
			// TODO: make this less hacky
			// there was a string literal with a newline in it, so we need to check the next line(s) for a comment
			for (size_t j = i + 1; j < lines.size(); j++)
			{
				auto &next_line = lines[j];
				line += "\n" + next_line;
				auto next_comment_pos = next_line.find(';');
				next_line = "";
				if (next_comment_pos != std::string::npos)
				{
					comment_pos = line.find(';');
					break;
				}
			}
			if (comment_pos == std::string::npos)
			{
				LogError("!!!!!!Disassembly line %d has no comment!!!!!", i);
				continue;
			}
		}
		if (line.size() < 19)
		{
			LogError("!!!!!!Disassembly line %d too short!!!!!", i);
			continue;
		}
		// lines go like this:
		// ip        opcode   op      arg1, arg2, arg3             ;arg1,arg2,arg3 {[resolved symbol]}(optional)
		// 00000464: 611e0201 call    [0x1319fc620],2,1            ;30,2,1  [ZTBotController.PickTeam]
		//
		// we want to extract the ip, the opcode, and the op
		// we also want to remove the ip and the opcode from the line
		auto col_pos = line.find(':');
		auto ipStr = line.substr(0, col_pos);
		auto opcode = line.substr(col_pos + 2, 8);
		auto op = line.substr(col_pos + 11, 8);
		op.erase(std::remove(op.begin(), op.end(), ' '), op.end());

		auto inst_str = line.substr(col_pos + 11,
																comment_pos - col_pos - 11);
		// trim the whitespace from inst_str
		inst_str.erase(std::remove(inst_str.begin(), inst_str.end(), ' '), inst_str.end());
		std::string comment;
		comment = line.substr(comment_pos + 1);
		// get the resolved_symbol if it exists
		std::string resolved_symbol;
		if (!comment.empty())
		{
			// find the first open bracket in the comment
			auto open_bracket = comment.find('[');
			if (open_bracket != std::string::npos)
			{
				// find the last close bracket
				auto close_bracket = comment.find_last_of(']');
				if (close_bracket != std::string::npos)
				{
					resolved_symbol = comment.substr(open_bracket + 1, close_bracket - open_bracket - 1);
				}
			}
		}
		auto ipnum = std::stoull(ipStr, nullptr, 16);

		auto instruction = std::make_shared<DisassemblyLine>();
		instruction->instruction = line.substr(col_pos + 11);
		bool mismatch = false;
		// convert ip string to void*
		instruction->address = (void*)ipnum;
		instruction->bytes = opcode;
		instruction->comment = comment;
		instruction->ref = ref;
		instruction->line = func->PCToLine((const VMOP *)instruction->address);
		instruction->is_valid_bp = true;
		if (instruction->line < 0) {
			// find the max line number
			int max_line = 0;
			for (size_t li = 0; li < func->LineInfoCount; ++li){
				if (func->LineInfo[li].LineNumber > max_line){
					max_line = func->LineInfo[li].LineNumber;
				}
			}
			instruction->line = max_line + 1;
		}
		if (!resolved_symbol.empty())
		{
			instruction->pointed_symbol = resolved_symbol;
		}
		// The reason for this is that the disassembler decodes a cmp and then {jne,je,etc} as a single {bne,be,etc} instruction
		// We want the instructions to always be 4 bytes long, so we add a dummy instruction if the current instruction is 8 bytes long
		// if instruction starts with "b"
		if (instruction->instruction.front() == 'b')
		{
			if (instruction->instruction[1] == 'n' || instruction->instruction[1] == 'e'|| instruction->instruction[1] == 'l' || instruction->instruction[1] == 'g' ){
				instruction->bytesize = 8;
				currCodePointer++;
				instruction->bytes += StringFormat("%02X%02X%02X%02X", currCodePointer->op, currCodePointer->a, currCodePointer->b, currCodePointer->c);
			}
			// TODO: do this instead of the above
			// lines_vec.push_back(instruction);
			// currCodePointer++;
			// instruction = std::make_shared<DisassemblyLine>();
			// instruction->address = (void*)currCodePointer;
			// instruction->bytes = StringFormat("%02X%02X%02X%02X", currCodePointer->op, currCodePointer->a, currCodePointer->b, currCodePointer->c);
			// instruction->instruction = StringFormat("; jmp %d", currCodePointer->i24);
			// instruction->comment = "";
			// instruction->ref = ref;
			// instruction->line = func->PCToLine((const VMOP *)instruction->address);
		}
		lines_vec.push_back(instruction);
		currCodePointer++;
		lines_added++;
	}


	return lines_added;
#endif
}

bool PexCache::GetDisassemblyLines(const VMOP* address, int64_t instructionOffset, uint64_t count, std::vector<std::shared_ptr<DisassemblyLine>> &lines_vec){
	// if the offset is negative, we get the previous instructions
	if (!address){
		return false;
	}
	auto ret = m_globalCodeMap.find_ranges((void*) address);

	if (ret.empty()){
		return false;
	}
	auto &it = ret.top();

	if (instructionOffset < 0){
		// Keep going back until we find enough instructions to fill the request
		int64_t instructions_until_start = -instructionOffset;
		bool first = true;
		Binary::FunctionCodeMap::iterator prev_it = it;
		auto test_it = Binary::FunctionCodeMap::reverse_iterator(it);
		while (instructions_until_start > 0 && count > 0) {
			// get the previous range
			if (prev_it == m_globalCodeMap.end()){
				break;
			}
			auto start_pt = prev_it->start_pt();
			auto end_pt = prev_it->end_pt();
			auto found = m_disassemblyMap.find_ranges(prev_it->start_pt());
			if (found.empty()) {
				AddDisassemblyLines(prev_it->mapped(), m_disassemblyMap);
				found = m_disassemblyMap.find_ranges(prev_it->start_pt());
			}
			auto &found_lines = found.top()->mapped();
			if (first){
				for (auto &line: found_lines){
					if (line->address == address || (line->bytesize == 8 && line->address == (void*)(address - 1))){
						break;
					}
					lines_vec.push_back(line);
					instructions_until_start--;
					count--;
					if (instructions_until_start <= 0 || count <= 0){
						break;
					}
				}
				first = false;
			} else {
				// get the reverse iterator to the end of the found lines
				auto rit = found_lines.rbegin();
				for (; rit != found_lines.rend(); rit++){
					lines_vec.insert(lines_vec.begin(), *rit);
					instructions_until_start--;
					count--;
					if (instructions_until_start <= 0 || count <= 0){
						break;
					}
				}
			}
			// get the reverse iterator to the end of the found lines
			// concatenate the found lines with the already existing lines such that the found lines appear on top of the existing ones
			if (instructions_until_start <= 0 || count <= 0){
				instructionOffset = -instructions_until_start;
				break;
			}
			// No more code behind this
			if (prev_it == m_globalCodeMap.begin()){
				return false;
			}
			prev_it--;
		}
	}
	if (instructionOffset >= 0 && count > 0){
		int64_t instructions_to_skip = instructionOffset;
		bool first = true;

		while (count > 0){
			if (it == m_globalCodeMap.end()){
				break;
			}
			auto found = m_disassemblyMap.find_ranges(it->start_pt());
			if (found.empty()){
				AddDisassemblyLines(it->mapped(), m_disassemblyMap);
				found = m_disassemblyMap.find_ranges(it->start_pt());
			}
			auto &found_lines = found.top()->mapped();
			if (first){
				for (auto &line: found_lines){
					if (line->address == address || (line->bytesize == 8 && line->address == (void*)(address - 1))){
						break;
					}
					instructions_to_skip++;
				}
				first = false;
			}
			for (auto &line: found_lines){
				if (instructions_to_skip <= 0){
					lines_vec.push_back(line);
					count--;
					if (count == 0){
						break;
					}
				}
				instructions_to_skip--;
			}
			it++;
		}
	}
}

dap::ResponseOrError<dap::DisassembleResponse> PexCache::Disassemble(const dap::DisassembleRequest &request) {

#if defined(_WIN32) || defined(_WIN64)
	RETURN_DAP_ERROR("Disassemble not supported on Windows");
#else
	auto ref = request.memoryReference;
	// ref is in the format "0x12345678", we need to convert it to a number
	if (ref.size() < 3 || ref[0] != '0' || ref[1] != 'x')
	{
		RETURN_DAP_ERROR("Invalid memoryReference");
	}
	const uint64_t req_address = std::stoull(ref.substr(2), nullptr, 16);
	const int64_t offset = request.instructionOffset.value(0);
	const VMOP* currCodePointer = (VMOP*)req_address;
	auto response = dap::DisassembleResponse();
	// the Disassemble request expects the EXACT number of instructions requested, so we need to fill in the gaps with "<INVALID>"
	auto add_invalid_inst_to_response = [&](size_t count)
	{
		for (size_t i = 0; i < count; i++)
		{
			auto instruction = dap::DisassembledInstruction();
			instruction.instruction = "<INVALID>";
			instruction.address = StringFormat("%p", currCodePointer);
			response.instructions.push_back(instruction);
			currCodePointer++;
		}
	};

	int64_t remaining_instructions = request.instructionCount;
	std::vector<std::shared_ptr<DisassemblyLine>> lines;
	GetDisassemblyLines(currCodePointer, offset, request.instructionCount, lines);
	BinaryPtr bin;
	std::vector<std::string> instruction_addrs;
	for (auto &line: lines){
		auto instruction = dap::DisassembledInstruction();
		instruction.instruction = line->instruction;
		instruction.address = StringFormat("%p", line->address);
		instruction_addrs.push_back(instruction.address);
		instruction.line = line->line;
		// only map the source for the first instruction, or if the source location has changed
//		if (!bin || bin->sourceData.sourceReference.value(-1) != line->ref){
			bin = GetCachedScript(line->ref);
			if (bin) {
				instruction.location = bin->sourceData;
			}
//		}
		instruction.instructionBytes = line->bytes;
		response.instructions.push_back(instruction);
	}
	return response;
#endif
}

}

std::string DebugServer::Binary::GetQualifiedPath() const {
  return archiveName + ":" + scriptPath;
}

void DebugServer::Binary::populateFunctionMaps() {
  functionLineMap.clear();
  functionCodeMap.clear();
	auto qualPath = GetQualifiedPath();
	// TODO: REMOVE THIS ONLY FOR TESTING
	std::vector<std::pair<FunctionLineMap::range_type, std::pair<std::string, std::pair<PFunction*, VMScriptFunction*>>>> ranges_inserted;
	int i = 0;
	for (auto & func : functions){
		i++;
		for (auto & variant : func.second->Variants) {

			auto vmfunc = variant.Implementation;
			if (IsFunctionNative(vmfunc) || IsFunctionAbstract(vmfunc)) {
				continue;
			}
			auto scriptFunc = static_cast<VMScriptFunction *>(vmfunc);
			if (!CaseInsensitiveEquals(scriptFunc->SourceFileName.GetChars(), qualPath)) {
				continue;
			}

			uint32_t firstLine = INT_MAX;
			uint32_t lastLine = 0;

			for (uint32_t i = 0; i < scriptFunc->LineInfoCount; ++i) {
				if (scriptFunc->LineInfo[i].LineNumber < firstLine) {
					firstLine = scriptFunc->LineInfo[i].LineNumber;
				}
				if (scriptFunc->LineInfo[i].LineNumber > lastLine) {
					lastLine = scriptFunc->LineInfo[i].LineNumber;
				}
			}

			FunctionLineMap::range_type range(firstLine, lastLine + 1, scriptFunc);
			ranges_inserted.push_back({range,{func.first.GetChars(), {func.second, scriptFunc}}});
			auto ret = functionLineMap.insert(true, range);
			if (ret.second == false) {
				// Probably a mixin, just continue
				continue;
			}
			void *code = scriptFunc->Code;
			void *end = scriptFunc->Code + scriptFunc->CodeSize;
			FunctionCodeMap::range_type codeRange(code, end, scriptFunc);
			functionCodeMap.insert(true, codeRange);
		}
    
  }

}

std::pair<int, int> DebugServer::Binary::GetFunctionLineRange(const VMScriptFunction *func) {
	for (auto &pair: functionLineMap) {
		if (pair.mapped() == func) {
			return {pair.range().get_left(), pair.range().get_right()};
		}
	}
	return {0, 0};

}
