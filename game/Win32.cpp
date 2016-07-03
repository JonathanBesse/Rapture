#include "sys_local.h"
#include <direct.h>
#include <process.h>

char* Sys_FS_GetHomepath() {
	// not sure
	return nullptr;
}

char* Sys_FS_GetBasepath() {
	static char cwd[1024];
	_getcwd(cwd, 1023);
	cwd[1023] = '\0';
	return cwd;
}

void Sys_FS_MakeDirectory(const char* path) {
	CreateDirectory(path, nullptr);
}

void Sys_RunThread(void (*threadRun)(void*), void* arg) {
	_beginthread(threadRun, 0, arg);
}

// http://stackoverflow.com/questions/14762456/getclipboarddatacf-text
string Sys_GetClipboardContents() {
	if(!OpenClipboard(nullptr))
		return "";

	// Get handle of clipboard object for ANSI text
	HANDLE hData = GetClipboardData(CF_TEXT);
	if (hData == nullptr)
		return ""; // error

	// Lock the handle to get the actual text pointer
	char * pszText = static_cast<char*>( GlobalLock(hData) );
	if (pszText == nullptr)
		return ""; // error

	// Save text in a string class instance
	std::string text( pszText );

	// Release the lock
	GlobalUnlock( hData );

	// Release the clipboard
	CloseClipboard();

	return text;
}

void Sys_SendToClipboard(string text) {
	if(text.length() <= 0) {
		OpenClipboard(0);
		EmptyClipboard();
		CloseClipboard();
		return;
	}
	HGLOBAL hMem =  GlobalAlloc(GMEM_MOVEABLE, text.length());
	if(hMem == 0) {
		R_Message(PRIORITY_ERROR, "Sys_SendToClipboard: couldn't GlobalAlloc (out of memory?)\n");
		return;
	}
	memcpy(GlobalLock(hMem), text.c_str(), text.length());
	GlobalUnlock(hMem);
	OpenClipboard(0);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, hMem);
	CloseClipboard();
}



//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
//(from http://stackoverflow.com/questions/1387064/how-to-get-the-error-message-from-the-error-code-returned-by-getlasterror)
std::string GetLastErrorAsString()
{
	//Get the error message, if any.
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0)
		return std::string(); //No error message has been recorded

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return message;
}



ptModule Sys_LoadLibrary(string name) {
	name.append(".dll");

	string filepath;
	filepath = Filesystem::ResolveFilePath(filepath, name, "rb+");
	ptModule hLib = (ptModule)LoadLibrary(filepath.c_str());
	if(!hLib) {
		R_Message(PRIORITY_ERROR, "module load failure: %s (%i)\n", GetLastErrorAsString().c_str(), GetLastError());
	}
	return hLib;
}

void Sys_FreeLibrary(ptModule module) {
	if(!module) {
		R_Message(PRIORITY_ERROR, "attempted to free library which doesn't exist!\n");
		return;
	}
	FreeLibrary((HMODULE)module);
}

ptModuleFunction Sys_GetFunctionAddress(ptModule module, string name) {
	return (ptModuleFunction)GetProcAddress((HMODULE)module, name.c_str());
}

bool Sys_Assertion(const char* msg, const char* file, const unsigned int line) {
	char text[256];
	sprintf(text, "Assertion Failure!\r\nFile: %s\r\nLine: %i\r\nExpression: %s", file, line, msg);
	int val = MessageBox(nullptr, text, "Rapture Assertion Failure",  MB_ABORTRETRYIGNORE|MB_ICONWARNING|MB_TASKMODAL|MB_SETFOREGROUND|MB_TOPMOST);
	switch(val) {
		default:
		case IDABORT:
			Cmd::ProcessCommand("quit");
			return false;
		case IDRETRY:
			return true;
		case IDIGNORE:
			return false;
	}
}

void Sys_Error(const char* error, ...) {
	va_list		argptr;
	char		text[4096];
	va_start (argptr, error);
	vsnprintf(text, sizeof(text), error, argptr);
	va_end (argptr);
	R_Message(PRIORITY_ERRFATAL, text);
	R_Message(PRIORITY_MESSAGE, "\n");

	Video::Shutdown();

	setGameQuitting(false);
	throw false;
}