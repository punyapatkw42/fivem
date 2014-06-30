#include "StdInc.h"
#include "ResourceManager.h"
#include "ResourceUI.h"
#include <yaml-cpp/yaml.h>

ResourceExport::ResourceExport(Resource* resource, std::string& functionName)
	: m_resource(resource), m_function(functionName)
{
	m_scriptFunction = -1;
}

Resource::Resource(std::string& name, std::string& path)
	: m_name(name), m_path(path), m_state(ResourceStateStopped)
{
	
}

void Resource::Tick()
{
	if (m_state == ResourceStateRunning)
	{
		m_scriptEnvironment->Tick();
	}
	else if (m_state == ResourceStateStopping)
	{
		m_scriptEnvironment = nullptr;

		m_state = ResourceStateStopped;

		std::string nameArgs = std::string(va("[\"%s\"]", m_name.c_str()));
		TheResources.TriggerEvent(std::string("resourceStopped"), nameArgs);

	}
}

void Resource::TriggerEvent(std::string& eventName, std::string& argsSerialized, int source)
{
	if (m_state == ResourceStateRunning)
	{
		m_scriptEnvironment->TriggerEvent(eventName, argsSerialized, source);
	}
}

void Resource::Start()
{
	if (m_state != ResourceStateStopped)
	{
		trace("Tried to start resource %s, but it wasn't in the stopped state.\n", m_name.c_str());
		return;
	}

	// resolve dependencies
	for (auto& dep : m_dependencies)
	{
		auto resource = TheResources.GetResource(dep);

		if (!resource.get())
		{
			trace("Can't resolve dependency %s from resource %s.\n", dep.c_str(), m_name.c_str());
			return;
		}

		resource->Start();
		resource->AddDependant(m_name);
	}

	m_ui = std::make_shared<ResourceUI>(this);
	m_ui->Create();

	// go
	// TODO: fix starting events
	//std::string nameArgs = std::string(va("[\"%s\"]", m_name.c_str()));
	//TheResources.TriggerEvent(std::string("resourceStarting"), nameArgs);

	if (!EnsureScriptEnvironment())
	{
		return;
	}

	// initialize any 'later' parts of the initialization file
	m_scriptEnvironment->DoInitFile(false);

	m_scriptEnvironment->LoadScripts();

	m_state = ResourceStateRunning;

	//TheResources.TriggerEvent(std::string("resourceStarted"), nameArgs);
}

bool Resource::EnsureScriptEnvironment()
{
	if (!m_scriptEnvironment.get())
	{
		m_scriptEnvironment = std::make_shared<ScriptEnvironment>(this);
		if (!m_scriptEnvironment->Create())
		{
			trace("Resource %s caused an error during loading. Please see the above lines for details.\n", m_name.c_str());

			m_state = ResourceStateError;
			return false;
		}

		ParseInfoFile();
	}

	return true;
}

void Resource::Stop()
{
	if (m_state != ResourceStateRunning)
	{
		trace("Tried to stop resource %s, but it wasn't in the running state.\n", m_name.c_str());
		return;
	}

	if (m_dependants.size())
	{
		trace("Could not stop resource %s because the following resources depend on it: ", m_name.c_str());

		for (auto& dep : m_dependants)
		{
			trace("%s ", dep.c_str());
		}

		trace("\n");

		return;
	}

	for (auto& dep : m_dependencies)
	{
		auto resource = TheResources.GetResource(dep);

		resource->RemoveDependant(m_name);
	}

	m_ui->Destroy();

	std::string nameArgs = std::string(va("[\"%s\"]", m_name.c_str()));
	TheResources.TriggerEvent(std::string("resourceStopping"), nameArgs);

	m_scriptEnvironment->Destroy();
	m_scriptEnvironment = nullptr;

	m_state = ResourceStateStopping;
}

void Resource::AddDependant(std::string& dependant)
{
	m_dependants.insert(dependant);
}

void Resource::RemoveDependant(std::string& dependant)
{
	m_dependants.erase(dependant);
}

std::string Resource::CallExport(std::string& exportName, std::string& argsSerialized)
{
	// if not running, return
	if (m_state != ResourceStateRunning)
	{
		return std::string("[null]");
	}

	// find the export
	for (auto& exp : m_exports)
	{
		if (exp.GetName() == exportName)
		{
			return m_scriptEnvironment->CallExport(exp.GetScriptFunction(), argsSerialized);
		}
	}

	return std::string("[null]");
}

int Resource::DuplicateRef(int luaRef)
{
	return m_scriptEnvironment->DuplicateRef(luaRef);
}

bool Resource::HasRef(int luaRef)
{
	return true; // FIXME: actually check
}

std::string Resource::CallRef(int luaRef, std::string& argsSerialized)
{
	return m_scriptEnvironment->CallExport(luaRef, argsSerialized);
}

void Resource::RemoveRef(int luaRef)
{
	m_scriptEnvironment->RemoveRef(luaRef);
}

bool Resource::HasExport(std::string& exportName)
{
	// find the export
	for (auto& exp : m_exports)
	{
		if (exp.GetName() == exportName)
		{
			return true;
		}
	}

	return false;
}

void Resource::AddExport(std::string exportName)
{
	m_exports.push_back(ResourceExport(this, exportName));
}

void Resource::AddDependency(std::string dependency)
{
	m_dependencies.push_back(dependency);
}

void Resource::SetMetaData(std::string key, std::string value)
{
	m_metaData.insert(std::make_pair(key, value));
}

bool Resource::Parse()
{
	if (!EnsureScriptEnvironment())
	{
		return false;
	}

	return ParseInfoFile();
}

bool Resource::ParseInfoFile()
{
	return m_scriptEnvironment->DoInitFile(true);
}