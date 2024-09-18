#include "library.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

Library::Library()
    : m_handle(nullptr)
{
}

Library::Library(const std::string &path)
    : m_handle(nullptr)
{
	Open(path);
}

Library::~Library()
{
	if (m_handle)
	{
#ifdef _WIN32
		FreeLibrary(reinterpret_cast<HMODULE>(m_handle));
#else
		dlclose(m_handle);
#endif
		m_handle = nullptr;
	}
}

bool Library::Open(const std::string &path)
{
	m_name = std::string(path);

#ifdef _WIN32
	m_handle = reinterpret_cast<void *>(LoadLibraryEx(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH));
#else
	m_handle = dlopen(path.c_str(), RTLD_NOW);
#endif
	return IsOpen();
}

bool Library::IsOpen() const
{
	if (m_handle)
		return true;
	return false;
}

void *Library::Get(const std::string &name) const
{
	if (!IsOpen())
		return nullptr;
#ifdef _WIN32
	return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(m_handle), name.c_str()));
#else
	return dlsym(m_handle, name.c_str());
#endif
}

/*static*/ std::optional<std::string> Library::GetFullLibraryPath(void *func)
{
	if (!func)
		func = (void *)Library::GetFullLibraryPath;

#ifdef _WIN32
	HMODULE dll = NULL;
	if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(func), &dll) != 0)
	{
		char path[MAX_PATH];
		if (GetModuleFileName(dll, path, sizeof(path)) != 0)
			return path;
	}
#else
	Dl_info dlInfo;
	dladdr(func, &dlInfo);
	if (dlInfo.dli_sname != NULL && dlInfo.dli_saddr != NULL)
		return dlInfo.dli_fname;
#endif
	return std::nullopt;
}
