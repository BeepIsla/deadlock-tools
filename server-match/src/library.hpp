#pragma once

#include <optional>
#include <string>

class Library
{
private:
	void       *m_handle;
	std::string m_name;

public:
	Library();
	Library(const std::string &path);
	~Library();
	bool  Open(const std::string &path);
	bool  IsOpen() const;
	void *Get(const std::string &name) const;

	// `func` is the function we should get the DLL of, if `nullptr` we will use `Library::GetFullLibraryPath` to get our own path
	static std::optional<std::string> GetFullLibraryPath(void *func = nullptr);
};
