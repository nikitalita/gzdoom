#pragma once

#include <map>

#include <dap/protocol.h>
#include <dap/session.h>
#include <mutex>
#include <string>
#include "Utilities.h"
#include "Protocol/range_map.h"
#include <name.h>
#include <shared_mutex>

class PFunction;
class PClassType;
class PStruct;
class VMFunction;
class VMScriptFunction;

namespace DebugServer
{
  struct Binary
  {
    using NameFunctionMap = std::map<FName, PFunction *>;
    using NameClassMap = std::map<FName, PClassType *>;
    using NameStructMap = std::map<FName, PStruct *>;
		using FunctionLineMap = DebugServer::range_map<uint32_t, VMScriptFunction *, std::less<uint32_t>, std::allocator<range_map_item<uint32_t, VMScriptFunction *>>, true>;
		using FunctionCodeMap = DebugServer::range_map<void*, VMScriptFunction *>;
    std::string archiveName;
		std::string archivePath;
    std::string scriptName;
    std::string scriptPath;
		std::string compiledPath;
    std::string sourceCode;
    dap::Source sourceData;
		int lump;
    NameFunctionMap functions;
    NameClassMap classes;
    NameStructMap structs;
		FunctionLineMap functionLineMap;
		FunctionCodeMap functionCodeMap;
    void populateFunctionMaps();
    std::string GetQualifiedPath() const;
  };

	class PexCache
	{
	public:
		using BinaryMap = std::map<int, std::shared_ptr<Binary>>;
		PexCache() = default;

		bool HasScript(int scriptReference);
		bool HasScript(const std::string &scriptName);

		std::shared_ptr<Binary> GetCachedScript(const int ref);
		std::shared_ptr<Binary> GetScript(const dap::Source &source);

		std::shared_ptr<Binary> GetScript(std::string fqsn);
		bool GetDecompiledSource(const dap::Source &source, std::string &decompiledSource);

		bool GetDecompiledSource(const std::string &fqpn, std::string &decompiledSource);
		bool GetSourceData(const std::string &scriptName, dap::Source &data);
		void Clear();
		void ScanAllScripts();
		dap::ResponseOrError<dap::LoadedSourcesResponse> GetLoadedSources(const dap::LoadedSourcesRequest &request);
	private:
		using scripts_lock = std::lock_guard<std::mutex>;
		std::shared_ptr<Binary> AddScript(const std::string &scriptPath);

		static void ScanScriptsInContainer(int baselump, BinaryMap &m_scripts, const std::string &filter = "");
		static std::shared_ptr<Binary> makeEmptyBinary(const std::string &scriptPath);
		std::mutex m_scriptsMutex;
		BinaryMap m_scripts;
	};
}
