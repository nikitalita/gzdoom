#include "PexCache.h"
#include "Utilities.h"
#include "GameInterfaces.h"

#include <functional>
#include <algorithm>
#include <string>
#include <common/engine/filesystem.h>
#include <zcc_parser.h>
#include "resourcefile.h"
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

  std::shared_ptr<Binary> PexCache::GetCachedScript(const int ref)
  {
		scripts_lock scriptLock(m_scriptsMutex);
		const auto entry = m_scripts.find(ref);
    return entry != m_scripts.end() ? entry->second : nullptr;
  }
  std::shared_ptr<Binary> PexCache::GetScript(const dap::Source &source)
  {
    auto binary = GetCachedScript(GetSourceReference(source));
    if (binary)
    {
      return binary;
    }
    return GetScript((source.origin.has_value() ? source.origin.value() + ":" : "") + source.path.value(""));
  }


std::shared_ptr<Binary> PexCache::makeEmptyBinary(const std::string &scriptPath){
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

void PexCache::ScanAllScripts(){
	scripts_lock scriptLock(m_scriptsMutex);
	ScanScriptsInContainer(-1, m_scripts);
}


void PexCache::ScanScriptsInContainer(int baselump, BinaryMap &p_scripts, const std::string &filter){
		TArray<PNamespace*> namespaces;
		std::string filterPath = filter;
		int filterRef = -1;
		if (!filter.empty()) {
			std::string truncScriptPath = GetScriptPathNoQual(filter);
			int filelump = fileSystem.FindFile(truncScriptPath.c_str());
			if (filelump == -1) {
				return;
			}
			baselump = fileSystem.GetFileContainer(filelump);
			if (baselump == -1) {
				return;
			}
			if (truncScriptPath == filter) {
				filterPath = GetScriptWithQual(filter, fileSystem.GetResourceFileName(baselump));
			}
			filterRef = GetScriptReference(filterPath);
			p_scripts[filterRef] = makeEmptyBinary(filterPath);
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
		// get all the script names in the container
		for (auto ns: namespaces) {
			if (!ns){
				continue;
			}
			std::vector<std::string> scriptNames;
			if (filterRef == -1) {
				for (int i = 0; i < fileSystem.GetNumEntries(); ++i) {
					if (baselump == -1 || fileSystem.GetFileContainer(i) == ns->FileNum) {
						std::string scriptPath = fileSystem.GetFileFullName(i);
						if (!isScriptPath(scriptPath)) {
							continue;
						}
						p_scripts[GetScriptReference(scriptPath)] = makeEmptyBinary(scriptPath);
						scriptNames.push_back(scriptPath);
					}
				}
			}
			if (p_scripts.empty()){
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
					if (filterRef != -1 && filterRef != cls_ref){
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
									if (filterRef != -1 && filterRef != script_ref){
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

	std::shared_ptr<Binary> PexCache::AddScript(const std::string &scriptPath){
		{
			scripts_lock scriptLock(m_scriptsMutex);
			ScanScriptsInContainer(-1, m_scripts, scriptPath);
		}
		return GetCachedScript(GetScriptReference(scriptPath));
	}

  std::shared_ptr<Binary> PexCache::GetScript(std::string fqsn)
  {
		if (!ScriptHasQual(fqsn)){
			auto archive_name = GetArchiveName(fqsn);
			if (archive_name.empty()){
				return nullptr;
			}
			fqsn = GetScriptWithQual(fqsn, archive_name);
		}
		uint32_t reference = GetScriptReference(fqsn);
		auto binary = GetCachedScript(reference);
		if (binary){
			return binary;
		}
		return AddScript(fqsn);
  }

  inline bool GetSourceContent(const std::string &scriptPath, std::string &decompiledSource)
  {
    auto lump = fileSystem.FindFile(GetScriptPathNoQual(scriptPath).c_str());
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
