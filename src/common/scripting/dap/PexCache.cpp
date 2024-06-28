#include "PexCache.h"
#include "Utilities.h"

#include <functional>
#include <algorithm>
#include <string>
#include <common/engine/filesystem.h>

namespace DebugServer
{
	bool PexCache::HasScript(const int scriptReference)
	{
		std::lock_guard<std::mutex> scriptLock(m_scriptsMutex);

		return m_scripts.find(scriptReference) != m_scripts.end();
	}
	bool PexCache::HasScript(const std::string& scriptName)
	{
		return HasScript(GetScriptReference(scriptName));
	}
	
	std::shared_ptr<Pex::Binary> PexCache::GetCachedScript(const int ref) {
		const auto entry = m_scripts.find(ref);
		return entry != m_scripts.end() ? entry->second : nullptr;
	}
  std::shared_ptr<Pex::Binary> PexCache::GetScript(const dap::Source& source)
  {
    auto binary = GetCachedScript(GetSourceReference(source));
    if (binary)
    {
      return binary;
    }
    return GetScript((source.origin.has_value() ? source.origin.value() + ":" : "") + source.path.value(""));
  }

	std::shared_ptr<Pex::Binary> PexCache::GetScript(const std::string& fqsn)
	{
		std::lock_guard<std::mutex> scriptLock(m_scriptsMutex);
		uint32_t reference = GetScriptReference(fqsn);

		const auto entry = m_scripts.find(reference);
		if (entry != m_scripts.end()) {
      return entry->second;
    }
    auto binary = std::make_shared<Pex::Binary>();
    // scriptName without the leading <ProjectArchive.pk3>: part
    auto colonPos = fqsn.find(':');
    auto truncScriptPath = fqsn;
    std::string archiveName = "";
    if (colonPos != std::string::npos)
    {
      truncScriptPath = fqsn.substr(colonPos + 1);
      archiveName = fqsn.substr(0, colonPos);
    }

    int lump = fileSystem.FindFile(truncScriptPath.c_str());
    // just the basename of the script, look for either '/' or '\\'
    if (lump != -1)
    {
      if (archiveName.empty()){
        // get the namespace from the lump
        archiveName = fileSystem.GetResourceFileName(fileSystem.GetFileContainer(lump));
      }
      binary->scriptName = truncScriptPath.substr(truncScriptPath.find_last_of("/\\") + 1);
      binary->scriptPath = truncScriptPath;
      binary->archiveName = archiveName;
      binary->source = "";
      binary->sourceData = dap::Source();
      m_scripts.emplace(reference, binary);
      return binary;
    }
    // if (LoadPexData(scriptName, *binary))
    // {
    // 	m_scripts.emplace(reference, binary);
    // 	return binary;
    // }
		return nullptr;
	}
  bool PexCache::GetDecompiledSourceByRef(int ref, std::string& decompiledSource)
  {
    auto binary = GetCachedScript(ref);
    if (!binary)
    {
      return false;
    }
    auto lump = fileSystem.FindFile(binary->scriptPath.c_str());
    if (lump == -1)
    {
      return false;
    }
    auto size = fileSystem.FileLength(lump);
    decompiledSource.reserve(size);
    // Godspeed, you magnificent bastard
    fileSystem.ReadFile(lump, decompiledSource.data());
    return true;
  }


  inline bool GetSource(const std::string& scriptPath, std::string& decompiledSource)
  {
    auto lump = fileSystem.FindFile(scriptPath.c_str());
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

  bool PexCache::GetDecompiledSource(const dap::Source& source, std::string& decompiledSource)
  {
    auto binary = GetCachedScript(GetSourceReference(source));
    if (!binary) {
      return GetDecompiledSource((source.origin.has_value() ? source.origin.value() + ":" : "") + source.path.value(""),
                                 decompiledSource);
    }
    return GetSource(binary->scriptPath, decompiledSource);
  }
	bool PexCache::GetDecompiledSource(const std::string& fqpn, std::string& decompiledSource)
	{
		const auto binary = this->GetScript(fqpn);
		if (!binary)
		{
		 	return false;
		}
    return GetSource(binary->scriptPath, decompiledSource);
  }

	bool PexCache::GetSourceData(const std::string& scriptName, dap::Source& data)
	{
		const int sourceReference = GetScriptReference(scriptName);
		auto binary = GetScript(scriptName);
		if (!binary)
		{
			return false;
		}
		std::string normname = NormalizeScriptName(scriptName);

		// auto headerSrcName = binary->getHeader().getSourceFileName();
		// if (headerSrcName.empty()) {
		// 	headerSrcName = ScriptNameToPSCPath(normname);
		// }
		data.name = binary->scriptName;
		data.path = binary->scriptPath;
		data.sourceReference = sourceReference; // TODO: Remember to remove this when we get script references from the extension working
    data.origin = binary->archiveName;
		return true;
	}

	void PexCache::Clear() {
		std::lock_guard<std::mutex> scriptLock(m_scriptsMutex);
		m_scripts.clear();
	}
}
